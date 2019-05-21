#include "TsuInspectorChannel.h"

#include "TsuInspectorLog.h"

FTsuInspectorChannel::FTsuInspectorChannel(FTsuWebSocketConnection* Conn, v8_inspector::V8Inspector& Inspector)
    : Conn(Conn)
    , InspectorSession(Inspector.connect(1, this, {}))

{
}

FTsuInspectorChannel::~FTsuInspectorChannel()
{
}

void FTsuInspectorChannel::dispatchMessage(const FString& data)
{
    UE_LOG(LogTsuInspector, Log, TEXT("dispatchMessage %p %s"), this, *data);

#if PLATFORM_TCHAR_IS_4_BYTES
    static_assert(sizeof(TCHAR) == sizeof(uint32_t), "Character size mismatch");
    auto MessageUtf16 = StringCast<char16_t>(static_cast<const TCHAR*>(*data));
    const auto MessagePtr = reinterpret_cast<const uint16_t*>(MessageUtf16.Get());
    const auto MessageLen = (size_t)MessageUtf16.Length();
#else
    static_assert(sizeof(TCHAR) == sizeof(uint16_t), "Character size mismatch");
    const auto MessagePtr = reinterpret_cast<const uint16_t*>(*data);
    const auto MessageLen = (size_t)data.Len();
#endif

    InspectorSession->dispatchProtocolMessage({MessagePtr, MessageLen});
}

void FTsuInspectorChannel::sendMessage(v8_inspector::StringBuffer& MessageBuffer)
{
    v8_inspector::StringView MessageView = MessageBuffer.string();

	FString Message;
	if (MessageView.is8Bit())
    {
		Message = UTF8_TO_TCHAR((const char*)MessageView.characters8());
	}
	else
    {
#if PLATFORM_TCHAR_IS_4_BYTES
		static_assert(sizeof(TCHAR) == sizeof(uint32_t), "Character size mismatch");
		auto Converter = StringCast<TCHAR>((const char16_t*)MessageView.characters16());
		Message = FString(Converter.Length(), Converter.Get());
#else
		static_assert(sizeof(TCHAR) == sizeof(uint16_t), "Character size mismatch");
		Message = FString((const TCHAR*)MessageView.characters16());
#endif
    }

	UE_LOG(LogTsuInspector, Log, TEXT("Send %p %p %s"), Conn, this, *Message);
	Conn->SendMessage(Message);
}

void FTsuInspectorChannel::sendResponse(int callId, std::unique_ptr<v8_inspector::StringBuffer> message)
{
    sendMessage(*message);
}

void FTsuInspectorChannel::sendNotification(std::unique_ptr<v8_inspector::StringBuffer> message)
{
    sendMessage(*message);
}

void FTsuInspectorChannel::flushProtocolNotifications()
{
}
