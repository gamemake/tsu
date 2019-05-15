#pragma once

class TSUWEBSOCKET_API FTsuWebSocketServer
{
public:
	class ICallback
	{
	public:
		virtual void OnHttp(FTsuWebSocketRequest& Request, FTsuWebSocketResponse& Response) = 0;
		virtual void OnOpen(FTsuWebSocketConnection* Conn) = 0;
		virtual void OnClosed(FTsuWebSocketConnection* Conn) = 0;
		virtual void OnReceive(FTsuWebSocketConnection* Conn, const FString& Data) = 0;
		virtual void OnWriteable(FTsuWebSocketConnection* Conn) = 0;
	};

	FTsuWebSocketServer();
	~FTsuWebSocketServer();

	void AddProtocol(const char* Name, size_t BufferSize, ICallback* Callback);

	bool Start(int Port);
	bool Run(int Timeout=0);
	void Stop();

private:
	static int ProtocolCallback(lws* Wsi, enum lws_callback_reasons Reason, void* User, void* In, size_t Len);

	struct ProtocolData
	{
		const char* Name;
		size_t BufferSize;
		ICallback* Callback;
	};

	TArray<ProtocolData> Protocols;
	lws_protocols* WebSocketProtocols;
	lws_context* WebSocketContext;
};
