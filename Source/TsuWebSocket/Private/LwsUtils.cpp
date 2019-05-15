#include "TsuWebSocketPrivatePCH.h"

#include "LwsUtils.h"

const TCHAR* LwsStatusName(int Code)
{
	switch (Code)
	{
	case 100: return TEXT("Continue");
	case 101: return TEXT("Switching Protocols");
	case 102: return TEXT("Processing");                 // RFC 2518"); obsoleted by RFC 4918
	case 103: return TEXT("Early Hints");
	case 200: return TEXT("OK");
	case 201: return TEXT("Created");
	case 202: return TEXT("Accepted");
	case 203: return TEXT("Non-Authoritative Information");
	case 204: return TEXT("No Content");
	case 205: return TEXT("Reset Content");
	case 206: return TEXT("Partial Content");
	case 207: return TEXT("Multi-Status");               // RFC 4918
	case 208: return TEXT("Already Reported");
	case 226: return TEXT("IM Used");
	case 300: return TEXT("Multiple Choices");           // RFC 7231
	case 301: return TEXT("Moved Permanently");
	case 302: return TEXT("Found");
	case 303: return TEXT("See Other");
	case 304: return TEXT("Not Modified");
	case 305: return TEXT("Use Proxy");
	case 307: return TEXT("Temporary Redirect");
	case 308: return TEXT("Permanent Redirect");         // RFC 7238
	case 400: return TEXT("Bad Request");
	case 401: return TEXT("Unauthorized");
	case 402: return TEXT("Payment Required");
	case 403: return TEXT("Forbidden");
	case 404: return TEXT("Not Found");
	case 405: return TEXT("Method Not Allowed");
	case 406: return TEXT("Not Acceptable");
	case 407: return TEXT("Proxy Authentication Required");
	case 408: return TEXT("Request Timeout");
	case 409: return TEXT("Conflict");
	case 410: return TEXT("Gone");
	case 411: return TEXT("Length Required");
	case 412: return TEXT("Precondition Failed");
	case 413: return TEXT("Payload Too Large");
	case 414: return TEXT("URI Too Long");
	case 415: return TEXT("Unsupported Media Type");
	case 416: return TEXT("Range Not Satisfiable");
	case 417: return TEXT("Expectation Failed");
	case 418: return TEXT("I\'m a Teapot");              // RFC 7168
	case 421: return TEXT("Misdirected Request");
	case 422: return TEXT("Unprocessable Entity");       // RFC 4918
	case 423: return TEXT("Locked");                     // RFC 4918
	case 424: return TEXT("Failed Dependency");          // RFC 4918
	case 425: return TEXT("Unordered Collection");       // RFC 4918
	case 426: return TEXT("Upgrade Required");           // RFC 2817
	case 428: return TEXT("Precondition Required");      // RFC 6585
	case 429: return TEXT("Too Many Requests");          // RFC 6585
	case 431: return TEXT("Request Header Fields Too Large"); // RFC 6585
	case 451: return TEXT("Unavailable For Legal Reasons");
	case 500: return TEXT("Internal Server Error");
	case 501: return TEXT("Not Implemented");
	case 502: return TEXT("Bad Gateway");
	case 503: return TEXT("Service Unavailable");
	case 504: return TEXT("Gateway Timeout");
	case 505: return TEXT("HTTP Version Not Supported");
	case 506: return TEXT("Variant Also Negotiates");    // RFC 2295
	case 507: return TEXT("Insufficient Storage");       // RFC 4918
	case 508: return TEXT("Loop Detected");
	case 509: return TEXT("Bandwidth Limit Exceeded");
	case 510: return TEXT("Not Extended");               // RFC 2774
	case 511: return TEXT("Network Authentication Required"); // RFC 6585
	};
	return TEXT("Unkown");
}

const FString LwsGetHeader(lws* Wsi, lws_token_indexes H)
{
	int len = lws_hdr_total_length(Wsi, H) + 1;
	const int MaxHeaderLength = 4096;

	if (len == 1 || len >= MaxHeaderLength)
		return TEXT("");

	char buf[MaxHeaderLength+1];
	lws_hdr_copy(Wsi, buf, len, H);
	return ANSI_TO_TCHAR(buf);
}

int LwsGetHeader(lws * Wsi, lws_token_indexes H, int Default)
{
	int len = lws_hdr_total_length(Wsi, H) + 1;
	const int MaxHeaderLength = 30;

	if (len == 1 || len >= MaxHeaderLength)
		return Default;

	char buf[MaxHeaderLength];
	lws_hdr_copy(Wsi, buf, len, H);
	return atoi(buf);
}

int LwsWrite(lws* Wsi, const TCHAR* Data, lws_write_protocol H)
{
	FTCHARToUTF8 Utf8(Data);
	auto Buf = new unsigned char[LWS_PRE + Utf8.Length() + LWS_PRE];
	FMemory::Memzero(Buf, LWS_PRE + Utf8.Length() + LWS_PRE);
	FMemory::Memcpy(Buf + LWS_PRE, Utf8.Get(), Utf8.Length());
	auto ret = lws_write(Wsi, (unsigned char*)Buf + LWS_PRE, Utf8.Length(), H);
	delete[] Buf;
	if (ret>=0 && H != LWS_WRITE_HTTP_FINAL)
		lws_callback_on_writable(Wsi);
	return ret;
}
