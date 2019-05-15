#pragma once

#include "CoreMinimal.h"
#include "TsuWebSocket.h"
#include "TsuV8Wrapper.h"

class TSUINSPECTOR_API FTsuInspectorChannel
	: public v8_inspector::V8Inspector::Channel
{
public:
    FTsuInspectorChannel(FTsuWebSocketConnection* Conn, v8_inspector::V8Inspector& Inspector);
    virtual ~FTsuInspectorChannel();

    void dispatchMessage(const FString& data);
    void sendResponse(int callId, std::unique_ptr<v8_inspector::StringBuffer> message) override;
    void sendNotification(std::unique_ptr<v8_inspector::StringBuffer> message) override;
    void flushProtocolNotifications() override;

private:
    FTsuWebSocketConnection* Conn;
    std::unique_ptr<v8_inspector::V8InspectorSession> InspectorSession;
};
