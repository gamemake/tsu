#include "TestInspectorCommandlet.h"
#include "TsuWebSocket.h"
#include "TsuInspectorLog.h"

class FCallback : public FTsuWebSocketServer::ICallback
{
	void OnHttp(FTsuWebSocketRequest& Request, FTsuWebSocketResponse& Response) override
	{
		UE_LOG(LogTsuInspector, Log, TEXT("OnHttp %s %s"), *Request.GetMethod(), *Request.GetUri());
		Response.End(200, CONTENT_TYPE_JSON_UTF8, Request.GetUri());
	}

	void OnOpen(FTsuWebSocketConnection* Conn) override
	{
		UE_LOG(LogTsuInspector, Log, TEXT("OnOpen %p"), Conn);
	}

	void OnClosed(FTsuWebSocketConnection* Conn) override
	{
		UE_LOG(LogTsuInspector, Log, TEXT("OnClose %p"), Conn);
	}

	void OnReceive(FTsuWebSocketConnection* Conn, const FString& Data) override
	{
		UE_LOG(LogTsuInspector, Log, TEXT("OnReceive %p %s"), Conn, *Data);
	}
	
	void OnWriteable(FTsuWebSocketConnection* Conn) override
	{
		UE_LOG(LogTsuInspector, Log, TEXT("OnWriteable %p"), Conn);
	}
};

UTestInspectorCommandlet::UTestInspectorCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

static bool Quit = false;

int32 UTestInspectorCommandlet::Main(const FString& Params)
{
	FTsuWebSocketServer Server;
	FCallback Callback;

	Server.AddProtocol("protocol", 5 * 1024, &Callback);

	if (!Server.Start(1394))
	{
		UE_LOG(LogTsuInspector, Log, TEXT("Failed to start websocket server"));
		return 1;
	}

	UE_LOG(LogTsuInspector, Log, TEXT("Server started."));

	while (!Quit)
	{
		// UE_LOG(LogTsuCommandlet, Log, TEXT("Do Run"));
		Server.Run(2000);
	}

	UE_LOG(LogTsuInspector, Log, TEXT("Server stopping."));
	Server.Stop();
	return 0;
}
