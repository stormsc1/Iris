#include <Iris/Server.h>

enum class CustomMsgType : uint32_t
{
	ServerAccept,
	ServerDeny,
	ServerPing,
	MessageAll,
	ServerMessage
};

class CustomServer : public IRIS::ServerInterface<CustomMsgType>
{
public:
	CustomServer(uint16_t port)
		: IRIS::ServerInterface<CustomMsgType>(port)
	{
	}

protected:
	virtual bool OnClientConnect(std::shared_ptr<IRIS::Connection<CustomMsgType>> client)
	{
		return true;
	}

	virtual void OnClientDisconnect(std::shared_ptr<IRIS::Connection<CustomMsgType>> client)
	{

	}

	virtual void OnRecivePackage(std::shared_ptr<IRIS::Connection<CustomMsgType>> client, IRIS::Package<CustomMsgType> package)
	{
		std::cout << "test";

		switch (package.Header.id)
		{
		case CustomMsgType::ServerPing:
		{
			std::cout << "[" << client->GetID() << "] Server Ping" << std::endl;

			// Bounce message back to client.
			client->Send(package);
		}
		break;
		}

	}
};

class CustomClient : public IRIS::ClientInterface<CustomMsgType>
{
public:
	void PingServer()
	{
		IRIS::Package<CustomMsgType> pkg;
		pkg.Header.id = CustomMsgType::ServerPing;

		std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();

		pkg << timeNow;
		Send(pkg);

		std::cout << "test";
	}
};

int main(int argc, char* argv[])
{
	uint16_t port = 60000;
	bool isRunning = true;

	if (argc > 1)
	{
		if (std::string(argv[1]) == "client")
		{
			/// Client
			CustomClient client;
			client.Connect("127.0.0.1", port);

			bool keys[3] = { false, false, false };
			bool oldKeys[3] = { false, false, false };

			while (isRunning)
			{
				if (GetForegroundWindow() == GetConsoleWindow())
				{
					keys[0] = GetAsyncKeyState('1') & 0x8000;
					keys[1] = GetAsyncKeyState('2') & 0x8000;
					keys[2] = GetAsyncKeyState('3') & 0x8000;
				}

				if (keys[0] && !oldKeys[0]) client.PingServer();
				if (keys[2] && !oldKeys[2]) isRunning = false;

				for (int i = 0; i < 3; i++) oldKeys[i] = keys[i];

				if (client.IsConnected())
				{
					if (!client.Incoming().IsEmpty())
					{
						auto pkg = client.Incoming().PopFront().pkg;

						switch (pkg.Header.id)
						{
						case CustomMsgType::ServerPing:
						{
							std::chrono::system_clock::time_point timeNow = std::chrono::system_clock::now();
							std::chrono::system_clock::time_point timeThen;
							pkg >> timeThen;
							std::cout << "Ping: " << std::chrono::duration<double>(timeNow - timeThen).count() << std::endl;

						}
						break;
						}
					}
				}
				else
				{
					std::cout << "Lost connection to server!" << std::endl;
					isRunning = false;
				}
			}
		}
		else if (std::string(argv[1]) == "server")
		{
			/// Server
			CustomServer server(port);
			server.Start();

			while (isRunning)
			{
				server.Update(512);
			}
		}
	}

	return 0;
}