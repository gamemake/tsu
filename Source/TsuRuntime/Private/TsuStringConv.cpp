#include "TsuStringConv.h"

#include "TsuIsolate.h"

v8::Local<v8::String> FTsuStringConv::To(const FString& String)
{
	return To(*String, String.Len());
}

v8::Local<v8::String> FTsuStringConv::To(const TCHAR* String, int32 Length)
{
	return v8::String::NewFromUtf8(
		FTsuIsolate::Get(),
		TCHAR_TO_UTF8(String),
		v8::NewStringType::kNormal
	).ToLocalChecked();
}

FString FTsuStringConv::From(v8::Local<v8::String> String)
{
	v8::Isolate* Isolate = FTsuIsolate::Get();
	v8::String::Utf8Value StringValue{Isolate, String};
	return FString(UTF8_TO_TCHAR(*StringValue));
}

v8::Local<v8::String> operator""_v8(const char16_t* StringPtr, size_t StringLen)
{
	return v8::String::NewFromTwoByte(
		FTsuIsolate::Get(),
		reinterpret_cast<const uint16_t*>(StringPtr),
		v8::NewStringType::kNormal,
		(int)StringLen
	).ToLocalChecked();
}
