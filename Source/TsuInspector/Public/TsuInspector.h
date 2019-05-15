#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "TsuWebSocket.h"
#include "TsuV8Wrapper.h"

class FTsuInspectorChannel;

class TSUINSPECTOR_API FTsuInspector
	: public v8_inspector::V8InspectorClient
    , public FTsuWebSocketServer::ICallback
	, public FTickerObjectBase
{
public:
    FTsuInspector();
    virtual ~FTsuInspector();

    void Start(int Port);
    void Stop();

    void RegisterContext(v8::Local<v8::Context> Context);
	void UnregisterContext(v8::Local<v8::Context> Context);

    void OnHttp(FTsuWebSocketRequest& Request, FTsuWebSocketResponse& Response) override;
    void OnOpen(FTsuWebSocketConnection* Conn) override;
    void OnClosed(FTsuWebSocketConnection* Conn) override;
    void OnReceive(FTsuWebSocketConnection* Conn, const FString& Data) override;
    void OnWriteable(FTsuWebSocketConnection* Conn) override;

	bool Tick(float DeltaTime = 0.f) override;

	void runMessageLoopOnPause(int ContextGroupId) override;
	void quitMessageLoopOnPause() override;

private:
	std::unique_ptr<v8_inspector::V8Inspector> Inspector;
    FTsuWebSocketServer Server;
    TMap<FTsuWebSocketConnection*, FTsuInspectorChannel*> Channels;
    bool bIsPaused;
};
