#include "TsuWebSocketPrivatePCH.h"

#include "TsuWebSocketConnection.h"

FTsuWebSocketConnection::FTsuWebSocketConnection(lws* Wsi) :
	Wsi(Wsi),
	IsWriteable(false)
{
}

FTsuWebSocketConnection::~FTsuWebSocketConnection()
{
	Wsi = nullptr;
}

void FTsuWebSocketConnection::SendMessage(const FString& Data)
{
	if (IsWriteable)
	{
		FTCHARToUTF8 utf8(*Data);
		lws_write(Wsi, (unsigned char*)utf8.Get(), utf8.Length(), LWS_WRITE_TEXT);
		IsWriteable = false;
		lws_callback_on_writable(Wsi);
	}
	else
	{
		SendQueue.Push(Data);
	}
}
