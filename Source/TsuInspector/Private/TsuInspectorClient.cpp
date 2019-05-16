#include "TsuInspectorClient.h"

#include "TsuInspectorChannel.h"
#include "TsuInspectorLog.h"
#include "TsuIsolate.h"

// #todo(#mihe): Should be the actual stringified port
const FString JsonList(
	TEXT("([{\n")
	TEXT("  \"description\": \"TSU instance\",\n")
	TEXT("  \"id\": \"2e0d32cf-212c-4f25-9ec8-4896e652513a\",\n")
	TEXT("  \"title\": \"TSU\",\n")
	TEXT("  \"type\": \"node\",\n")
	TEXT("  \"webSocketDebuggerUrl\": \"ws://127.0.0.1:{PORT}/\"\n")
	TEXT("}])"));

// #todo(#mihe): Should be the actual stringified version number
const FString JsonVersion(
	TEXT("({\n")
	TEXT("  \"Browser\": \"TSU/v0.1.0\",\n")
	TEXT("  \"Protocol-Version\": \"1.1\"\n")
	TEXT("})"));

FTsuInspectorClient::FTsuInspectorClient()
{
	Inspector = v8_inspector::V8Inspector::create(FTsuIsolate::Get(), this);
	bIsPaused = false;
	ServerPort = -1;
}

FTsuInspectorClient::~FTsuInspectorClient()
{
}

void FTsuInspectorClient::Start(int Port)
{
	Server.AddProtocol("protocol", 5 * 1024, this);
	ServerPort = Port;
	Server.Start(Port);
}

void FTsuInspectorClient::Stop()
{
	Server.Stop();

	for (auto It = Channels.CreateIterator(); It; ++It)
	{
		delete It->Value;
	}
	Channels.Empty();
}

void FTsuInspectorClient::RegisterContext(v8::Local<v8::Context> Context)
{
	const uint8_t ContextName[] = "TSU";
	v8_inspector::StringView ContextNameView(ContextName, sizeof(ContextName) - 1);
	Inspector->contextCreated(v8_inspector::V8ContextInfo(Context, 1, ContextNameView));
}

void FTsuInspectorClient::UnregisterContext(v8::Local<v8::Context> Context)
{
	Inspector->contextDestroyed(Context);
}

void FTsuInspectorClient::OnHttp(FTsuWebSocketRequest& Request, FTsuWebSocketResponse& Response)
{
	if (Request.GetUri() == TEXT("/json") || Request.GetUri() == TEXT("/json/list"))
	{
		auto PortStr = FString::Printf(TEXT("%d"), ServerPort);
		auto ResStr = JsonList.Replace(TEXT("{PORT}"), *PortStr);
		Response.End(200, CONTENT_TYPE_JSON_UTF8, *ResStr);
	}
	else if (Request.GetUri() == TEXT("/json/version"))
	{
		Response.End(200, CONTENT_TYPE_JSON_UTF8, JsonVersion);
	}
	else
	{
		Response.End(404, CONTENT_TYPE_TEXT_UTF8, TEXT("Not Found"));
	}
}

void FTsuInspectorClient::OnOpen(FTsuWebSocketConnection* Conn)
{
	Channels.Add(Conn, new FTsuInspectorChannel(Conn, *Inspector));
}

void FTsuInspectorClient::OnClosed(FTsuWebSocketConnection* Conn)
{
	FTsuInspectorChannel* Channel = nullptr;
	if (Channels.RemoveAndCopyValue(Conn, Channel))
	{
		delete Channel;
	}
}

void FTsuInspectorClient::OnReceive(FTsuWebSocketConnection* Conn, const FString& Data)
{
	auto Channel = Channels.Find(Conn);
	if (Channel)
	{
		(*Channel)->dispatchMessage(Data);
	}
}

void FTsuInspectorClient::OnWriteable(FTsuWebSocketConnection* Conn)
{
}

bool FTsuInspectorClient::Tick(float /*DeltaTime*/)
{
	FMemory::TestMemory();
	Server.Run(0);
	FMemory::TestMemory();
	return true;
}

void FTsuInspectorClient::runMessageLoopOnPause(int /*ContextGroupId*/)
{
	if (bIsPaused)
		return;

	bIsPaused = true;

	while (bIsPaused)
		Tick();
}

void FTsuInspectorClient::quitMessageLoopOnPause()
{
	bIsPaused = false;
}
