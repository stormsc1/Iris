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

	}
};

class CustomClient : public IRIS::ClientInterface<CustomMsgType>
{

};

int main(int argc, char* argv[])
{
	if (argv[1] == "--server")
	{
		CustomServer server(60000);
		server.Start();

		while (1)
		{
			server.Update();
		}
	}
	else
	{
		CustomClient client;
		client.Connect("127.0.0.1", 60000);

		while (1)
		{
		}
	}


	return 0;
}