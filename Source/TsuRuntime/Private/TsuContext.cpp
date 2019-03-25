#include "TsuContext.h"

#include "TsuDelegateEvent.h"
#include "TsuIsolate.h"
#include "TsuPaths.h"
#include "TsuReflection.h"
#include "TsuRuntimeLog.h"
#include "TsuStringConv.h"
#include "TsuTryCatch.h"
#include "TsuTypings.h"
#include "TsuUtilities.h"
#include "TsuWorldScope.h"

#include "Engine/Engine.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/TextProperty.h"

#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif // PLATFORM_WINDOWS

DEFINE_LOG_CATEGORY_STATIC(LogTsu, Log, All);

const FName FTsuContext::MetaWorldContext = TEXT("WorldContext");
const FName FTsuContext::NameEventExecute = GET_FUNCTION_NAME_CHECKED(UTsuDelegateEvent, Execute);

TOptional<FTsuContext> FTsuContext::Singleton;
v8::Isolate* FTsuContext::Isolate = nullptr;
v8::Global<v8::FunctionTemplate> FTsuContext::GlobalDelegateTemplate;
v8::Global<v8::FunctionTemplate> FTsuContext::GlobalMulticastDelegateTemplate;
v8::Global<v8::Function> FTsuContext::GlobalArrayHandlerConstructor;
v8::Global<v8::Function> FTsuContext::GlobalArrayConstructor;
v8::Global<v8::Function> FTsuContext::GlobalStructHandlerConstructor;

namespace TsuContext_Private
{

bool ValuesToString(const TArray<v8::Local<v8::Value>>& Values, FString& OutResult)
{
	v8::Isolate* Isolate = FTsuIsolate::Get();
	v8::Local<v8::Context> Context = Isolate->GetCurrentContext();

	int32 NumWritten = 0;
	for (const v8::Local<v8::Value>& Value : Values)
	{
		if (NumWritten++ > 0)
			OutResult += TEXT(' ');

		v8::Local<v8::String> String;
		if (Value->ToString(Context).ToLocal(&String))
			OutResult.Append(V8_TO_TCHAR(String));
	}

	return true;
}

void SetProperty(
	v8::Local<v8::Context> Context,
	v8::Local<v8::Object> Object,
	v8::Local<v8::String> Key,
	v8::Local<v8::Value> Value)
{
	verify(Object->Set(Context, Key, Value).ToChecked());
}

void SetMethod(
	v8::Local<v8::Context> Context,
	v8::Local<v8::Object> Object,
	v8::Local<v8::String> Key,
	v8::FunctionCallback Callback)
{
	v8::Local<v8::Function> Value = v8::Function::New(Context, Callback).ToLocalChecked();
	verify(Object->Set(Context, Key, Value).ToChecked());
}

TArray<v8::Local<v8::Value>> ExtractArgs(const v8::FunctionCallbackInfo<v8::Value>& Info, int32 InBegin = -1, int32 InEnd = -1)
{
	TArray<v8::Local<v8::Value>> Args;
	Args.Reserve(Info.Length());

	const int32 Begin = InBegin != -1 ? InBegin : 0;
	const int32 End = InEnd != -1 ? InEnd : Info.Length();

	for (int32 Index = Begin; Index < End; ++Index)
		Args.Add(Info[Index]);

	return Args;
}

template<typename T>
bool GetExternalValue(v8::Local<v8::Value> Value, T** OutData)
{
	if (!ensure(Value->IsExternal()))
		return false;

	void* Data = Value.As<v8::External>()->Value();
	*OutData = static_cast<T*>(Data);
	return true;
}

template<typename T1, typename T2 = void>
bool GetInternalFields(v8::Local<v8::Value> Value, T1** Field1, T2** Field2 = nullptr)
{
	if (!ensure(Value->IsObject()))
		return false;

	v8::Local<v8::Object> Object = Value.As<v8::Object>();
	if (!ensure(Object->InternalFieldCount() == 2))
		return false;

	if (Field1)
		*Field1 = static_cast<T1*>(Object->GetAlignedPointerFromInternalField(0));

	if (Field2)
		*Field2 = static_cast<T2*>(Object->GetAlignedPointerFromInternalField(1));

	return true;
}

bool EnsureV8(bool bCondition, const TCHAR* Expression)
{
	if (LIKELY(bCondition))
		return true;

	v8::Isolate* Isolate = FTsuIsolate::Get();
	v8::Local<v8::String> Message = TCHAR_TO_V8(Expression);
	v8::Local<v8::Value> Exception = v8::Exception::Error(Message);
	Isolate->ThrowException(Exception);

	return false;
}

#define ensureV8(InExpression) TsuContext_Private::EnsureV8(ensure(InExpression), TEXT(#InExpression))

void GetCallSite(FString& OutScriptName, FString& OutFunctionName, int32& OutLineNumber)
{
	const auto Options = v8::StackTrace::StackTraceOptions(
		v8::StackTrace::kScriptName |
		v8::StackTrace::kFunctionName |
		v8::StackTrace::kLineNumber);

	v8::Isolate* Isolate = FTsuIsolate::Get();
	v8::Local<v8::StackTrace> StackTrace = v8::StackTrace::CurrentStackTrace(Isolate, 1, Options);
	v8::Local<v8::StackFrame> StackFrame = StackTrace->GetFrame(Isolate, 0);

	const FString ScriptName = V8_TO_TCHAR(StackFrame->GetScriptName());
	OutScriptName = FPaths::GetBaseFilename(ScriptName);
	OutLineNumber = StackFrame->GetLineNumber();
	OutFunctionName = V8_TO_TCHAR(StackFrame->GetFunctionName());
}

int32 GetArrayLikeLength(v8::Local<v8::Context> Context, v8::Local<v8::Object> Value)
{
	v8::Local<v8::String> Key = u"length"_v8;
	if (!Value->Has(Context, Key).ToChecked())
		return 0;

	return Value->Get(Context, Key).ToLocalChecked().As<v8::Int32>()->Value();
}

} // namespace TsuContext_Private

FTsuContext::FTsuContext()
{
	Isolate = FTsuIsolate::Get();

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FTsuContext::OnPreGarbageCollect);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FTsuContext::OnPostGarbageCollect);

	v8::HandleScope HandleScope{Isolate};

	v8::Local<v8::Context> Context = v8::Context::New(Isolate);
	Context->Enter();
	GlobalContext.Reset(Isolate, Context);

	InitializeBuiltins();
	InitializeDelegates();
	InitializeRequire();
	InitializeArrayProxy();
	InitializeStructProxy();

	Inspector.Emplace(FTsuIsolate::GetPlatform(), Context);
}

FTsuContext::~FTsuContext()
{
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);

	for (auto& Struct : AliveStructs)
	{
		FStructKey& Key = Struct.Key;
		v8::Global<v8::Object>& Handle = Struct.Value;

		void* Object = Key.Key;
		UScriptStruct* Type = Key.Value;

		Type->DestroyStruct(Object);
		FMemory::Free(Object);

		Isolate->AdjustAmountOfExternalAllocatedMemory(-Type->GetStructureSize());
	}

	for (FTsuTimer& Timer : AliveTimers)
	{
		if (UWorld* World = Timer.World.Get())
		{
			FTimerManager& TimerManager = World->GetTimerManager();
			TimerManager.ClearTimer(Timer.Handle);
		}
	}
}

void FTsuContext::Create()
{
	Get();
}

void FTsuContext::Destroy()
{
	if (Singleton.IsSet())
		Singleton.Reset();
}

bool FTsuContext::Exists()
{
	return Singleton.IsSet();
}

FTsuContext& FTsuContext::Get()
{
	if (!Singleton.IsSet())
		Singleton.Emplace();

	return Singleton.GetValue();
}

v8::MaybeLocal<v8::Value> FTsuContext::EvalModule(const TCHAR* Code, const TCHAR* Path)
{
	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

	FString ModulePath = Path;
	if (!ensure(FPaths::MakePathRelativeTo(ModulePath, *FTsuPaths::ScriptsSourceDir())))
		return {};

	// clang-format off
	const FString WrappedCode = FString::Printf(
		TEXT("(function(__filename, __dirname) {")
		TEXT("const module = { exports: {} };")
		TEXT("const exports = module.exports;")
		TEXT("%s;\n")
		TEXT("return module.exports;\n")
		TEXT("})('%s', '%s')"),
		Code,
		*ModulePath,
		*FPaths::GetPath(ModulePath));
	// clang-format on

	v8::ScriptOrigin Origin{TCHAR_TO_V8(ModulePath), v8::Integer::New(Isolate, 0)};
	v8::MaybeLocal<v8::Script> MaybeScript = v8::Script::Compile(Context, TCHAR_TO_V8(WrappedCode), &Origin);

	v8::Local<v8::Script> Script;
	if (!ensure(MaybeScript.ToLocal(&Script)))
		return {};

	FTsuTryCatch Catcher{Isolate};
	return Script->Run(Context);
}

bool FTsuContext::BindModule(const TCHAR* Binding, const TCHAR* Code, const TCHAR* Path)
{
	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Global = Context->Global();

	v8::Local<v8::Value> Module = EvalModule(Code, Path).ToLocalChecked();

	return Global->Set(Context, TCHAR_TO_V8(Binding), Module).ToChecked();
}

TWeakPtr<FTsuModule> FTsuContext::ClaimModule(const TCHAR* Binding, const TCHAR* Code, const TCHAR* Path)
{
	v8::HandleScope HandleScope{Isolate};

	if (!ensure(BindModule(Binding, Code, Path)))
		return {};

	return LoadedModules.Add(Binding, MakeShared<FTsuModule>(Binding));
}

void FTsuContext::UnloadModule(const TCHAR* Binding)
{
	v8::HandleScope HandleScope{Isolate};

	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Global = Context->Global();

	v8::Local<v8::Primitive> Module = v8::Undefined(Isolate);
	Global->Set(Context, TCHAR_TO_V8(Binding), Module);

	LoadedModules.Remove(Binding);
}

v8::MaybeLocal<v8::Function> FTsuContext::GetExportedFunction(const TCHAR* Binding, const TCHAR* FunctionName)
{
	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Global = Context->Global();

	v8::Local<v8::Object> Exports = Global->Get(Context, TCHAR_TO_V8(Binding)).ToLocalChecked().As<v8::Object>();
	v8::Local<v8::Value> Export = Exports->Get(Context, TCHAR_TO_V8(FunctionName)).ToLocalChecked();
	if (!ensure(Export->IsFunction()))
		return {};

	return Export.As<v8::Function>();
}

bool FTsuContext::PushWorldContext(UObject* WorldContext)
{
	check(WorldContext != nullptr);

	if (UWorld* World = WorldContext->GetWorld())
	{
		PushWorldContext(ReferenceClassObject(World));
		return true;
	}

	return false;
}

bool FTsuContext::PushWorldContext(v8::Local<v8::Value> WorldContext)
{
	check(!WorldContext->IsUndefined() && !WorldContext->IsNull());
	WorldContexts.Emplace(FTsuIsolate::Get(), WorldContext);
	return true;
}

void FTsuContext::PopWorldContext()
{
	WorldContexts.Pop();
}

v8::Local<v8::Value> FTsuContext::GetWorldContext()
{
	if (WorldContexts.Num() > 0)
		return WorldContexts[0].Get(Isolate);

	UWorld* World = GWorld.GetReference();

#if WITH_EDITOR
	if (GEditor && GEditor->PlayWorld)
		World = GEditor->PlayWorld;
#endif // WITH_EDITOR

	check(World != nullptr);

	return ReferenceClassObject(World);
}

void FTsuContext::InitializeBuiltins()
{
	using TsuContext_Private::SetMethod;
	using TsuContext_Private::SetProperty;

	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Global = Context->Global();

	SetProperty(Context, Global, u"global"_v8, Global);
	SetProperty(Context, Global, u"module"_v8, Global);
	SetProperty(Context, Global, u"exports"_v8, v8::Object::New(Isolate));

	SetMethod(Context, Global, u"setTimeout"_v8, &FTsuContext::OnSetTimeout);
	SetMethod(Context, Global, u"setInterval"_v8, &FTsuContext::OnSetInterval);
	SetMethod(Context, Global, u"clearTimeout"_v8, &FTsuContext::OnClearTimeout);
	SetMethod(Context, Global, u"clearInterval"_v8, &FTsuContext::OnClearTimeout);
	SetMethod(Context, Global, u"__require"_v8, &FTsuContext::OnRequire);
	SetMethod(Context, Global, u"__import"_v8, &FTsuContext::OnImport);
	SetMethod(Context, Global, u"__get"_v8, &FTsuContext::OnGetStruct);
	SetMethod(Context, Global, u"__set"_v8, &FTsuContext::OnSetStruct);

	v8::Local<v8::Object> Console = v8::Object::New(Isolate);
	SetMethod(Context, Console, u"log"_v8, &FTsuContext::OnConsoleLog);
	SetMethod(Context, Console, u"display"_v8, &FTsuContext::OnConsoleDisplay);
	SetMethod(Context, Console, u"error"_v8, &FTsuContext::OnConsoleError);
	SetMethod(Context, Console, u"warn"_v8, &FTsuContext::OnConsoleWarning);
	SetMethod(Context, Console, u"time"_v8, &FTsuContext::OnConsoleTimeBegin);
	SetMethod(Context, Console, u"timeEnd"_v8, &FTsuContext::OnConsoleTimeEnd);
	SetProperty(Context, Global, u"console"_v8, Console);

	v8::Local<v8::Object> Path = v8::Object::New(Isolate);
	SetMethod(Context, Path, u"join"_v8, &FTsuContext::OnPathJoin);
	SetMethod(Context, Path, u"resolve"_v8, &FTsuContext::OnPathResolve);
	SetMethod(Context, Path, u"dirname"_v8, &FTsuContext::OnPathDirName);
	SetProperty(Context, Global, u"__path"_v8, Path);

	v8::Local<v8::Object> File = v8::Object::New(Isolate);
	SetMethod(Context, File, u"read"_v8, &FTsuContext::OnFileRead);
	SetMethod(Context, File, u"exists"_v8, &FTsuContext::OnFileExists);
	SetProperty(Context, Global, u"__file"_v8, File);

#if PLATFORM_WINDOWS
	SetProperty(Context, Global, u"__platform"_v8, u"win32"_v8);
#elif PLATFORM_MAC
	SetProperty(Context, Global, u"__platform"_v8, u"darwin"_v8);
#elif PLATFORM_LINUX
	SetProperty(Context, Global, u"__platform"_v8, u"linux"_v8);
#else
#error Not implemented
#endif

	SetProperty(Context, Global, u"__ue"_v8, v8::Object::New(Isolate));
}

void FTsuContext::InitializeDelegates()
{
	if (!GlobalDelegateTemplate.IsEmpty())
		return;

	v8::Local<v8::FunctionTemplate> DelegateTemplate = v8::FunctionTemplate::New(Isolate);
	DelegateTemplate->SetClassName(u"Delegate"_v8);
	DelegateTemplate->InstanceTemplate()->SetInternalFieldCount(2);

	v8::Local<v8::ObjectTemplate> DelegatePrototypeTemplate = DelegateTemplate->PrototypeTemplate();
	DelegatePrototypeTemplate->Set(u"bind"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnDelegateBind));
	DelegatePrototypeTemplate->Set(u"unbind"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnDelegateUnbind));
	DelegatePrototypeTemplate->Set(u"execute"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnDelegateExecute));
	DelegatePrototypeTemplate->SetAccessorProperty(u"isBound"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnDelegateIsBound));

	v8::Local<v8::FunctionTemplate> MulticastDelegateTemplate = v8::FunctionTemplate::New(Isolate);
	MulticastDelegateTemplate->SetClassName(u"MulticastDelegate"_v8);
	MulticastDelegateTemplate->InstanceTemplate()->SetInternalFieldCount(2);

	v8::Local<v8::ObjectTemplate> MulticastDelegatePrototypeTemplate = MulticastDelegateTemplate->PrototypeTemplate();
	MulticastDelegatePrototypeTemplate->Set(u"add"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnMulticastDelegateAdd));
	MulticastDelegatePrototypeTemplate->Set(u"remove"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnMulticastDelegateRemove));
	MulticastDelegatePrototypeTemplate->Set(u"broadcast"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnMulticastDelegateBroadcast));
	MulticastDelegatePrototypeTemplate->SetAccessorProperty(u"isBound"_v8, v8::FunctionTemplate::New(Isolate, &FTsuContext::OnMulticastDelegateIsBound));

	GlobalDelegateTemplate.Reset(Isolate, DelegateTemplate);
	GlobalMulticastDelegateTemplate.Reset(Isolate, MulticastDelegateTemplate);
}

void FTsuContext::InitializeRequire()
{
	const FString RequirePath = FTsuPaths::RequirePath();

#if UE_BUILD_SHIPPING
#error LoadFileToString won't work in Shipping
#endif // UE_BUILD_SHIPPING

	FString RequireCode;
	verify(FFileHelper::LoadFileToString(RequireCode, *RequirePath));
	BindModule(TEXT("require"), *RequireCode, *RequirePath);
}

void FTsuContext::InitializeArrayProxy()
{
	if (!GlobalArrayConstructor.IsEmpty())
		return;

	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

	v8::Local<v8::FunctionTemplate> ConstructorTemplate = v8::FunctionTemplate::New(Isolate);
	v8::Local<v8::ObjectTemplate> PrototypeTemplate = ConstructorTemplate->PrototypeTemplate();
	v8::Local<v8::ObjectTemplate> InstanceTemplate = ConstructorTemplate->InstanceTemplate();

	InstanceTemplate->SetInternalFieldCount(2);
	InstanceTemplate->SetHandler({&FTsuContext::OnArrayGetIndex, &FTsuContext::OnArraySetIndex});

	PrototypeTemplate->SetAccessorProperty(
		u"length"_v8,
		v8::FunctionTemplate::New(Isolate, &FTsuContext::OnArrayGetLength),
		v8::FunctionTemplate::New(Isolate, &FTsuContext::OnArraySetLength));

	// clang-format off
	// #todo(#mihe): Move this to its own file somehow
	constexpr TCHAR HandlerCode[] =
		TEXT("(class {")
		TEXT("    constructor(impl) {")
		TEXT("        this._impl = impl;")
		TEXT("    }")
		TEXT("    get(_, key) {")
		TEXT("        if (key === 'length') {")
		TEXT("            return this._impl.length;")
		TEXT("        }")
		TEXT("        return Array.prototype[key] || this._impl[key];")
		TEXT("    }")
		TEXT("    set(_, key, value) {")
		TEXT("        this._impl[key] = value;")
		TEXT("        return true;")
		TEXT("    }")
		TEXT("    has(_, key) {")
		TEXT("        return (key in Array.prototype) || (key in this._impl);")
		TEXT("    }")
		TEXT("})");
	// clang-format on

	v8::Local<v8::String> HandlerSourceValue = TCHAR_TO_V8(HandlerCode);
	v8::Local<v8::Script> CompiledScript = v8::Script::Compile(Context, HandlerSourceValue).ToLocalChecked();
	v8::Local<v8::Function> HandlerConstructor = CompiledScript->Run(Context).ToLocalChecked().As<v8::Function>();

	v8::Local<v8::Function> Constructor = ConstructorTemplate->GetFunction(Context).ToLocalChecked();

	GlobalArrayConstructor.Reset(Isolate, Constructor);
	GlobalArrayHandlerConstructor.Reset(Isolate, HandlerConstructor);
}

void FTsuContext::InitializeStructProxy()
{
	if (!GlobalStructHandlerConstructor.IsEmpty())
		return;

	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

	// clang-format off
	// #todo(#mihe): Move this to its own file somehow
	constexpr TCHAR HandlerCode[] =
		TEXT("(class {")
		TEXT("    constructor(parent, property) {")
		TEXT("        this._parent = parent;")
		TEXT("        this._property = property;")
		TEXT("    }")
		TEXT("    get target() {")
		TEXT("        return __get(this._parent, this._property);")
		TEXT("    }")
		TEXT("    set target(value) {")
		TEXT("        __set(this._parent, this._property, value);")
		TEXT("    }")
		TEXT("    get(_, key) {")
		TEXT("        return this.target[key];")
		TEXT("    }")
		TEXT("    set(_, key, value) {")
		TEXT("        const target = this.target;")
		TEXT("        target[key] = value;")
		TEXT("        this.target = target;")
		TEXT("        return true;")
		TEXT("    }")
		TEXT("    has(_, key) { return key in this.target; }")
		TEXT("    getPrototypeOf(_) { return Object.getPrototypeOf(this.target); }")
		TEXT("    setPrototypeOf(_, value) { return Object.setPrototypeOf(this.target, value); }")
		TEXT("    isExtensible(_) { return Object.isExtensible(this.target); }")
		TEXT("    preventExtensions(_) { return Object.preventExtensions(this.target); }")
		TEXT("    getOwnPropertyDescriptor(_, key) { return Object.getOwnPropertyDescriptor(this.target, key); }")
		TEXT("    deleteProperty(_, key) { delete this.target[key]; }")
		TEXT("    defineProperty(_, key, attributes) { return Object.defineProperty(this.target, key, attributes); }")
		TEXT("    enumerate(_) { return Reflect.enumerate(this.target); }")
		TEXT("    ownKeys(_) { return Reflect.ownKeys(this.target); }")
		TEXT("    apply(_, thisArg, args) { return this.target.apply(thisArg, args); }")
		TEXT("})");
	// clang-format on

	v8::Local<v8::String> HandlerSourceValue = TCHAR_TO_V8(HandlerCode);
	v8::Local<v8::Script> CompiledScript = v8::Script::Compile(Context, HandlerSourceValue).ToLocalChecked();
	v8::Local<v8::Function> HandlerConstructor = CompiledScript->Run(Context).ToLocalChecked().As<v8::Function>();

	GlobalStructHandlerConstructor.Reset(Isolate, HandlerConstructor);
}

v8::Local<v8::Function> FTsuContext::ExposeObject(UStruct* Type)
{
	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::FunctionTemplate> Template = FindOrAddTemplate(Type);
	return Template->GetFunction(Context).ToLocalChecked();
}

v8::Local<v8::FunctionTemplate> FTsuContext::AddTemplate(UStruct* Type)
{
	auto GetNonAbstractClassType = [](UStruct* Type)
	{
		auto ClassType = Cast<UClass>(Type);
		return ClassType && !ClassType->HasAnyClassFlags(CLASS_Abstract) ? ClassType : nullptr;
	};

	v8::Local<v8::FunctionTemplate> ConstructorTemplate;
	if (UFunction* MakeFunction = FTsuReflection::FindMakeFunction(Type))
	{
		ConstructorTemplate = v8::FunctionTemplate::New(
			Isolate,
			&FTsuContext::OnCallStaticMethod,
			v8::External::New(Isolate, MakeFunction));
	}
	else if (UClass* ClassType = GetNonAbstractClassType(Type))
	{
		ConstructorTemplate = v8::FunctionTemplate::New(
			Isolate,
			&FTsuContext::OnClassNew,
			v8::External::New(Isolate, Type));
	}
	else if (auto ScriptStructType = Cast<UScriptStruct>(Type))
	{
		ConstructorTemplate = v8::FunctionTemplate::New(
			Isolate,
			&FTsuContext::OnStructNew,
			v8::External::New(Isolate, ScriptStructType));
	}
	else
	{
		ConstructorTemplate = v8::FunctionTemplate::New(Isolate);
	}

	ConstructorTemplate->SetClassName(TCHAR_TO_V8(FTsuTypings::TailorNameOfType(Type)));

	v8::Local<v8::ObjectTemplate> InstanceTemplate = ConstructorTemplate->InstanceTemplate();
	InstanceTemplate->SetInternalFieldCount(2);

	v8::Local<v8::ObjectTemplate> PrototypeTemplate = ConstructorTemplate->PrototypeTemplate();

	FTsuReflection::VisitObjectProperties([&](UProperty* Property, bool bIsReadOnly)
	{
		v8::Local<v8::String> Name = TCHAR_TO_V8(FTsuTypings::TailorNameOfField(Property));
		v8::Local<v8::External> Data = v8::External::New(Isolate, Property);
		v8::Local<v8::FunctionTemplate> Getter = v8::FunctionTemplate::New(Isolate, &FTsuContext::OnPropertyGet, Data);
		v8::Local<v8::FunctionTemplate> Setter = bIsReadOnly
			? v8::Local<v8::FunctionTemplate>()
			: v8::FunctionTemplate::New(Isolate, &FTsuContext::OnPropertySet, Data);

		PrototypeTemplate->SetAccessorProperty(Name, Getter, Setter);
	}, Type);

	FTsuReflection::VisitObjectMethods([&](UFunction* Method)
	{
		v8::Local<v8::String> Name = TCHAR_TO_V8(FTsuTypings::TailorNameOfField(Method));
		v8::Local<v8::External> Data = v8::External::New(Isolate, Method);

		if (Method->HasAnyFunctionFlags(FUNC_Static))
		{
			ConstructorTemplate->Set(
				Name,
				v8::FunctionTemplate::New(
					Isolate,
					&FTsuContext::OnCallStaticMethod,
					Data));
		}
		else
		{
			PrototypeTemplate->Set(
				Name,
				v8::FunctionTemplate::New(
					Isolate,
					&FTsuContext::OnCallMethod,
					Data));
		}
	}, Type);

	FTsuReflection::VisitObjectExtensions([&](UFunction* Extension)
	{
		v8::Local<v8::String> Name = TCHAR_TO_V8(FTsuTypings::TailorNameOfExtension(Type, Extension));

		v8::Local<v8::FunctionTemplate> Callback = v8::FunctionTemplate::New(
			Isolate,
			&FTsuContext::OnCallExtensionMethod,
			v8::External::New(Isolate, Extension));

		PrototypeTemplate->Set(Name, Callback);
	}, Type);

	if (auto Class = Cast<UClass>(Type))
	{
		ConstructorTemplate->SetAccessorProperty(
			u"staticClass"_v8,
			v8::FunctionTemplate::New(
				Isolate,
				&FTsuContext::OnGetStaticClass,
				v8::External::New(Isolate, Class)));

		if (UStruct* Super = Class->GetSuperStruct())
			ConstructorTemplate->Inherit(FindOrAddTemplate(Super));
	}

	v8::Global<v8::FunctionTemplate> CachedTemplate;
	CachedTemplate.Reset(Isolate, ConstructorTemplate);
	Templates.Add(Type, MoveTemp(CachedTemplate));

	return ConstructorTemplate;
}

v8::Local<v8::FunctionTemplate> FTsuContext::FindOrAddTemplate(UStruct* Type)
{
	if (v8::Global<v8::FunctionTemplate>* FoundTemplate = Templates.Find(Type))
		return FoundTemplate->Get(Isolate);

	return AddTemplate(Type);
}

v8::Local<v8::Value> FTsuContext::ReferenceStructObject(void* StructObject, UScriptStruct* StructType)
{
	v8::Local<v8::FunctionTemplate> ConstructorTemplate = FindOrAddTemplate(StructType);
	v8::Local<v8::ObjectTemplate> InstanceTemplate = ConstructorTemplate->InstanceTemplate();

	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Value = InstanceTemplate->NewInstance(Context).ToLocalChecked();
	Value->SetAlignedPointerInInternalField(0, StructObject);
	Value->SetAlignedPointerInInternalField(1, StructType);

	auto OnCollected = [](const v8::WeakCallbackInfo<FTsuContext>& Info)
	{
		void* StructObject = Info.GetInternalField(0);
		auto StructType = static_cast<UScriptStruct*>(Info.GetInternalField(1));

		StructType->DestroyStruct(StructObject);
		FMemory::Free(StructObject);

		Isolate->AdjustAmountOfExternalAllocatedMemory(-StructType->GetStructureSize());

		Info.GetParameter()->AliveStructs.Remove(FStructKey{StructObject, StructType});
	};

	v8::Global<v8::Object>& Observer = AliveStructs.Add(FStructKey{StructObject, StructType});
	Observer.Reset(Isolate, Value);
	Observer.SetWeak(this, OnCollected, v8::WeakCallbackType::kInternalFields);

	Isolate->AdjustAmountOfExternalAllocatedMemory(StructType->GetStructureSize());

	return Value;
}

v8::Local<v8::Value> FTsuContext::ReferenceClassObject(UObject* ClassObject)
{
	if (!ClassObject)
		return v8::Null(Isolate);

	v8::Local<v8::Object> Value;

	if (v8::Global<v8::Object>* Found = AliveObjects.Find(ClassObject))
	{
		Value = Found->Get(Isolate);
	}
	else
	{
		v8::Local<v8::FunctionTemplate> ConstructorTemplate = FindOrAddTemplate(ClassObject->GetClass());
		v8::Local<v8::ObjectTemplate> InstanceTemplate = ConstructorTemplate->InstanceTemplate();

		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
		Value = InstanceTemplate->NewInstance(Context).ToLocalChecked().As<v8::Object>();
		Value->SetAlignedPointerInInternalField(0, ClassObject);
		Value->SetAlignedPointerInInternalField(1, ClassObject->GetClass());

		auto OnCollected = [](const v8::WeakCallbackInfo<FTsuContext>& Info)
		{
			auto ClassObject = static_cast<UObject*>(Info.GetInternalField(0));
			Info.GetParameter()->AliveObjects.Remove(ClassObject);
		};

		v8::Global<v8::Object>& Observer = AliveObjects.Add(ClassObject);
		Observer.Reset(Isolate, Value);
		Observer.SetWeak(this, OnCollected, v8::WeakCallbackType::kInternalFields);
	}

	return Value;
}

v8::Local<v8::Value> FTsuContext::ReferenceDelegate(UProperty* DelegateProperty, UObject* Parent)
{
	v8::Local<v8::Object> Value;

	FDelegateKey Key{Parent, DelegateProperty};
	if (v8::Global<v8::Object>* Found = AliveDelegates.Find(Key))
	{
		Value = Found->Get(Isolate);
	}
	else
	{
		v8::Local<v8::FunctionTemplate> ConstructorTemplate = GlobalMulticastDelegateTemplate.Get(Isolate);
		v8::Local<v8::ObjectTemplate> InstanceTemplate = ConstructorTemplate->InstanceTemplate();

		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
		Value = InstanceTemplate->NewInstance(Context).ToLocalChecked().As<v8::Object>();
		Value->SetAlignedPointerInInternalField(0, Parent);
		Value->SetAlignedPointerInInternalField(1, DelegateProperty);

		auto OnCollected = [](const v8::WeakCallbackInfo<FTsuContext>& Info)
		{
			auto Parent = static_cast<UObject*>(Info.GetInternalField(0));
			auto Property = static_cast<UProperty*>(Info.GetInternalField(1));
			Info.GetParameter()->AliveDelegates.Remove(FDelegateKey{Parent, Property});
		};

		v8::Global<v8::Object>& Observer = AliveDelegates.Add(Key);
		Observer.Reset(Isolate, Value);
		Observer.SetWeak(this, OnCollected, v8::WeakCallbackType::kInternalFields);
	}

	return Value;
}

void FTsuContext::Invoke(const TCHAR* Binding, FFrame& Stack, RESULT_DECL)
{
	v8::HandleScope HandleScope{Isolate};

	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Global = Context->Global();

	FTsuWorldScope WorldScope{*this, Stack.Object};

	UFunction* Function = Stack.CurrentNativeFunction;
	const FString FunctionName = FTsuTypings::TailorNameOfField(Function);
	v8::Local<v8::Function> Export = GetExportedFunction(Binding, *FunctionName).ToLocalChecked();

	TArray<v8::Local<v8::Value>> Arguments;
	PopArgumentsFromStack(Stack, Function, Arguments);

	FTsuTryCatch Catcher{Isolate};

	v8::MaybeLocal<v8::Value> MaybeReturnValue = Export->Call(
		Context,
		Global,
		Arguments.Num(),
		Arguments.GetData());

	Catcher.Check();

	v8::Local<v8::Value> ReturnValue;
	if (!MaybeReturnValue.ToLocal(&ReturnValue))
		return;

	if (UProperty* ReturnProperty = Function->GetReturnProperty())
		WritePropertyToBuffer(ReturnProperty, ReturnValue, RESULT_PARAM);
}

bool FTsuContext::InvokeDelegateEvent(
	v8::Local<v8::Value> WorldContext,
	v8::Local<v8::Function> Callback,
	UFunction* Signature,
	void* ParamsBuffer)
{
	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);
	v8::Local<v8::Object> Global = Context->Global();

	FTsuWorldScope WorldScope{*this, WorldContext};

	TArray<v8::Local<v8::Value>> Arguments;

	if (Signature)
	{
		FTsuReflection::VisitFunctionParameters([&](UProperty* Parameter)
		{
			Arguments.Add(ReadPropertyFromContainer(Parameter, ParamsBuffer));
		}, Signature, false, false);
	}

	FTsuTryCatch Catcher{Isolate};

	return !Callback->Call(Context, Global, Arguments.Num(), Arguments.GetData()).IsEmpty();
}

void FTsuContext::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AllowEliminatingReferences(false);

	for (auto& Struct : AliveStructs)
		Collector.AddReferencedObject(Struct.Key.Value);

	for (auto& Object : AliveObjects)
		Collector.AddReferencedObject(Object.Key);

	for (auto& Delegate : AliveDelegates)
		Collector.AddReferencedObject(Delegate.Key.Key);

	for (FTsuTimer& Timer : AliveTimers)
		Collector.AddReferencedObject(Timer.Event);

	for (auto& Events : DelegateEvents)
	{
		for (auto& Event : Events.Value)
			Collector.AddReferencedObject(Event.Value);
	}

	Collector.AllowEliminatingReferences(true);
}

void FTsuContext::OnPreGarbageCollect()
{
	for (auto Iter = AliveTimers.CreateIterator(); Iter; ++Iter)
	{
		if (auto World = Iter->World.Get())
		{
			if (!World->GetTimerManager().TimerExists(Iter->Handle))
				Iter.RemoveCurrent();
		}
		else
		{
			Iter.RemoveCurrent();
		}
	}
}

void FTsuContext::OnPostGarbageCollect()
{
	if (DelegateEvents.Num() > 0)
	{
		for (auto Iter = DelegateEvents.CreateIterator(); Iter; ++Iter)
		{
			if (!Iter->Key.IsValid())
				Iter.RemoveCurrent();
		}

		DelegateEvents.Compact();
	}
}

v8::Local<v8::Value> FTsuContext::StartTimeout(v8::Local<v8::Function> Callback, float Delay, bool bLoop)
{
	v8::Local<v8::Value> WorldContext_ = GetWorldContext();
	UObject* WorldContext = nullptr;
	TsuContext_Private::GetInternalFields(WorldContext_, &WorldContext);

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
		return {};

	auto Event = NewObject<UTsuDelegateEvent>();
	Event->Initialize(Callback);

	FTimerManager& TimerManager = World->GetTimerManager();

	FTimerDelegate Delegate;
	Delegate.BindUFunction(Event, NameEventExecute);

	FTimerHandle Handle;
	TimerManager.SetTimer(Handle, Delegate, Delay, bLoop);

	AliveTimers.Emplace(World, Handle, Event);

	UScriptStruct* HandleStruct = FTimerHandle::StaticStruct();
	void* HandleBuffer = FMemory::Malloc(HandleStruct->GetStructureSize());

	HandleStruct->CopyScriptStruct(HandleBuffer, &Handle);

	return ReferenceStructObject(HandleBuffer, HandleStruct);
}

void FTsuContext::OnConsoleLog(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	FString ScriptName;
	FString FunctionName;
	int32 LineNumber = 0;
	TsuContext_Private::GetCallSite(ScriptName, FunctionName, LineNumber);

	FString Message;
	if (ensureV8(TsuContext_Private::ValuesToString(TsuContext_Private::ExtractArgs(Info), Message)))
	{
		UE_LOG(LogTsu, Log, TEXT("%s (at %s:%s:%d)"), *Message, *ScriptName, *FunctionName, LineNumber);
	}
}

void FTsuContext::OnConsoleDisplay(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() >= 2))
		return;

	if (GAreScreenMessagesEnabled)
	{
		v8::Local<v8::Value> DurationValue = Info[0];
		if (!ensureV8(DurationValue->IsNumber()))
			return;

		const double Duration = DurationValue.As<v8::Number>()->Value();

		FString Message;
		if (ensureV8(TsuContext_Private::ValuesToString(TsuContext_Private::ExtractArgs(Info, 1), Message)))
			GEngine->AddOnScreenDebugMessage((uint64)-1, (float)Duration, FColor::Cyan, Message);
	}
}

void FTsuContext::OnConsoleError(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	FString ScriptName;
	FString FunctionName;
	int32 LineNumber = 0;
	TsuContext_Private::GetCallSite(ScriptName, FunctionName, LineNumber);

	FString Message;
	if (ensureV8(TsuContext_Private::ValuesToString(TsuContext_Private::ExtractArgs(Info), Message)))
	{
		UE_LOG(LogTsu, Error, TEXT("%s (at %s:%s:%d)"), *Message, *ScriptName, *FunctionName, LineNumber);
	}
}

void FTsuContext::OnConsoleWarning(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	FString ScriptName;
	FString FunctionName;
	int32 LineNumber = 0;
	TsuContext_Private::GetCallSite(ScriptName, FunctionName, LineNumber);

	FString Message;
	if (ensureV8(TsuContext_Private::ValuesToString(TsuContext_Private::ExtractArgs(Info), Message)))
	{
		UE_LOG(LogTsu, Warning, TEXT("%s (at %s:%s:%d)"), *Message, *ScriptName, *FunctionName, LineNumber);
	}
}

void FTsuContext::OnConsoleTimeBeginImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	v8::Local<v8::Value> LabelArg = Info[0];
	if (!ensureV8(LabelArg->IsString()))
		return;

	const FString Label = V8_TO_TCHAR(LabelArg.As<v8::String>());

	PendingTimeLogs.FindOrAdd(Label) = FPlatformTime::Cycles64();
}

void FTsuContext::OnConsoleTimeEndImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	v8::Local<v8::Value> LabelArg = Info[0];
	if (!ensureV8(LabelArg->IsString()))
		return;

	const FString Label = V8_TO_TCHAR(LabelArg.As<v8::String>());

	const uint64 EndCycles = FPlatformTime::Cycles64();

	uint64 StartCycles = 0;
	if (PendingTimeLogs.RemoveAndCopyValue(Label, StartCycles))
	{
		const float ElapsedMs = FPlatformTime::ToMilliseconds(EndCycles - StartCycles);
		UE_LOG(LogTsu, Log, TEXT("%s: %.3f ms"), *Label, ElapsedMs);
	}
	else
	{
		UE_LOG(LogTsu, Error, TEXT("No active time label named '%s'"), *Label);
	}
}

void FTsuContext::OnClassNewImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UClass* Type = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Type)))
		return;

	UObject* Outer = nullptr;
	v8::Local<v8::Value> OuterArg = Info[0];
	if (OuterArg->IsObject())
		TsuContext_Private::GetInternalFields(OuterArg, &Outer);

	if (!Outer)
		Outer = GetTransientPackage();

	Info.GetReturnValue().Set(ReferenceClassObject(StaticConstructObject_Internal(Type, Outer)));
}

void FTsuContext::OnStructNewImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UScriptStruct* Type = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Type)))
		return;

	void* Object = FMemory::Malloc(Type->GetStructureSize());
	Type->InitializeStruct(Object);

	Info.GetReturnValue().Set(ReferenceStructObject(Object, Type));
}

void FTsuContext::OnCallMethodImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UFunction* Method = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Method)))
		return;

	UObject* Object = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Object)))
		return;

	void* ParamsBuffer = FMemory_Alloca(Method->ParmsSize);

	for (UProperty* Param : FParamRange(Method))
		Param->InitializeValue_InContainer(ParamsBuffer);

	ON_SCOPE_EXIT
	{
		for (UProperty* Param : FParamRange(Method))
			Param->DestroyValue_InContainer(ParamsBuffer);
	};

	GetMethodParams(Info, Method, ParamsBuffer);
	CallMethod(Object, Method, ParamsBuffer, Info.GetReturnValue());
}

void FTsuContext::OnCallStaticMethodImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UFunction* Method = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Method)))
		return;

	void* ParamsBuffer = FMemory_Alloca(Method->ParmsSize);

	for (UProperty* Param : FParamRange(Method))
		Param->InitializeValue_InContainer(ParamsBuffer);

	ON_SCOPE_EXIT
	{
		for (UProperty* Param : FParamRange(Method))
			Param->DestroyValue_InContainer(ParamsBuffer);
	};

	UObject* Object = Method->GetOwnerClass()->GetDefaultObject();

	GetMethodParams(Info, Method, ParamsBuffer);
	CallMethod(Object, Method, ParamsBuffer, Info.GetReturnValue());
}

void FTsuContext::OnCallExtensionMethodImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UFunction* Method = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Method)))
		return;

	void* ParamsBuffer = FMemory_Alloca(Method->ParmsSize);

	for (UProperty* Param : FParamRange(Method))
		Param->InitializeValue_InContainer(ParamsBuffer);

	ON_SCOPE_EXIT
	{
		for (UProperty* Param : FParamRange(Method))
			Param->DestroyValue_InContainer(ParamsBuffer);
	};

	UObject* Object = Method->GetOwnerClass()->GetDefaultObject();

	GetExtensionMethodParams(Info, Method, ParamsBuffer);
	CallMethod(Object, Method, ParamsBuffer, Info.GetReturnValue());
}

void FTsuContext::OnPropertyGetImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	v8::Local<v8::Context> Context = Isolate->GetCurrentContext();

	UProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Property)))
		return;

	v8::Local<v8::Value> This = UnwrapStructProxy(Info.This());

	void* Self = nullptr;
	UStruct* Type = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(This, &Self, &Type)))
		return;

	if (UFunction* BreakFunction = FTsuReflection::FindBreakFunction(Type))
	{
		void* ParamsBuffer = FMemory_Alloca(BreakFunction->ParmsSize);

		for (UProperty* Param : FParamRange(BreakFunction))
			Param->InitializeValue_InContainer(ParamsBuffer);

		ON_SCOPE_EXIT
		{
			for (UProperty* Param : FParamRange(BreakFunction))
				Param->DestroyValue_InContainer(ParamsBuffer);
		};

		FParamIterator ParamIt{BreakFunction};
		auto StructParam = Cast<UStructProperty>(*ParamIt);
		StructParam->CopyCompleteValue(StructParam->ContainerPtrToValuePtr<void>(ParamsBuffer), Self);

		BreakFunction->GetOwnerClass()->ProcessEvent(BreakFunction, ParamsBuffer);

		for (UProperty* OutParam : FParamRange(BreakFunction))
		{
			if (Property == OutParam)
			{
				Info.GetReturnValue().Set(ReadPropertyFromContainer(OutParam, ParamsBuffer));
				break;
			}
		}
	}
	else if (auto DelegateProperty = Cast<UDelegateProperty>(Property))
	{
		Info.GetReturnValue().Set(ReferenceDelegate(DelegateProperty, static_cast<UObject*>(Self)));
	}
	else if (auto MulticastDelegateProperty = Cast<UMulticastDelegateProperty>(Property))
	{
		Info.GetReturnValue().Set(ReferenceDelegate(MulticastDelegateProperty, static_cast<UObject*>(Self)));
	}
	else if (auto StructProperty = Cast<UStructProperty>(Property))
	{
		// #todo(#mihe): Consider fixed-size arrays since we're skipping ReadPropertyFromContainer
		check(StructProperty->ArrayDim == 1);

		v8::Local<v8::Function> HandlerConstructor = GlobalStructHandlerConstructor.Get(Isolate);
		v8::Local<v8::Value> HandlerArgs[] = {This, Info.Data()};
		v8::Local<v8::Object> Handler = HandlerConstructor->NewInstance(
			Context,
			ARRAY_COUNT(HandlerArgs),
			HandlerArgs
		).ToLocalChecked();

		v8::Local<v8::Proxy> Proxy = v8::Proxy::New(
			Context,
			v8::Object::New(Isolate),
			Handler
		).ToLocalChecked();

		Info.GetReturnValue().Set(Proxy);
	}
	else
	{
		Info.GetReturnValue().Set(ReadPropertyFromContainer(Property, Self));
	}
}

void FTsuContext::OnPropertySetImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	UProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Property)))
		return;

	v8::Local<v8::Value> This = UnwrapStructProxy(Info.This());

	void* Self = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(This, &Self)))
		return;

	if (!ensureV8(WritePropertyToContainer(Property, Info[0], Self)))
		return;
}

void FTsuContext::OnSetTimeoutImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 2))
		return;

	v8::Local<v8::Function> InCallback = Info[0].As<v8::Function>();
	v8::Local<v8::Number> InDelay = Info[1].As<v8::Number>();

	float Delay = (float)(InDelay->Value() / 1000);

	v8::Local<v8::Value> Handle = StartTimeout(InCallback, Delay, false);
	if (!ensureV8(!Handle.IsEmpty()))
		return;

	Info.GetReturnValue().Set(Handle);
}

void FTsuContext::OnSetIntervalImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 2))
		return;

	v8::Local<v8::Function> InCallback = Info[0].As<v8::Function>();
	v8::Local<v8::Number> InDelay = Info[1].As<v8::Number>();

	float Delay = (float)(InDelay->Value() / 1000);

	v8::Local<v8::Value> Handle = StartTimeout(InCallback, Delay, true);
	if (!ensureV8(!Handle.IsEmpty()))
		return;

	Info.GetReturnValue().Set(Handle);
}

void FTsuContext::OnClearTimeoutImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	v8::Local<v8::Value> HandleValue = UnwrapStructProxy(Info[0]);

	FTimerHandle* Handle = nullptr;
	TsuContext_Private::GetInternalFields(HandleValue, &Handle);

	for (auto Iter = AliveTimers.CreateIterator(); Iter; ++Iter)
	{
		if (Iter->Handle == *Handle)
		{
			if (UWorld* World = Iter->World.Get())
				World->GetTimerManager().ClearTimer(Iter->Handle);

			Iter.RemoveCurrent();
			break;
		}
	}
}

void FTsuContext::OnPathJoin(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	const int32 NumArgs = Info.Length();

	FString Path;
	for (int32 Index = 0; Index < NumArgs; ++Index)
	{
		v8::Local<v8::Value> Arg = Info[Index];
		if (!ensureV8(Arg->IsString()))
			return;

		Path /= V8_TO_TCHAR(Arg.As<v8::String>());
	}

	Info.GetReturnValue().Set(TCHAR_TO_V8(Path));
}

void FTsuContext::OnPathResolve(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	const int32 NumArgs = Info.Length();

	FString Path;
	for (int32 Index = 0; Index < NumArgs; ++Index)
	{
		v8::Local<v8::Value> Arg = Info[Index];
		if (!ensureV8(Arg->IsString()))
			return;

		Path /= V8_TO_TCHAR(Arg.As<v8::String>());
	}

	FPaths::NormalizeFilename(Path);
	FPaths::CollapseRelativeDirectories(Path);
	FPaths::RemoveDuplicateSlashes(Path);

	Info.GetReturnValue().Set(TCHAR_TO_V8(Path));
}

void FTsuContext::OnPathDirName(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	v8::Local<v8::Value> PathArg = Info[0];
	if (!ensureV8(PathArg->IsString()))
		return;

	const FString Path = V8_TO_TCHAR(PathArg.As<v8::String>());
	const FString DirName = FPaths::GetPath(Path);
	Info.GetReturnValue().Set(TCHAR_TO_V8(DirName));
}

void FTsuContext::OnFileRead(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	v8::Local<v8::Value> PathArg = Info[0];
	if (!ensureV8(PathArg->IsString()))
		return;

	const FString Path = V8_TO_TCHAR(PathArg.As<v8::String>());

#if UE_BUILD_SHIPPING
#error LoadFileToString won't work in Shipping
#endif // UE_BUILD_SHIPPING

	FString FileContents;
	if (!ensureV8(FFileHelper::LoadFileToString(FileContents, *Path)))
		return;

	Info.GetReturnValue().Set(TCHAR_TO_V8(FileContents));
}

void FTsuContext::OnFileExists(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	auto PathArg = Info[0];
	if (!ensureV8(PathArg->IsString()))
		return;

	const FString Path = V8_TO_TCHAR(PathArg.As<v8::String>());

#if UE_BUILD_SHIPPING
#error FileExists won't work in Shipping
#endif // UE_BUILD_SHIPPING

	const bool bFileExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*Path);
	Info.GetReturnValue().Set(bFileExists);
}

void FTsuContext::OnRequireImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	auto PathArg = Info[0];
	if (!ensureV8(PathArg->IsString()))
		return;

	const FString Path = V8_TO_TCHAR(PathArg.As<v8::String>());

#if UE_BUILD_SHIPPING
#error LoadFileToString won't work in Shipping
#endif // UE_BUILD_SHIPPING

	FString Code;
	if (!ensureV8(FFileHelper::LoadFileToString(Code, *Path)))
		return;

	v8::MaybeLocal<v8::Value> MaybeModule = EvalModule(*Code, *Path);

	v8::Local<v8::Value> Module;
	if (!ensureV8(MaybeModule.ToLocal(&Module)))
		return;

	Info.GetReturnValue().Set(Module);
}

void FTsuContext::OnImportImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	auto TypeNameArg = Info[0];
	if (!ensureV8(TypeNameArg->IsString()))
		return;

	const FString TypeName = V8_TO_TCHAR(TypeNameArg.As<v8::String>());
	UField* Type = FTsuReflection::FindTypeByName(TypeName);
	auto Object = Cast<UStruct>(Type);
	ensureV8(Object != nullptr);

	Info.GetReturnValue().Set(ExposeObject(Object));
}

void FTsuContext::OnGetStructImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 2))
		return;

	void* Self = nullptr;
	UStruct* Type = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info[0], &Self, &Type)))
		return;

	UProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info[1], &Property)))
		return;

	Info.GetReturnValue().Set(ReadPropertyFromContainer(Property, Self));
}

void FTsuContext::OnSetStructImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 3))
		return;

	void* Self = nullptr;
	UStruct* Type = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info[0], &Self, &Type)))
		return;

	UProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info[1], &Property)))
		return;

	if (!ensureV8(WritePropertyToContainer(Property, Info[2], Self)))
		return;
}

void FTsuContext::OnGetStaticClassImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UClass* Class = nullptr;
	if (!ensureV8(TsuContext_Private::GetExternalValue(Info.Data(), &Class)))
		return;

	v8::Local<v8::Value> Result = ReferenceClassObject(Class);
	Info.GetReturnValue().Set(Result);
}

void FTsuContext::OnDelegateBindImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	UObject* Parent = nullptr;
	UDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto Delegate = Property->ContainerPtrToValuePtr<FScriptDelegate>(Parent);

	v8::Local<v8::Function> Callback = Info[0].As<v8::Function>();

	auto Event = NewObject<UTsuDelegateEvent>();
	Event->Initialize(Callback, Property->SignatureFunction);

	Delegate->BindUFunction(Event, NameEventExecute);

	uint64 DelegateHandle = NextDelegateHandle++;

	DelegateEvents.FindOrAdd(Parent).FindOrAdd(DelegateHandle) = Event;

	Info.GetReturnValue().Set((double)DelegateHandle);
}

void FTsuContext::OnDelegateUnbindImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UObject* Parent = nullptr;
	UDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto Delegate = Property->ContainerPtrToValuePtr<FScriptDelegate>(Parent);
	Delegate->Unbind();
}

void FTsuContext::OnDelegateExecuteImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UObject* Parent = nullptr;
	UDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto Delegate = Property->ContainerPtrToValuePtr<FScriptDelegate>(Parent);
	UFunction* SignatureFunction = Property->SignatureFunction;

	void* ParamsBuffer = FMemory_Alloca(SignatureFunction->ParmsSize);

	for (UProperty* Param : FParamRange(SignatureFunction))
		Param->InitializeValue_InContainer(ParamsBuffer);

	ON_SCOPE_EXIT
	{
		for (UProperty* Param : FParamRange(SignatureFunction))
			Param->DestroyValue_InContainer(ParamsBuffer);
	};

	const int32 NumArgs = Info.Length();

	int32 ArgIndex = 0;
	FTsuReflection::VisitFunctionParameters([&](UProperty* Parameter)
	{
		if (ArgIndex < NumArgs)
			WritePropertyToContainer(Parameter, Info[ArgIndex++], ParamsBuffer);
	}, SignatureFunction, false, false);

	Delegate->ProcessDelegate<UObject>(ParamsBuffer);
}

void FTsuContext::OnDelegateIsBoundImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UObject* Parent = nullptr;
	UDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto Delegate = Property->ContainerPtrToValuePtr<FScriptDelegate>(Parent);

	Info.GetReturnValue().Set(Delegate->IsBound());
}

void FTsuContext::OnMulticastDelegateAddImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	UObject* Parent = nullptr;
	UMulticastDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto MulticastDelegate = Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Parent);

	v8::Local<v8::Function> Callback = Info[0].As<v8::Function>();

	auto Event = NewObject<UTsuDelegateEvent>();
	Event->Initialize(Callback, Property->SignatureFunction);

	FScriptDelegate Delegate;
	Delegate.BindUFunction(Event, NameEventExecute);
	MulticastDelegate->Add(Delegate);

	uint64 DelegateHandle = NextDelegateHandle++;

	DelegateEvents.FindOrAdd(Parent).FindOrAdd(DelegateHandle) = Event;

	Info.GetReturnValue().Set((double)DelegateHandle);
}

void FTsuContext::OnMulticastDelegateRemoveImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	if (!ensureV8(Info.Length() == 1))
		return;

	UObject* Parent = nullptr;
	UMulticastDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto MulticastDelegate = Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Parent);
	uint64 Handle = (uint64)Info[0].As<v8::Number>()->Value();
	UTsuDelegateEvent* Event = DelegateEvents.FindOrAdd(Parent).FindOrAdd(Handle);
	MulticastDelegate->Remove(Event, NameEventExecute);
}

void FTsuContext::OnMulticastDelegateBroadcastImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UObject* Parent = nullptr;
	UMulticastDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto MulticastDelegate = Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Parent);
	UFunction* SignatureFunction = Property->SignatureFunction;

	void* ParamsBuffer = FMemory_Alloca(SignatureFunction->ParmsSize);

	for (UProperty* Param : FParamRange(SignatureFunction))
		Param->InitializeValue_InContainer(ParamsBuffer);

	ON_SCOPE_EXIT
	{
		for (UProperty* Param : FParamRange(SignatureFunction))
			Param->DestroyValue_InContainer(ParamsBuffer);
	};

	const int32 NumArgs = Info.Length();

	int32 ArgIndex = 0;
	FTsuReflection::VisitFunctionParameters([&](UProperty* Parameter)
	{
		if (ArgIndex < NumArgs)
			WritePropertyToContainer(Parameter, Info[ArgIndex++], ParamsBuffer);
	}, SignatureFunction, false, false);

	MulticastDelegate->ProcessMulticastDelegate<UObject>(ParamsBuffer);
}

void FTsuContext::OnMulticastDelegateIsBoundImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	UObject* Parent = nullptr;
	UMulticastDelegateProperty* Property = nullptr;
	if (!ensureV8(TsuContext_Private::GetInternalFields(Info.This(), &Parent, &Property)))
		return;

	auto MulticastDelegate = Property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Parent);

	Info.GetReturnValue().Set(MulticastDelegate->IsBound());
}

void FTsuContext::OnArrayGetIndexImpl(uint32_t Index, const v8::PropertyCallbackInfo<v8::Value>& Info)
{
	v8::Local<v8::Object> This = Info.This();

	auto ArrayProperty = (UArrayProperty*)This->GetAlignedPointerFromInternalField(0);
	void* ArrayBuffer = This->GetAlignedPointerFromInternalField(1);
	FScriptArrayHelper ArrayHelper{ArrayProperty, ArrayBuffer};

	if (Index >= (uint32_t)ArrayHelper.Num())
	{
		Info.GetReturnValue().SetUndefined();
		return;
	}

	UProperty* ElementProperty = ArrayProperty->Inner;
	const void* ElementBuffer = ArrayHelper.GetRawPtr(Index);

	Info.GetReturnValue().Set(ReadPropertyFromBuffer(ElementProperty, ElementBuffer));
}

void FTsuContext::OnArraySetIndexImpl(uint32_t Index, v8::Local<v8::Value> Value, const v8::PropertyCallbackInfo<v8::Value>& Info)
{
	v8::Local<v8::Object> This = Info.This();

	auto ArrayProperty = (UArrayProperty*)This->GetAlignedPointerFromInternalField(0);
	void* ArrayBuffer = This->GetAlignedPointerFromInternalField(1);
	FScriptArrayHelper ArrayHelper{ArrayProperty, ArrayBuffer};

	if (Index >= (uint32_t)ArrayHelper.Num())
		ArrayHelper.Resize(Index + 1);

	UProperty* ElementProperty = ArrayProperty->Inner;
	void* ElementBuffer = ArrayHelper.GetRawPtr(Index);

	WritePropertyToBuffer(ElementProperty, UnwrapStructProxy(Value), ElementBuffer);
}

void FTsuContext::OnArrayGetLengthImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	v8::Local<v8::Object> This = Info.This();

	auto ArrayProperty = (UArrayProperty*)This->GetAlignedPointerFromInternalField(0);
	void* ArrayBuffer = This->GetAlignedPointerFromInternalField(1);
	FScriptArrayHelper ArrayHelper{ArrayProperty, ArrayBuffer};

	const int32 Result = ArrayHelper.Num();
	Info.GetReturnValue().Set((uint32_t)Result);
}

void FTsuContext::OnArraySetLengthImpl(const v8::FunctionCallbackInfo<v8::Value>& Info)
{
	v8::Local<v8::Object> This = Info.This();

	auto ArrayProperty = (UArrayProperty*)This->GetAlignedPointerFromInternalField(0);
	void* ArrayBuffer = This->GetAlignedPointerFromInternalField(1);
	FScriptArrayHelper ArrayHelper{ArrayProperty, ArrayBuffer};

	const int32 NewSize = Info[0].As<v8::Uint32>()->Value();
	ArrayHelper.Resize(NewSize);
}

void FTsuContext::CallMethod(UObject* Object, UFunction* Method, void* ParamsBuffer, v8::ReturnValue<v8::Value> ReturnValue)
{
	Object->ProcessEvent(Method, ParamsBuffer);

	if (FTsuReflection::HasOutputParameters(Method))
	{
		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

		v8::Local<v8::Object> ReturnObject = v8::Object::New(Isolate);

		FTsuReflection::VisitFunctionReturns([&](UProperty* Return)
		{
			const FString Name = FTsuTypings::TailorNameOfField(Return);
			v8::Local<v8::Value> Value = ReadPropertyFromContainer(Return, ParamsBuffer);
			ReturnObject->Set(Context, TCHAR_TO_V8(Name), Value).ToChecked();
		}, Method);

		ReturnValue.Set(ReturnObject);
	}
	else if (UProperty* ReturnProperty = Method->GetReturnProperty())
	{
		ReturnValue.Set(ReadPropertyFromContainer(ReturnProperty, ParamsBuffer));
	}
}

void FTsuContext::GetMethodParams(const v8::FunctionCallbackInfo<v8::Value>& Info, UFunction* Method, void* ParamsBuffer)
{
	FName WorldContextName;
	if (Method->HasMetaData(MetaWorldContext))
		WorldContextName = *Method->GetMetaData(MetaWorldContext);

	const int32 NumArgs = Info.Length();

	int32 ArgIndex = 0;
	FTsuReflection::VisitFunctionParameters([&](UProperty* Parameter)
	{
		bool bArgsConsumed = false;

		v8::Local<v8::Value> ArgValue;
		if (Parameter->GetFName() == WorldContextName)
		{
			ArgValue = GetWorldContext();
		}
		else
		{
			bArgsConsumed = ArgIndex >= NumArgs;
			if (!bArgsConsumed)
				ArgValue = Info[ArgIndex++];
		}

		if (!bArgsConsumed)
		{
			WritePropertyToContainer(Parameter, ArgValue, ParamsBuffer);
		}
		else
		{
			const FString MetaDefaultValue = TEXT("CPP_Default_") + Parameter->GetName();
			if (Method->HasMetaData(*MetaDefaultValue))
			{
				const FString& DefaultValue = Method->GetMetaData(*MetaDefaultValue);
				void* ParamBuffer = Parameter->ContainerPtrToValuePtr<void>(ParamsBuffer);
				Parameter->ImportText(*DefaultValue, ParamBuffer, PPF_None, nullptr);
			}
		}
	}, Method, false, false);
}

void FTsuContext::GetExtensionMethodParams(const v8::FunctionCallbackInfo<v8::Value>& Info, UFunction* Method, void* ParamsBuffer)
{
	FName WorldContextName;
	if (Method->HasMetaData(MetaWorldContext))
		WorldContextName = *Method->GetMetaData(MetaWorldContext);

	const int32 NumArgs = Info.Length();

	int32 ArgIndex = 0;
	int32 JsArgIndex = 0;
	FTsuReflection::VisitFunctionParameters([&](UProperty* Parameter)
	{
		v8::Local<v8::Value> ArgValue;

		if (ArgIndex++ == 0)
		{
			ArgValue = UnwrapStructProxy(Info.This());
		}
		else if (Parameter->GetFName() == WorldContextName)
		{
			ArgValue = GetWorldContext();
		}
		else
		{
			if (JsArgIndex >= NumArgs)
				return;

			ArgValue = Info[JsArgIndex++];
		}

		WritePropertyToContainer(Parameter, ArgValue, ParamsBuffer);
	}, Method, false, false);
}

v8::Local<v8::Value> FTsuContext::UnwrapStructProxy(const v8::Local<v8::Value>& Value)
{
	if (!Value->IsProxy())
		return Value;

	v8::Local<v8::Proxy> Proxy = Value.As<v8::Proxy>();
	v8::Local<v8::Object> Handler = Proxy->GetHandler().As<v8::Object>();

	return Handler->Get(Isolate->GetCurrentContext(), u"target"_v8).ToLocalChecked();
}

void FTsuContext::PopArgumentsFromStack(FFrame& Stack, UFunction* Function, TArray<v8::Local<v8::Value>>& OutArguments)
{
	for (
		UProperty* Argument = (UProperty*)Function->Children;
		Argument != nullptr;
		Argument = (UProperty*)Argument->Next)
	{
		if (!FTsuReflection::IsInputParameter(Argument))
			break;

		PopArgumentFromStack(Stack, Argument, OutArguments);
	}
}

void FTsuContext::PopArgumentFromStack(FFrame& Stack, UProperty* Argument, TArray<v8::Local<v8::Value>>& OutArguments)
{
	if (Argument->IsA<UStrProperty>())
	{
		FString PropertyValue;
		Stack.StepCompiledIn<UStrProperty>(&PropertyValue);
		OutArguments.Add(TCHAR_TO_V8(PropertyValue));
	}
	else if (Argument->IsA<UBoolProperty>())
	{
		bool PropertyValue = false;
		Stack.StepCompiledIn<UBoolProperty>(&PropertyValue);
		OutArguments.Add(v8::Boolean::New(Isolate, PropertyValue));
	}
	else if (Argument->IsA<UFloatProperty>())
	{
		float PropertyValue = 0.f;
		Stack.StepCompiledIn<UFloatProperty>(&PropertyValue);
		OutArguments.Add(v8::Number::New(Isolate, (double)PropertyValue));
	}
	else if (Argument->IsA<UObjectPropertyBase>())
	{
		UObject* PropertyValue = nullptr;
		Stack.StepCompiledIn<UObjectPropertyBase>(&PropertyValue);
		OutArguments.Add(ReferenceClassObject(PropertyValue));
	}
	else if (auto StructArgument = Cast<UStructProperty>(Argument))
	{
		UScriptStruct* Struct = StructArgument->Struct;

		void* PropertyValue = FMemory::Malloc(Struct->GetStructureSize());
		Struct->InitializeStruct(PropertyValue);

		Stack.StepCompiledIn<UStructProperty>(PropertyValue);
		OutArguments.Add(ReferenceStructObject(PropertyValue, StructArgument->Struct));
	}
	else if (auto EnumArgument = Cast<UEnumProperty>(Argument))
	{
		PopArgumentFromStack(Stack, EnumArgument->GetUnderlyingProperty(), OutArguments);
	}
	else if (Argument->IsA<UByteProperty>())
	{
		int8 PropertyValue = 0;
		Stack.StepCompiledIn<UByteProperty>(&PropertyValue);
		OutArguments.Add(v8::Integer::New(Isolate, PropertyValue));
	}
	else if (Argument->IsA<UArrayProperty>())
	{
		Stack.StepCompiledIn<UArrayProperty>(nullptr);
		OutArguments.Add(ReadPropertyFromBuffer(Argument, Stack.MostRecentPropertyAddress));
	}
	else
	{
		UE_LOG(LogTsuRuntime, Error, TEXT("Unhandled type: %s"), *Argument->GetCPPType());
		checkNoEntry();
	}
}

bool FTsuContext::WritePropertyToContainer(UProperty* Property, v8::Local<v8::Value> Value, void* Buffer)
{
	if (Property->ArrayDim > 1)
	{
		// #todo(#mihe)
		checkNoEntry();
		return false;
	}
	else
	{
		return WritePropertyToBuffer(Property, Value, Property->ContainerPtrToValuePtr<void>(Buffer));
	}
}

bool FTsuContext::WritePropertyToBuffer(UProperty* Property, v8::Local<v8::Value> Value, void* Buffer)
{
	if (auto StrProperty = Cast<UStrProperty>(Property))
	{
		const FString String = V8_TO_TCHAR(Value.As<v8::String>());
		StrProperty->SetPropertyValue(Buffer, String);
	}
	else if (auto NameProperty = Cast<UNameProperty>(Property))
	{
		const FName Name = *V8_TO_TCHAR(Value.As<v8::String>());
		NameProperty->SetPropertyValue(Buffer, Name);
	}
	else if (auto TextProperty = Cast<UTextProperty>(Property))
	{
		const FString String = V8_TO_TCHAR(Value.As<v8::String>());
		const FText Text = FText::FromString(String);
		TextProperty->SetPropertyValue(Buffer, Text);
	}
	else if (auto BoolProperty = Cast<UBoolProperty>(Property))
	{
		BoolProperty->SetPropertyValue(Buffer, Value.As<v8::Boolean>()->Value());
	}
	else if (auto NumericProperty = Cast<UNumericProperty>(Property))
	{
		if (NumericProperty->IsInteger())
		{
			NumericProperty->SetIntPropertyValue(Buffer, (int64)Value.As<v8::Number>()->Value());
		}
		else if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(Buffer, Value.As<v8::Number>()->Value());
		}
		else
		{
			UE_LOG(LogTsuRuntime, Error, TEXT("Unhandled numeric type: %s"), *NumericProperty->GetCPPType());
			checkNoEntry();
		}
	}
	else if (auto EnumProperty = Cast<UEnumProperty>(Property))
	{
		return WritePropertyToBuffer(EnumProperty->GetUnderlyingProperty(), Value, Buffer);
	}
	else if (auto StructProperty = Cast<UStructProperty>(Property))
	{
		Value = UnwrapStructProxy(Value);

		void* Object = nullptr;
		verify(TsuContext_Private::GetInternalFields(Value, &Object));
		StructProperty->Struct->CopyScriptStruct(Buffer, Object);
	}
	else if (auto ObjectProperty = Cast<UObjectPropertyBase>(Property))
	{
		if (Value->IsNull())
		{
			ObjectProperty->SetObjectPropertyValue(Buffer, nullptr);
		}
		else
		{
			UObject* Object = nullptr;
			verify(TsuContext_Private::GetInternalFields(Value, &Object));
			ObjectProperty->SetObjectPropertyValue(Buffer, Object);
		}
	}
	else if (auto ArrayProperty = Cast<UArrayProperty>(Property))
	{
		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

		FScriptArrayHelper ArrayHelper{ArrayProperty, Buffer};

		v8::Local<v8::Array> ArrayValue = Value.As<v8::Array>();
		const int32 ArrayLength = TsuContext_Private::GetArrayLikeLength(Context, ArrayValue);
		ArrayHelper.Resize(ArrayLength);

		UProperty* ElementProperty = ArrayProperty->Inner;

		for (int32 ElementIndex = 0; ElementIndex < ArrayLength; ++ElementIndex)
		{
			v8::Local<v8::Value> ElementValue = ArrayValue->Get(Context, ElementIndex).ToLocalChecked();
			void* ElementBuffer = ArrayHelper.GetRawPtr(ElementIndex);
			WritePropertyToBuffer(ElementProperty, ElementValue, ElementBuffer);
		}
	}
	else if (auto SetProperty = Cast<USetProperty>(Property))
	{
		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

		FScriptSetHelper SetHelper{SetProperty, Buffer};

		v8::Local<v8::Set> SetValue = Value.As<v8::Set>();
		v8::Local<v8::Array> SetArray = SetValue->AsArray();

		const int32 SetSize = (int32)SetValue->Size();
		SetHelper.EmptyElements(SetSize);

		UProperty* ElementProperty = SetHelper.ElementProp;

		FDefaultConstructedPropertyElement ValueStorage(ElementProperty);
		void* ElementBuffer = ValueStorage.GetObjAddress();

		for (int32 ElementIndex = 0; ElementIndex < SetSize; ++ElementIndex)
		{
			v8::Local<v8::Value> ElementValue = SetArray->Get(Context, ElementIndex).ToLocalChecked();
			WritePropertyToBuffer(ElementProperty, ElementValue, ElementBuffer);
			SetHelper.AddElement(ElementBuffer);
		}
	}
	else if (auto MapProperty = Cast<UMapProperty>(Property))
	{
		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

		FScriptMapHelper MapHelper{MapProperty, Buffer};

		v8::Local<v8::Map> MapValue = Value.As<v8::Map>();
		v8::Local<v8::Array> MapArray = MapValue->AsArray();

		const int32 NewMapSize = (int32)MapValue->Size();
		MapHelper.EmptyValues(NewMapSize);

		UProperty* KeyProperty = MapProperty->KeyProp;
		UProperty* ValueProperty = MapProperty->ValueProp;

		FDefaultConstructedPropertyElement KeyStorage(KeyProperty);
		FDefaultConstructedPropertyElement ValueStorage(ValueProperty);

		void* KeyBuffer = KeyStorage.GetObjAddress();
		void* ValueBuffer = ValueStorage.GetObjAddress();

		for (int32 PairIndex = 0; PairIndex < NewMapSize; ++PairIndex)
		{
			const int ArrayIndex = PairIndex * 2;
			const int KeyIndex = ArrayIndex;
			const int ValueIndex = ArrayIndex + 1;

			v8::Local<v8::Value> KeyValue = MapArray->Get(Context, KeyIndex).ToLocalChecked();
			v8::Local<v8::Value> ValueValue = MapArray->Get(Context, ValueIndex).ToLocalChecked();

			WritePropertyToBuffer(KeyProperty, KeyValue, KeyBuffer);
			WritePropertyToBuffer(ValueProperty, ValueValue, ValueBuffer);

			MapHelper.AddPair(KeyBuffer, ValueBuffer);
		}
	}
	else if (auto DelegateProperty = Cast<UDelegateProperty>(Property))
	{
		UObject* Parent = DelegateProperty->GetOuter();

		auto Event = NewObject<UTsuDelegateEvent>();
		Event->Initialize(Value.As<v8::Function>(), DelegateProperty->SignatureFunction);

		DelegateEvents.FindOrAdd(Parent).FindOrAdd(NextDelegateHandle++) = Event;

		FScriptDelegate Delegate;
		Delegate.BindUFunction(Event, NameEventExecute);

		DelegateProperty->SetPropertyValue(Buffer, Delegate);
	}
	else if (auto MulticastDelegateProperty = Cast<UMulticastDelegateProperty>(Property))
	{
		UObject* Parent = MulticastDelegateProperty->GetOuter();

		auto Event = NewObject<UTsuDelegateEvent>();
		Event->Initialize(Value.As<v8::Function>(), MulticastDelegateProperty->SignatureFunction);

		DelegateEvents.FindOrAdd(Parent).FindOrAdd(NextDelegateHandle++) = Event;

		FMulticastScriptDelegate MulticastDelegate;
		FScriptDelegate Delegate;
		Delegate.BindUFunction(Event, NameEventExecute);
		MulticastDelegate.Add(Delegate);

		MulticastDelegateProperty->SetPropertyValue(Buffer, MulticastDelegate);
	}
	else
	{
		UE_LOG(LogTsuRuntime, Error, TEXT("Unhandled type: %s"), *Property->GetCPPType());
		checkNoEntry();
		return false;
	}

	return true;
}

v8::Local<v8::Value> FTsuContext::ReadPropertyFromContainer(UProperty* Property, const void* Buffer)
{
	if (Property->ArrayDim > 1)
	{
		v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

		v8::Local<v8::Array> Array = v8::Array::New(Isolate, Property->ArrayDim);

		for (int32 ElementIndex = 0; ElementIndex < Property->ArrayDim; ++ElementIndex)
		{
			auto ElementBuffer = Property->ContainerPtrToValuePtr<void>(Buffer, ElementIndex);
			v8::Local<v8::Value> ElementValue = ReadPropertyFromBuffer(Property, ElementBuffer);
			Array->Set(Context, (uint32_t)ElementIndex, ElementValue).ToChecked();
		}

		return Array;
	}
	else
	{
		return ReadPropertyFromBuffer(Property, Property->ContainerPtrToValuePtr<void>(Buffer));
	}
}

v8::Local<v8::Value> FTsuContext::ReadPropertyFromBuffer(UProperty* Property, const void* Buffer)
{
	v8::Local<v8::Context> Context = GlobalContext.Get(Isolate);

	if (auto StrProperty = Cast<UStrProperty>(Property))
	{
		return TCHAR_TO_V8(StrProperty->GetPropertyValue(Buffer));
	}
	else if (auto NameProperty = Cast<UNameProperty>(Property))
	{
		return TCHAR_TO_V8(NameProperty->GetPropertyValue(Buffer).ToString());
	}
	else if (auto TextProperty = Cast<UTextProperty>(Property))
	{
		return TCHAR_TO_V8(TextProperty->GetPropertyValue(Buffer).ToString());
	}
	else if (auto BoolProperty = Cast<UBoolProperty>(Property))
	{
		return v8::Boolean::New(Isolate, BoolProperty->GetPropertyValue(Buffer));
	}
	else if (auto NumericProperty = Cast<UNumericProperty>(Property))
	{
		if (NumericProperty->IsInteger())
			return v8::Number::New(Isolate, (double)NumericProperty->GetSignedIntPropertyValue(Buffer));
		else if (NumericProperty->IsFloatingPoint())
			return v8::Number::New(Isolate, (double)NumericProperty->GetFloatingPointPropertyValue(Buffer));
	}
	else if (auto EnumProperty = Cast<UEnumProperty>(Property))
	{
		return ReadPropertyFromBuffer(EnumProperty->GetUnderlyingProperty(), Buffer);
	}
	else if (auto StructProperty = Cast<UStructProperty>(Property))
	{
		UScriptStruct* Type = StructProperty->Struct;

		void* Object = FMemory::Malloc(Type->GetStructureSize());
		Type->InitializeStruct(Object);
		Type->CopyScriptStruct(Object, Buffer);

		return ReferenceStructObject(Object, Type);
	}
	else if (auto ObjectProperty = Cast<UObjectPropertyBase>(Property))
	{
		return ReferenceClassObject(ObjectProperty->GetObjectPropertyValue(Buffer));
	}
	else if (auto ArrayProperty = Cast<UArrayProperty>(Property))
	{
		if (ArrayProperty->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
		{
			FScriptArrayHelper ArrayHelper{ArrayProperty, Buffer};
			v8::Local<v8::Array> Array = v8::Array::New(Isolate, ArrayHelper.Num());
			UProperty* ElementProperty = ArrayProperty->Inner;

			for (int32 ElementIndex = 0; ElementIndex < ArrayHelper.Num(); ++ElementIndex)
			{
				const void* ElementBuffer = ArrayHelper.GetRawPtr(ElementIndex);
				v8::Local<v8::Value> ElementValue = ReadPropertyFromBuffer(ElementProperty, ElementBuffer);
				Array->Set(Context, ElementIndex, ElementValue).ToChecked();
			}

			return Array;
		}
		else
		{
			v8::Local<v8::Function> Constructor = GlobalArrayConstructor.Get(Isolate);

			v8::Local<v8::Object> Instance = Constructor->NewInstance(Context).ToLocalChecked();
			Instance->SetAlignedPointerInInternalField(0, ArrayProperty);
			Instance->SetAlignedPointerInInternalField(1, const_cast<void*>(Buffer));

			v8::Local<v8::Function> HandlerConstructor = GlobalArrayHandlerConstructor.Get(Isolate);
			v8::Local<v8::Value> HandlerArgs[] = {Instance};
			v8::Local<v8::Object> Handler = HandlerConstructor->NewInstance(
				Context,
				ARRAY_COUNT(HandlerArgs),
				HandlerArgs
			).ToLocalChecked();

			v8::Local<v8::Proxy> Proxy = v8::Proxy::New(
				Context,
				v8::Array::New(Isolate),
				Handler
			).ToLocalChecked();

			return Proxy;
		}
	}
	else if (auto SetProperty = Cast<USetProperty>(Property))
	{
		FScriptSetHelper SetHelper{SetProperty, Buffer};
		v8::Local<v8::Set> Set = v8::Set::New(Isolate);
		UProperty* ElementProperty = SetProperty->ElementProp;

		for (int32 ElementIndex = 0; ElementIndex < SetHelper.Num(); ++ElementIndex)
		{
			const void* ElementBuffer = SetHelper.GetElementPtr(ElementIndex);
			v8::Local<v8::Value> ElementValue = ReadPropertyFromBuffer(ElementProperty, ElementBuffer);
			Set->Add(Context, ElementValue).ToLocalChecked();
		}

		return Set;
	}
	else if (auto MapProperty = Cast<UMapProperty>(Property))
	{
		FScriptMapHelper MapHelper{MapProperty, Buffer};

		v8::Local<v8::Map> Map = v8::Map::New(Isolate);

		UProperty* KeyProperty = MapHelper.GetKeyProperty();
		UProperty* ValueProperty = MapHelper.GetValueProperty();

		for (int32 ElementIndex = 0; ElementIndex < MapHelper.Num(); ++ElementIndex)
		{
			const void* KeyBuffer = MapHelper.GetKeyPtr(ElementIndex);
			const void* ValueBuffer = MapHelper.GetValuePtr(ElementIndex);

			v8::Local<v8::Value> ElementKey = ReadPropertyFromBuffer(KeyProperty, KeyBuffer);
			v8::Local<v8::Value> ElementValue = ReadPropertyFromBuffer(ValueProperty, ValueBuffer);

			Map->Set(Context, ElementKey, ElementValue).ToLocalChecked();
		}

		return Map;
	}

	UE_LOG(LogTsuRuntime, Error, TEXT("Unhandled type: %s"), *Property->GetCPPType());
	checkNoEntry();
	return {};
}