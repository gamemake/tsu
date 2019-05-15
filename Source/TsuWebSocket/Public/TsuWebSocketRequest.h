#pragma once

class FTsuWebSocketServer;

class TSUWEBSOCKET_API FTsuWebSocketRequest
{
	friend class FTsuWebSocketServer;
public:
	FTsuWebSocketRequest(const TCHAR* Method, const TCHAR* Uri);
	~FTsuWebSocketRequest();

	const FString& GetMethod();
	const FString& GetUri();
	const FString& GetQueryString();
	const TMap<const FString, const FString> GetHeaders();
	const TCHAR* GetHeader(const TCHAR* Name);
	int GetContentLength();
	const FString& GetBody();
	bool ParseForm();
	const TMap<const FString, const FString> GetFields();
	const TCHAR* GetField(const TCHAR* Name, const TCHAR* Default = TEXT(""));

protected:
	bool ParseHeader(lws* Wsi);

private:
	FString Method;
	FString Uri;
	FString QueryString;
	TMap<const FString, const FString> Headers;
	int ContentLength;
	FString Body;
	TMap<const FString, const FString> Form;
};
