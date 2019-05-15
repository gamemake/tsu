#pragma once

class FTsuWebSocketServer;

#define CONTENT_TYPE TEXT("Content-Type")
#define CONTENT_LENGTH TEXT("Content-Length")

#define CONTENT_TYPE_JSON_UTF8 TEXT("Content-Type: application/json; charset=UTF-8")
#define CONTENT_TYPE_TEXT_UTF8 TEXT("Content-Type: application/text; charset=UTF-8")

class TSUWEBSOCKET_API FTsuWebSocketResponse
{
	friend class FTsuWebSocketServer;
public:
	FTsuWebSocketResponse();
	~FTsuWebSocketResponse();

	void SetHeader(const TCHAR* Name, const TCHAR* Value);
	void SetHeader(const FString& Name, const FString& Value);
	void Write(const FString& Data);
	void End(int StatusCode, const TCHAR* _ContentType, const FString& Data);

protected:
	int Send(lws* Wsi);

private:
	int StatusCode;
	FString ContentType;
	TMap<const FString, const FString> Headers;
	FString BodyText;
	FTCHARToUTF8* BodyData;
};
