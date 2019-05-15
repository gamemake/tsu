#include "TsuWebSocketPrivatePCH.h"

#include "TsuWebSocketResponse.h"
#include "LwsUtils.h"

FTsuWebSocketResponse::FTsuWebSocketResponse()
	: StatusCode(404),
	ContentType(TEXT("application-text; charset=UTF-8")),
	BodyData(nullptr)
{
}

FTsuWebSocketResponse::~FTsuWebSocketResponse()
{
	if (BodyData)
	{
		delete BodyData;
		BodyData = nullptr;
	}
}

void FTsuWebSocketResponse::SetHeader(const TCHAR* Name, const TCHAR* Value)
{
	Headers.Add(Name, Value);
}

void FTsuWebSocketResponse::SetHeader(const FString& Name, const FString& Value)
{
	Headers.Add(Name, Value);
}

void FTsuWebSocketResponse::Write(const FString& Data)
{
	BodyText += Data;
}

void FTsuWebSocketResponse::End(int Code, const TCHAR* _ContentType, const FString& Data)
{
	StatusCode = Code;
	Headers.Add(CONTENT_TYPE, _ContentType);
	BodyText += Data;
}

int FTsuWebSocketResponse::Send(lws* Wsi)
{
	if (!BodyData)
	{
		BodyData = new FTCHARToUTF8(*BodyText);

		auto Header = FString::Printf(TEXT("HTTP/1.0 %d %s\r\n"), StatusCode, LwsStatusName(StatusCode));
		auto _ContentType = Headers.Find(CONTENT_TYPE);
		Header += FString::Printf(TEXT("%s: %s\r\n"), CONTENT_TYPE, **_ContentType);
		Header += FString::Printf(TEXT("%s: %d\r\n"), CONTENT_LENGTH, BodyData->Length());
		for (auto It = Headers.CreateConstIterator(); It; ++It)
		{
			if (It->Key != CONTENT_TYPE)
			{
				Header += FString::Printf(TEXT("%s: %s\r\n"), *It->Key, *It->Value);
			}
		}
		Header += TEXT("\r\n");

		return LwsWrite(Wsi, *Header, LWS_WRITE_HTTP_HEADERS);
	}

	return LwsWrite(Wsi, *BodyText, LWS_WRITE_HTTP_FINAL);
}
