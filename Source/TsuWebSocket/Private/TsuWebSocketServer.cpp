#include "CoreMinimal.h"

#include "libWebSocketsWarpper.h"
#include "TsuWebSocketRequest.h"
#include "TsuWebSocketResponse.h"
#include "TsuWebSocketConnection.h"
#include "TsuWebSocketServer.h"
#include "TsuWebSocketLog.h"
#include "LwsUtils.h"

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
		WebSocketProtocols[i].per_session_data_size = sizeof(SessionData);
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
		WebSocketProtocols = nullptr;
		return false;
	}

	return true;
}

void FTsuWebSocketServer::Stop()
{
	if (WebSocketContext)
	{
		lws_context_destroy(WebSocketContext);
		WebSocketContext = nullptr;
	}

	if (WebSocketProtocols)
	{
		delete[] WebSocketProtocols;
		WebSocketProtocols = nullptr;
	}
}

int FTsuWebSocketServer::ProtocolCallback(lws* Wsi, enum lws_callback_reasons Reason, void* User, void* In, size_t Len)
{
	auto protocol = lws_get_protocol(Wsi);
	auto Callback = protocol ? (ICallback*)protocol->user : nullptr;
	auto Data = (SessionData*)User;

	/*
    UE_LOG(LogTsuWebSocket, Log, TEXT("OnCallback: %p %2d %p %p %4d %p %p %p"),
           Wsi, (int)Reason, User, In, (int)Len,
           Data?Data->Request:nullptr,
           Data?Data->Response:nullptr,
           Data?Data->Conn:nullptr
           );
	*/

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
				UE_LOG(LogTsuWebSocket, Error, TEXT("Invalid Http Method"));
				break;
			}

			Data->Request = new FTsuWebSocketRequest(Method, *LwsToString(In, Len));
			if (!Data->Request->ParseHeader(Wsi)) 
			{
                delete Data->Request;
				Data->Request = nullptr;
				UE_LOG(LogTsuWebSocket, Error, TEXT("Failed to ParseHeader"));
				break;
			}

			if (Data->Request->ContentLength == 0)
			{
				Data->Response = new FTsuWebSocketResponse;
				UE_LOG(LogTsuWebSocket, Log, TEXT("BODY_COMPLETION %p %s"), Data->Request, *Data->Request->Method);
				Callback->OnHttp(*Data->Request, *Data->Response);
				if (lws_callback_on_writable(Wsi) < 0)
				{
					UE_LOG(LogTsuWebSocket, Error, TEXT("Failed to lws_callback_on_writable 1"));
					delete Data->Request;
					Data->Request = nullptr;
					delete Data->Response;
					Data->Response = nullptr;
					return 1;
				}
			}
			return 0;
		}
		break;
	case LWS_CALLBACK_HTTP_BODY:
		if (Data && Data->Request)
		{
			auto Str = LwsToString(In, Len);
			UE_LOG(LogTsuWebSocket, Log, TEXT("BODY %p %d %s"), Data->Request, Str.Len(), *Str);
			Data->Request->Body += Str;
			return 0;
		}
		break;
	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		if (Data && Data->Request != nullptr)
		{
			check(Data->Request->ContentLength > 0);

			Data->Response = new FTsuWebSocketResponse;
			UE_LOG(LogTsuWebSocket, Log, TEXT("BODY_COMPLETION %p %s %s"), Data->Request, *Data->Request->Method, *Data->Request->Body);
			Callback->OnHttp(*Data->Request, *(Data->Response));
			if (lws_callback_on_writable(Wsi) < 0)
			{
				UE_LOG(LogTsuWebSocket, Error, TEXT("Failed to lws_callback_on_writable 2"));
				break;
			}
			return 0;
		}
		break;
	case LWS_CALLBACK_HTTP_WRITEABLE:
		if (Data && Data->Response)
		{
			auto Ret = Data->Response->Send(Wsi);
			if (Ret >= 0) return 0;
		}
		break;
	case LWS_CALLBACK_CLOSED_HTTP:
		if (Data) {
			if (Data->Request)
			{
                delete Data->Request;
				Data->Request = nullptr;
			}
			if (Data->Response)
			{
                delete Data->Response;
				Data->Response = nullptr;
			}
			return 0;
		}
		break;
	case LWS_CALLBACK_ESTABLISHED:
		if (Data)
		{
			Data->Conn = new FTsuWebSocketConnection(Wsi);
			UE_LOG(LogTsuWebSocket, Log, TEXT("OPEN %p"), Data->Conn);
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
                delete Data->Request;
				Data->Request = nullptr;
			}
			if (Data->Response)
			{
                delete Data->Response;
				Data->Response = nullptr;
			}
			UE_LOG(LogTsuWebSocket, Log, TEXT("CLOSE %p"), Data->Conn);
			if (Data->Conn)
			{
				Callback->OnClosed(Data->Conn);
				delete Data->Conn;
				Data->Conn = nullptr;
			}
		}
		break;
	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (Data && Data->Conn)
		{
			if (Data->Conn->SendQueue.Num() > 0)
			{
				auto Str = Data->Conn->SendQueue[0];
				Data->Conn->SendQueue.RemoveAt(0);
				UE_LOG(LogTsuWebSocket, Log, TEXT("SEND %p %d %s"), Data->Conn, Str.Len(), *Str);
				FTCHARToUTF8 Utf8(*Str);
				lws_write(Wsi, (unsigned char*)Utf8.Get(), Utf8.Length(), LWS_WRITE_TEXT);
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
			auto Str = LwsToString(In, Len);
			UE_LOG(LogTsuWebSocket, Log, TEXT("RECEIVE %p %s"), Data->Conn, *Str);
			Callback->OnReceive(Data->Conn, Str);
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
