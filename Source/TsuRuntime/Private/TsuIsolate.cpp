#include "TsuIsolate.h"

#include "TsuHeapStats.h"
#include "TsuRuntimeLog.h"

v8::Isolate* FTsuIsolate::Isolate = nullptr;
std::unique_ptr<v8::Platform> FTsuIsolate::Platform;

static void v8_error_handler(const char* Location, const char* Message)
{
	UE_LOG(LogTsuRuntime,
		Fatal,
		TEXT("%s: %s"),
		UTF8_TO_TCHAR(Location),
		UTF8_TO_TCHAR(Message));
}

class FTsuV8Allocator
	: public v8::ArrayBuffer::Allocator
{
public:
	~FTsuV8Allocator()
	{
	}

    void* Allocate(size_t length) override
	{
		auto Ret = FMemory::Malloc(length, sizeof(void*));
		if (!Ret) return Ret;
		FMemory::Memzero(Ret, length);
		return Ret;
	}

    void* AllocateUninitialized(size_t length) override
	{
		return FMemory::Malloc(length, sizeof(void*));
	}

    void Free(void* data, size_t length) override
	{
		return FMemory::Free(data);
	}
};

static FTsuV8Allocator TsuV8Allocator;
static v8::Isolate::CreateParams TsuV8CreateParams;
static v8::Local<v8::Context> TsuV8Context;

void FTsuIsolate::Initialize()
{
	v8::V8::InitializeICUDefaultLocation("TSU");
	v8::V8::InitializeExternalStartupData("TSU");
	Platform = v8::platform::NewDefaultPlatform();
	v8::V8::InitializePlatform(Platform.get());
	v8::V8::Initialize();
	TsuV8CreateParams.array_buffer_allocator = &TsuV8Allocator;
	Isolate = v8::Isolate::New(TsuV8CreateParams);
	Isolate->SetFatalErrorHandler(v8_error_handler);
}

void FTsuIsolate::Uninitialize()
{
	Isolate->Dispose();
	v8::V8::Dispose();
	v8::V8::ShutdownPlatform();
}
