#pragma once

#include <thread>
#include <mutex>
#include <deque>
#include <optional>
#include <vector>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif

#define ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>

#include "ThreadSafeQueue.h"

namespace IRIS
{
	template <typename T>
	struct PackageHeader
	{
	public:
		T id{};
		uint32_t size = 0;
	};

	template <typename T>
	struct Package
	{
	public:
		PackageHeader<T> Header{};
		std::vector<uint8_t> Body;

		uint32_t GetSize() const
		{
			return sizeof(PackageHeader<T>) + Body.size();
		}

		std::string ToString() const
		{
			return std::sprintf("ID: %d, Size: %d", int(Header.id), Header.size);
		}

		/// TODO: Change
		// Pushes any POD-like data into the message buffer
		template<typename DataType>
		friend Package<T>& operator << (Package<T>& msg, const DataType& data)
		{
			// Check that the type of the data being pushed is trivially copyable
			static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pushed into vector");

			// Cache current size of vector, as this will be the point we insert the data
			size_t i = msg.Body.size();

			// Resize the vector by the size of the data being pushed
			msg.Body.resize(msg.Body.size() + sizeof(DataType));

			// Physically copy the data into the newly allocated vector space
			std::memcpy(msg.Body.data() + i, &data, sizeof(DataType));

			// Recalculate the message size
			msg.Header.size = msg.GetSize();

			// Return the target message so it can be "chained"
			return msg;
		}

		// Pulls any POD-like data form the message buffer
		template<typename DataType>
		friend Package<T>& operator >> (Package<T>& msg, DataType& data)
		{
			// Check that the type of the data being pushed is trivially copyable
			static_assert(std::is_standard_layout<DataType>::value, "Data is too complex to be pulled from vector");

			// Cache the location towards the end of the vector where the pulled data starts
			size_t i = msg.Body.size() - sizeof(DataType);

			// Physically copy the data from the vector into the user variable
			std::memcpy(&data, msg.Body.data() + i, sizeof(DataType));

			// Shrink the vector to remove read bytes, and reset end position
			msg.Body.resize(i);

			// Recalculate the message size
			msg.Header.size = msg.GetSize();

			// Return the target message so it can be "chained"
			return msg;
		}
	};

	template <typename T>
	class Connection;

	template <typename T>
	struct OwnedPackage
	{
		std::shared_ptr<Connection<T>> Remote = nullptr;
		Package<T> pkg;
	};

	template<typename T>
	class Connection : public std::enable_shared_from_this<Connection<T>>
	{
	public:
		enum class Owner
		{
			Server,
			Client
		};

		Connection(Owner owner, asio::io_context& asioContext, asio::ip::tcp::socket socket, ThreadSafeQueue<OwnedPackage<T>>& inQueue)
			: m_AsioContext(asioContext), m_Socket(std::move(socket)), m_PackagesIn(inQueue), m_OwnerType(owner)
		{
		}

		virtual ~Connection() {}
	public:
		// Called when the server want's to connect to the client
		void ConnectToClient(uint32_t uid = 0)
		{
			if (m_OwnerType == Owner::Server)
			{
				if (m_Socket.is_open())
				{
					m_ID = uid;
				}
			}
		}

		void ConnectToServer(const asio::ip::tcp::resolver::results_type& endpoints)
		{
			// Only clients can connect to servers
			if (m_OwnerType == Owner::Client)
			{
				// Request asio attempts to connect to an endpoint
				asio::async_connect(m_Socket, endpoints,
					[this](std::error_code ec, asio::ip::tcp::endpoint endpoint)
				{
					if (!ec)
					{
						ReadHeader();
					}
				});
			}
		}

		void Disconnect()
		{
			if (IsConnected())
			{
				asio::post(m_AsioContext, [this]() { m_Socket.close(); });
			}
		}

		bool IsConnected() const
		{
			return m_Socket.is_open();
		}

		void Send(const Package<T>& pkg)
		{
			asio::post(m_AsioContext,
				[this, pkg]()
			{
				// If the queue has a message in it, then we must 
				// assume that it is in the process of asynchronously being written.
				// Either way add the message to the queue to be output. If no messages
				// were available to be written, then start the process of writing the
				// message at the front of the queue.
				bool writingMessage = !m_PackagesOut.IsEmpty();
				m_PackagesOut.PushBack(pkg);
				if (!writingMessage)
				{
					WriteHeader();
				}
			});
		}

		uint32_t GetID() const
		{
			return m_ID;
		}

	private:
		// ASYNC - Prime context read to a message header
		void ReadHeader()
		{
			asio::async_read(m_Socket, asio::buffer(&m_PackageTemporaryIn.Header, sizeof(PackageHeader<T>)), [this](std::error_code ec, std::uint32_t lenght)
			{
				if (!ec)
				{
					// A complete message header has been read, check if this message
					// has a body to follow...
					if (m_PackageTemporaryIn.Header.size > 0)
					{
						// ...it does, so allocate enough space in the messages' body
						// vector, and issue asio with the task to read the body.
						m_PackageTemporaryIn.Body.resize(m_PackageTemporaryIn.Header.size);
						ReadBody();
					}
					else
					{
						// it doesn't, so add this bodyless message to the connections
						// incoming message queue
						AddToIncomingMessageQueue();
					}
				}
				else
				{
					// Reading form the client went wrong, most likely a disconnect
					// has occurred. Close the socket and let the system tidy it up later.
					std::cout << "[" << m_ID << "] Read Header Fail.\n";
					m_Socket.close();
				}
			});
		}

		// ASYNC - Prime context read to a package body
		void ReadBody()
		{
			asio::async_read(m_Socket, asio::buffer(m_PackageTemporaryIn.Body.data(), m_PackageTemporaryIn.Body.size()), [this](std::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					// ...and they have! The message is now complete, so add
					// the whole message to incoming queue
					AddToIncomingMessageQueue();
				}
				else
				{
					// As above!
					std::cout << "[" << m_ID << "] Read Body Fail.\n";
					m_Socket.close();
				}
			});
		}

		void AddToIncomingMessageQueue()
		{
			// Shove it in queue, converting it to an "owned message", by initialising
			// with the a shared pointer from this connection object
			if (m_OwnerType == Owner::Server)
			{
				m_PackagesIn.PushBack({ this->shared_from_this(), m_PackageTemporaryIn });
			}
			else
			{
				m_PackagesIn.PushBack({ nullptr, m_PackageTemporaryIn });
			}

			// We must now prime the asio context to receive the next message. It 
			// wil just sit and wait for bytes to arrive, and the message construction
			// process repeats itself. Clever huh?
			ReadHeader();
		}

		// ASYNC - Prime context write to a message header
		void WriteHeader()
		{
			// If this function is called, we know the outgoing message queue must have 
				// at least one message to send. So allocate a transmission buffer to hold
				// the message, and issue the work - asio, send these bytes
			asio::async_write(m_Socket, asio::buffer(&m_PackagesOut.Front().Header, sizeof(PackageHeader<T>)),
				[this](std::error_code ec, uint32_t length)
			{
				// asio has now sent the bytes - if there was a problem
				// an error would be available...
				if (!ec)
				{
					// ... no error, so check if the message header just sent also
					// has a message body...
					if (m_PackagesOut.Front().Body.size() > 0)
					{
						// ...it does, so issue the task to write the body bytes
						WriteBody();
					}
					else
					{
						// ...it didnt, so we are done with this message. Remove it from 
						// the outgoing message queue
						m_PackagesOut.PopFront();

						// If the queue is not empty, there are more messages to send, so
						// make this happen by issuing the task to send the next header.
						if (!m_PackagesOut.IsEmpty())
						{
							WriteHeader();
						}
					}
				}
				else
				{
					// ...asio failed to write the message, we could analyse why but 
					// for now simply assume the connection has died by closing the
					// socket. When a future attempt to write to this client fails due
					// to the closed socket, it will be tidied up.
					std::cout << "[" << m_ID << "] Write Header Fail.\n";
					m_Socket.close();
				}
			});
		}

		// ASYNC - Prime context write to a package body
		void WriteBody()
		{
			// If this function is called, a header has just been sent, and that header
				// indicated a body existed for this message. Fill a transmission buffer
				// with the body data, and send it!
			asio::async_write(m_Socket, asio::buffer(m_PackagesOut.Front().Body.data(), m_PackagesOut.Front().Body.size()), [this](std::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					// Sending was successful, so we are done with the message
					// and remove it from the queue
					m_PackagesOut.PopFront();

					// If the queue still has messages in it, then issue the task to 
					// send the next messages' header.
					if (!m_PackagesOut.IsEmpty())
					{
						WriteHeader();
					}
				}
				else
				{
					// Sending failed, see WriteHeader() equivalent for description :P
					std::cout << "[" << m_ID << "] Write Body Fail.\n";
					m_Socket.close();
				}
			});
		}

	protected:
		// Each connection has a unique socket to a remote
		asio::ip::tcp::socket m_Socket;

		// The context is shared with the whole asio instance
		asio::io_context& m_AsioContext;

		// This queue holds all packages to be sent to the remote side of this connection
		ThreadSafeQueue<Package<T>> m_PackagesOut;

		// This queue holds all packages that have been recieved from the remote side of the connection.
		// Note it is a reference as the "owner" of this connection is expected to provide a queue.
		ThreadSafeQueue<OwnedPackage<T>>& m_PackagesIn;
		Package<T> m_PackageTemporaryIn;

		Owner m_OwnerType = Owner::Server;
		uint32_t m_ID = 0;
	};

	template <typename T>
	class ClientInterface
	{
	public:
		ClientInterface()
			: m_Socket(m_Context)
		{
		}

		virtual ~ClientInterface()
		{
			Disconnect();
		}

	public:
		bool Connect(const std::string& host, const uint16_t port)
		{
			try
			{
				// Resolve hostname/ip-address into tangiable physical address
				asio::ip::tcp::resolver resolver(m_Context);
				asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));

				// Create connection
				m_Connection = std::make_unique<Connection<T>>(Connection<T>::Owner::Client, m_Context, asio::ip::tcp::socket(m_Context), m_PackagesIn);

				// Tell the connection object to connect to server
				m_Connection->ConnectToServer(endpoints);

				// Start context thread
				m_ContextThread = std::thread([this]() { m_Context.run(); });
			}
			catch (std::exception& e)
			{
				std::cerr << "Client Exception: " << e.what() << std::endl;
				return false;
			}

			return false;
		}

		void Disconnect()
		{
			if (IsConnected())
			{
				m_Connection->Disconnect();
			}

			m_Context.stop();

			if (m_ContextThread.joinable())
			{
				m_ContextThread.join();
			}

			m_Connection.release();
		}

		bool IsConnected() const
		{
			if (m_Connection)
			{
				return m_Connection->IsConnected();
			}
			else
			{
				return false;
			}
		}


		// Send message to server
		void Send(const Package<T>& package)
		{
			if (IsConnected())
			{
				m_Connection->Send(package);
			}
		}

		ThreadSafeQueue<OwnedPackage<T>>& Incoming()
		{
			return m_PackagesIn;
		}
	protected:
		// Asio context handles the data transfer.
		asio::io_context m_Context;
		// ...but needs a thread of its own to execute its work commands.
		std::thread m_ContextThread;
		// This is the hardware socket that is connected to the server.
		asio::ip::tcp::socket m_Socket;
		// The client has a single instance of a "connectionn" object, which handles data transfer.
		std::unique_ptr<Connection<T>> m_Connection;

	private:
		// Queue for incoming packages from the server
		ThreadSafeQueue<OwnedPackage<T>> m_PackagesIn;
	};

	template <typename T>
	class ServerInterface
	{
	public:
		ServerInterface(uint16_t port)
			: m_AsioAcceptor(m_AsioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
		{
		}

		virtual ~ServerInterface()
		{
			Stop();
		}

		bool Start()
		{
			try
			{
				WaitForClientConnection();

				m_ContextThread = std::thread([this]() { m_AsioContext.run(); });
			}
			catch (std::exception& e)
			{
				std::cerr << "[SERVER] Exception: " << e.what() << std::endl;
				return false;
			}

			std::cout << "[SERVER] Started!" << std::endl;
			return true;
		}

		void Stop()
		{
			// Request the context to close
			m_AsioContext.stop();

			// Tidy up the context thread
			if (m_ContextThread.joinable())
			{
				m_ContextThread.join();
			}

			// Inform some, anybody, if they care...
			std::cout << "[SERVER] Stopped!" << std::endl;
		}

		// ASYNC - Instruct asio to wait for connection
		void WaitForClientConnection()
		{
			m_AsioAcceptor.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket)
			{
				if (!ec)
				{
					std::cout << "[SERVER] New Connection: " << socket.remote_endpoint() << std::endl;

					std::shared_ptr<Connection<T>> newConnection = std::make_shared<Connection<T>>(Connection<T>::Owner::Server, m_AsioContext, std::move(socket), m_PackagesIn);

					// Give the user server a chance to deny connection
					if (OnClientConnect(newConnection))
					{
						// Connection allowed, so add to coiner of active connections
						m_DequeConnections.push_back(std::move(newConnection));

						m_DequeConnections.back()->ConnectToClient(m_IDCounter++);

						std::cout << "[" << m_DequeConnections.back()->GetID() << "] Connection Approved" << std::endl;
					}
					else
					{
						std::cout << "[-----] Connection Denied" << std::endl;
					}
				}
				else
				{
					std::cout << "[SERVER] Connection Error: " << ec.message() << std::endl;
				}

				// Prime the asio cotext with more work - again simply wair for another connection...
				WaitForClientConnection();
			});
		}

		void SendPackageToClient(std::shared_ptr<Connection<T>> client, const Package<T>& package)
		{
			if (client && client->IsConnected())
			{
				client->Send(package)
			}
			else
			{
				OnClientDisconnect(client);
				client.reset();
				m_DequeConnections.erase(std::remove(m_DequeConnections.begin(), m_DequeConnections.end(), client), m_DequeConnections.end();
				)
			}
		}

		void SendPackageToAllClients(const Package<T>& package, std::shared_ptr<Connection<T>> ignoreClient = nullptr)
		{
			bool invalidClientExists = false;

			for (auto& client : m_DequeConnections)
			{
				if (client && client->IsConnected())
				{
					if (client != ignoreClient)
					{
						client->Send(package);
					}
				}
				else
				{
					// The client couldnt be conteactes, so assume it has discconected.
					OnClientDisconnect(client);
					client.reset();
					invalidClientExists = true;
				}
			}

			if (invalidClientExists)
			{
				m_DequeConnections.erase(std::remove(m_DequeConnections.begin(), m_DequeConnections.end(), nullptr), m_DequeConnections.end());
			}
		}

		void Update(uint32_t maxPackages = UINT32_MAX)
		{
			uint32_t packetCount = 0;
			while (packetCount < maxPackages && !m_PackagesIn.IsEmpty())
			{
				auto pkg = m_PackagesIn.PopFront();

				OnRecivePackage(pkg.Remote, pkg.pkg);

				packetCount++;
			}
		}
	protected:
		// Called when a client connects, you can veto the connection by returning false.
		virtual bool OnClientConnect(std::shared_ptr<Connection<T>> client)
		{
			return false;
		}

		// Called when a client appeats to have disconnected
		virtual void OnClientDisconnect(std::shared_ptr<Connection<T>> client)
		{
		}

		// Called when a message arrives
		virtual void OnRecivePackage(std::shared_ptr<Connection<T>> client, Package<T>& package)
		{

		}
	protected:
		// Thread safe queue for incoming packages
		ThreadSafeQueue<OwnedPackage<T>> m_PackagesIn;

		// Container of active validated connections
		std::deque<std::shared_ptr<Connection<T>>> m_DequeConnections;

		// Order of delaration is importatn - it is also the order if initialisation
		asio::io_context m_AsioContext;
		std::thread m_ContextThread;

		// These thinfs need an asio context
		asio::ip::tcp::acceptor m_AsioAcceptor;

		// Client will identifed in the wider system via an ID
		uint32_t m_IDCounter = 10000;
	};
}
