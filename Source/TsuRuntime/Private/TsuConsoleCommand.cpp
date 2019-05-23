#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "TsuRuntimeLog.h"
#include "TsuIsolate.h"
#include "TsuContext.h"
#include "TsuCodeGenerator.h"

static const char* ToCString(const v8::String::Utf8Value& value);
static bool ExecuteString(v8::Isolate* isolate, v8::Local<v8::String> source, v8::Local<v8::Value> name, bool print_result, bool report_exceptions);
static void ReportException(v8::Isolate* isolate, v8::TryCatch* handler);

static void JSRun(const TArray< FString >& Args)
{
	FString Line;
	for (auto It = Args.CreateConstIterator(); It; ++It)
	{
		Line += TEXT(" ");
		Line += *It;
	}

	v8::HandleScope handle_scope(FTsuIsolate::Get());

	v8::Local<v8::String> name(v8::String::NewFromUtf8(FTsuIsolate::Get(), "(shell)", v8::NewStringType::kNormal).ToLocalChecked());

	ExecuteString(FTsuIsolate::Get(),
		v8::String::NewFromUtf8(FTsuIsolate::Get(), TCHAR_TO_UTF8(*Line), v8::NewStringType::kNormal).ToLocalChecked(),
		name,
		true,
		true);

	while (v8::platform::PumpMessageLoop(FTsuIsolate::GetPlatform(), FTsuIsolate::Get()))
	{
		continue;
	}
}

static void TsuCodeGenerator(const TArray<FString>& Args)
{
	FTsuCodeGenerator::ExportAll();
}

static FAutoConsoleCommand CVarJSRun(
	TEXT("JSRun"),
	TEXT("Execute javascript string"),
	FConsoleCommandWithArgsDelegate::CreateStatic(JSRun),
	ECVF_Cheat);

static FAutoConsoleCommand CVarTsuCodeGenerate(
	TEXT("TsuCodeGenerate"),
	TEXT("Generat code"),
	FConsoleCommandWithArgsDelegate::CreateStatic(TsuCodeGenerator),
	ECVF_Cheat);

// Extracts a C string from a V8 Utf8Value.
const char* ToCString(const v8::String::Utf8Value& value)
{
	return *value ? *value : "<string conversion failed>";
}

// Executes a string within the current v8 context.
bool ExecuteString(v8::Isolate* isolate, v8::Local<v8::String> source, v8::Local<v8::Value> name, bool print_result, bool report_exceptions)
{
	v8::HandleScope handle_scope(isolate);
	v8::TryCatch try_catch(isolate);
	v8::ScriptOrigin origin(name);
	v8::Local<v8::Context> context(isolate->GetCurrentContext());
	v8::Local<v8::Script> script;
	if (!v8::Script::Compile(context, source, &origin).ToLocal(&script))
	{
		// Print errors that happened during compilation.
		if (report_exceptions)
			ReportException(isolate, &try_catch);
		return false;
	}
	else
	{
		v8::Local<v8::Value> result;
		if (!script->Run(context).ToLocal(&result))
		{
			check(try_catch.HasCaught());
			// Print errors that happened during execution.
			if (report_exceptions)
				ReportException(isolate, &try_catch);
			return false;
		}
		else
		{
			check(!try_catch.HasCaught());
			if (print_result && !result->IsUndefined())
			{
				// If all went well and the result wasn't undefined then print
				// the returned value.
				v8::String::Utf8Value str(isolate, result);
				const char* cstr = ToCString(str);
				UE_LOG(LogTsuRuntime, Log, TEXT("V8: %s"), UTF8_TO_TCHAR(cstr));
			}
			return true;
		}
	}
}

void ReportException(v8::Isolate* isolate, v8::TryCatch* try_catch)
{
	v8::HandleScope handle_scope(isolate);
	v8::String::Utf8Value exception(isolate, try_catch->Exception());
	const char* exception_string = ToCString(exception);
	v8::Local<v8::Message> message = try_catch->Message();
	if (message.IsEmpty())
	{
		// V8 didn't provide any extra information about this error; just
		// print the exception.
		UE_LOG(LogTsuRuntime, Error, TEXT("V8: %s"), UTF8_TO_TCHAR(exception_string));
	}
	else
	{
		// Print (filename):(line number): (message).
		v8::String::Utf8Value filename(isolate, message->GetScriptOrigin().ResourceName());
		v8::Local<v8::Context> context(isolate->GetCurrentContext());
		const char* filename_string = ToCString(filename);
		int linenum = message->GetLineNumber(context).FromJust();
		UE_LOG(LogTsuRuntime, Error, TEXT("%s:%i: %s"), UTF8_TO_TCHAR(filename_string), linenum, UTF8_TO_TCHAR(exception_string));
		// Print line of source code.
		v8::String::Utf8Value sourceline(
			isolate, message->GetSourceLine(context).ToLocalChecked());
		const char* sourceline_string = ToCString(sourceline);
		UE_LOG(LogTsuRuntime, Error, TEXT("%s"), UTF8_TO_TCHAR(sourceline_string))
			// Print wavy underline (GetUnderline is deprecated).
			int start = message->GetStartColumn(context).FromJust();
		FString Line;
		for (int i = 0; i < start; i++)
		{
			Line += TEXT(" ");
		}
		int end = message->GetEndColumn(context).FromJust();
		for (int i = start; i < end; i++)
		{
			Line += TEXT("^");
		}
		UE_LOG(LogTsuRuntime, Error, TEXT("%s"), *Line)
		v8::Local<v8::Value> stack_trace_string;
		if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) &&
			stack_trace_string->IsString() &&
			v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0)
		{
			v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
			UE_LOG(LogTsuRuntime, Error, TEXT("%s"), UTF8_TO_TCHAR(ToCString(stack_trace)));
		}
	}
}
