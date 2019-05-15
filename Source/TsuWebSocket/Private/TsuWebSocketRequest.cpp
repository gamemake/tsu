#include "CoreMinimal.h"
#include "libWebSocketsWarpper.h"

#include "TsuWebSocketRequest.h"
#include "LwsUtils.h"

FTsuWebSocketRequest::FTsuWebSocketRequest(const TCHAR* Method, const TCHAR* Uri)
	: Method(Method), Uri(Uri)
{
}

FTsuWebSocketRequest::~FTsuWebSocketRequest()
{

}

const FString& FTsuWebSocketRequest::GetMethod()
{
	return Method;
}

const FString& FTsuWebSocketRequest::GetUri()
{
	return Uri;
}

const FString& FTsuWebSocketRequest::GetQueryString()
{
	return QueryString;
}

const TMap<const FString, const FString> FTsuWebSocketRequest::GetHeaders()
{
	return Headers;
}

const TCHAR* FTsuWebSocketRequest::GetHeader(const TCHAR* Name)
{
	auto Value = Headers.Find(Name);
	return Value ? **Value : TEXT("");
}

int FTsuWebSocketRequest::GetContentLength()
{
	return ContentLength;
}

const FString& FTsuWebSocketRequest::GetBody()
{
	return Body;
}

bool FTsuWebSocketRequest::ParseForm()
{
	TArray<FString> Fields;
	if (!Body.ParseIntoArray(Fields, TEXT("\r\n"), false)) return false;
	Form.Empty();
	for (auto It = 0; It < Fields.Num(); It++)
	{
		auto Field = Fields[It];
		int32 Pos;
		if (!Field.FindChar(TEXT('='), Pos)) return false;
		auto Name = Field.Mid(0, Pos);
		auto Value = Field.Mid(Pos + 1);
		Form.Add(Name, Value);
	}
	return true;
}

const TMap<const FString, const FString> FTsuWebSocketRequest::GetFields()
{
	return Form;
}

const TCHAR* FTsuWebSocketRequest::GetField(const TCHAR* Name, const TCHAR* Default)
{
	auto Value = Form.Find(Name);
	return Value ? **Value : Default;
}

bool FTsuWebSocketRequest::ParseHeader(lws* Wsi)
{
	QueryString = LwsGetHeader(Wsi, WSI_TOKEN_HTTP_URI_ARGS);

	char HeaderAnsi[1000];
	for (auto It = 0; It < WSI_TOKEN_COUNT; It++)
	{
		auto Length = sizeof HeaderAnsi;
		Length = lws_hdr_copy(Wsi, HeaderAnsi, Length, (lws_token_indexes)It);
		if (Length > 0)
		{
			auto Name = (const char*)lws_token_to_string((lws_token_indexes)It);
			Headers.Add(ANSI_TO_TCHAR(Name), ANSI_TO_TCHAR(HeaderAnsi));
		}
	}

	ContentLength = LwsGetHeader(Wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH, 0);
	if (ContentLength < 0) return false;

	return true;
}
