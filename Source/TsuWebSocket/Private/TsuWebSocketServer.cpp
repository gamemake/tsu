#include "CoreMinimal.h"

#include "libWebSocketsWarpper.h"
#include "TsuWebSocketRequest.h"
#include "TsuWebSocketResponse.h"
#include "TsuWebSocketConnection.h"
#include "TsuWebSocketServer.h"
#include "TsuWebSocketLog.h"

struct SessionData
{
	FTsuWebSocketConnection* Conn;
	FTsuWebSocketRequest* Request;
	FTsuWebSocketResponse* Response;
};

FTsuWebSocketServer::FTsuWebSocketServer()
	: WebSocketProtocols(nullptr),
	WebSocketContext(nullptr)
{
}

FTsuWebSocketServer::~FTsuWebSocketServer()
{
	check(!WebSocketProtocols && !WebSocketContext);
}

void FTsuWebSocketServer::AddProtocol(const char* Name, size_t BufferSize, ICallback* Callback)
{
	Protocols.Add({ Name, BufferSize, Callback });
}

bool FTsuWebSocketServer::Start(int Port)
{
	WebSocketProtocols = new lws_protocols[Protocols.Num() + 1];
	FMemory::Memzero(WebSocketProtocols, sizeof(lws_protocols) * (Protocols.Num() + 1));
	for (auto i = 0; i < Protocols.Num(); i++)
	{
		WebSocketProtocols[i].name = Protocols[i].Name;
		WebSocketProtocols[i].user = Protocols[i].Callback;
		WebSocketProtocols[i].rx_buffer_size = Protocols[i].BufferSize;
		WebSocketProtocols[i].per_session_data_size = sizeof(FTsuWebSocketConnection*);
		WebSocketProtocols[i].callback = FTsuWebSocketServer::ProtocolCallback;
	}

	lws_context_creation_info Info;
	FMemory::Memzero(&Info, sizeof Info);
	Info.port = Port;
	Info.protocols = WebSocketProtocols;
	Info.gid = -1;
	Info.uid = -1;
	Info.options = LWS_SERVER_OPTION_DISABLE_IPV6;
	Info.user = this;

	WebSocketContext = lws_create_context(&Info);
	if (WebSocketContext == nullptr)
	{
		delete[] WebSocketProtocols;
		WebSocketProtocols = NULL;
		return false;
	}

	return true;
}

void FTsuWebSocketServer::Stop()
{
	if (WebSocketContext)
	{
		lws_context_destroy(WebSocketContext);
		WebSocketContext = NULL;
	}

	if (WebSocketProtocols)
	{
		delete[] WebSocketProtocols;
		WebSocketProtocols = NULL;
	}
}

int FTsuWebSocketServer::ProtocolCallback(lws* Wsi, enum lws_callback_reasons Reason, void* User, void* In, size_t Len)
{
	auto protocol = lws_get_protocol(Wsi);
	auto Callback = protocol ? (ICallback*)protocol->user : nullptr;
	auto Data = (SessionData*)User;

	if (Data)
	{
		UE_LOG(LogTsuWebSocket, Log, TEXT("OnCallback: %p %d %p %p %d %p %p %p"), 
			Wsi, (int)Reason, User, In, (int)Len,
			Data->Request, Data->Response, Data->Conn);
	}
	else
	{
		UE_LOG(LogTsuWebSocket, Log, TEXT("OnCallback: %p %d %p %p %d"),
			Wsi, (int)Reason, User, In, (int)Len);
	}

	switch (Reason)
	{
	case LWS_CALLBACK_HTTP:
		if (Data)
		{
			Data->Request = nullptr;
			Data->Response = nullptr;

			const TCHAR* Method = nullptr;
			if (lws_hdr_total_length(Wsi, WSI_TOKEN_GET_URI) > 0)
			{
				Method = TEXT("GET");
			}
			else if (lws_hdr_total_length(Wsi, WSI_TOKEN_POST_URI) > 0)
			{
				Method = TEXT("POST");
			}
			else
			{
				return 1;
			}

			Data->Request = new FTsuWebSocketRequest(Method, UTF8_TO_TCHAR((const char*)In));
			if (!Data->Request->ParseHeader(Wsi)) 
			{
				// delete Data->Request;
				Data->Request = nullptr;
				return 1;
			}

			if (Data->Request->ContentLength == 0)
			{
				Data->Response = new FTsuWebSocketResponse;
				Callback->OnHttp(*Data->Request, *Data->Response);
				lws_callback_on_writable(Wsi);
			}
			return 0;
		}
		break;
	case LWS_CALLBACK_HTTP_BODY:
		if (Data && Data->Request)
		{
			FUTF8ToTCHAR Utf8((const ANSICHAR*)In, Len);
			Data->Request->Body += Utf8.Get();
			return 0;
		}
		break;
	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		if (Data && Data->Request != nullptr)
		{
			check(Data->Request->ContentLength > 0);

			Data->Response = new FTsuWebSocketResponse;
			Callback->OnHttp(*Data->Request, *(Data->Response));
			lws_callback_on_writable(Wsi);
			return 0;
		}
		break;
	case LWS_CALLBACK_HTTP_WRITEABLE:
		if (Data && Data->Response)
		{
			return Data->Response->Send(Wsi);
		}
		break;
	case LWS_CALLBACK_CLOSED_HTTP:
		if (Data) {
			if (Data->Request)
			{
				// delete Data->Request;
				Data->Request = nullptr;
			}
			if (Data->Response)
			{
				// delete Data->Response;
				Data->Response = nullptr;
			}
			return 0;
		}
		break;
	case LWS_CALLBACK_ESTABLISHED:
		if (Data)
		{
			Data->Conn = new FTsuWebSocketConnection(Wsi);
			Callback->OnOpen(Data->Conn);
			lws_callback_on_writable(Wsi);
			return 0;
		}
		break;
	case LWS_CALLBACK_CLOSED:
		if (Data)
		{
			if (Data->Request)
			{
				// delete Data->Request;
				Data->Request = nullptr;
			}
			if (Data->Response)
			{
				// delete Data->Response;
				Data->Response = nullptr;
			}
			if (Data->Conn)
			{
				Callback->OnClosed(Data->Conn);
				// delete Data->Conn;
				Data->Conn = nullptr;
			}
		}
		break;
	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (Data && Data->Conn)
		{
			if (Data->Conn->SendQueue.Num() > 0)
			{
				FTCHARToUTF8 utf8(*Data->Conn->SendQueue[0]);
				lws_write(Wsi, (unsigned char*)utf8.Get(), utf8.Length(), LWS_WRITE_TEXT);
				lws_callback_on_writable(Wsi);
			}
			else
			{
				Data->Conn->IsWriteable = true;
				Callback->OnWriteable(Data->Conn);
			}
			return 0;
		}
		break;
	case LWS_CALLBACK_RECEIVE:
		if (Data && Data->Conn)
		{
			Callback->OnReceive(Data->Conn, UTF8_TO_TCHAR((const char*)In));
			return 0;
		}
		break;
	}

	return lws_callback_http_dummy(Wsi, Reason, User, In, Len);
}

bool FTsuWebSocketServer::Run(int Timeout)
{
	return lws_service(WebSocketContext, Timeout) >= 0;
}
