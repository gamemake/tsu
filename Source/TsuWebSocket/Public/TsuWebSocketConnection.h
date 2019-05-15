#pragma once

class FTsuWebSocketServer;

class TSUWEBSOCKET_API FTsuWebSocketConnection
{
	friend class FTsuWebSocketServer;
public:
	FTsuWebSocketConnection(lws* Wsi);
	~FTsuWebSocketConnection();

	void SetUser(void* Data) { User = Data; }
	
	template<typename T>
	T* GetUser() { return (T)User; }

	void SendMessage(const FString& Data);

protected:
	lws* Wsi;
	void* User;

	bool IsWriteable;
	TArray<FString> SendQueue;
};
