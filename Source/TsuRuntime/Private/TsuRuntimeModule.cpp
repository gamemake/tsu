#include "TsuRuntimeModule.h"

#include "TsuBlueprint.h"
#include "TsuContext.h"
#include "TsuPaths.h"
#include "TsuRuntimeBlueprintCompiler.h"

#if WITH_EDITOR
#include "TsuRuntimeSettings.h"
#endif // WITH_EDITOR

#include "Editor.h"
#include "Engine/Engine.h"
#include "GameDelegates.h"
#include "HAL/PlatformProcess.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif // WITH_EDITOR

#include "TsuRuntimeLog.h"
#include "TsuIsolate.h"

class FTsuRuntimeModule final
	: public ITsuRuntimeModule
{
public:
	void StartupModule() override
	{
		UE_LOG(LogTsuRuntime, Log, TEXT("V8 Version %d.%d.%d.%d"),
			V8_MAJOR_VERSION, V8_MINOR_VERSION, V8_BUILD_NUMBER, V8_PATCH_LEVEL);
		DelayLoadDLL();

		FTsuIsolate::Initialize();

		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FTsuRuntimeModule::OnPostEngineInit);
	}

	void ShutdownModule() override
	{
		RemoveCleanupDelegates();
		UnregisterSettings();

		FTsuIsolate::Uninitialize();

		FreeDLL();
	}

private:
	void DelayLoadDLL()
	{
#ifdef TSU_DLL_DELAY_LOAD
		auto V8DllDir = FTsuPaths::V8DllDir();

		FPlatformProcess::PushDllDirectory(*V8DllDir);
		HandleV8 = FPlatformProcess::GetDllHandle(*FPaths::V8DllPath(TEXT("v8")));
		check(HandleV8);
		HandleV8LibBase = FPlatformProcess::GetDllHandle(*FPaths::V8DllPath(TEXT("v8_libbase")));
		check(HandleV8LibBase);
		HandleV8LibPlatform = FPlatformProcess::GetDllHandle(*FPaths::V8DllPath(TEXT("v8_libplatform")));
		check(HandleV8LibPlatform);
		FPlatformProcess::PopDllDirectory(*V8DllDir);
#endif
	}

	void FreeDLL()
	{
#ifdef TSU_DLL_DELAY_LOAD
		FPlatformProcess::FreeDllHandle(HandleV8LibPlatform);
		FPlatformProcess::FreeDllHandle(HandleV8LibBase);
		FPlatformProcess::FreeDllHandle(HandleV8);
#endif
	}

	void RegisterSettings()
	{
#if WITH_EDITOR
		if (auto SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings(
				TEXT("Project"), // Container
				TEXT("Plugins"), // Category
				TEXT("TSU"), // Section
				FText::FromString(TEXT("TypeScript for Unreal")), // DisplayName
				FText::FromString(TEXT("Configure TypeScript for Unreal")), // Description
				GetMutableDefault<UTsuRuntimeSettings>());
		}
#endif // WITH_EDITOR
	}

	void UnregisterSettings()
	{
#if WITH_EDITOR
		if (auto SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings(
				TEXT("Project"), // Container
				TEXT("Plugins"), // Category
				TEXT("TSU")); // Section
		}
#endif // WITH_EDITOR
	}

	void OnPostEngineInit()
	{
		FCoreDelegates::OnPostEngineInit.RemoveAll(this);

		if (!GEditor)
		{
			FKismetCompilerContext::RegisterCompilerForBP(
				UTsuBlueprint::StaticClass(),
				&FTsuRuntimeModule::MakeCompiler);
		}

		RegisterSettings();
		FTsuContext::Get();
		AddCleanupDelegates();
	}

	static TSharedPtr<FKismetCompilerContext> MakeCompiler(
		UBlueprint* InBlueprint,
		FCompilerResultsLog& InMessageLog,
		const FKismetCompilerOptions& InCompileOptions)
	{
		return MakeShared<FTsuRuntimeBlueprintCompiler>(
			CastChecked<UTsuBlueprint>(InBlueprint),
			InMessageLog,
			InCompileOptions);
	}

	void AddCleanupDelegates()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			HandlePreCompile = GEditor->OnBlueprintPreCompile().AddLambda(
				[](UBlueprint* /*Blueprint*/)
				{
					FTsuContext::Destroy();
				});
		}

		HandleBeginPIE = FEditorDelegates::BeginPIE.AddLambda(
			[](bool /*bIsSimulating*/)
			{
				FTsuContext::Destroy();
			});
#endif // WITH_EDITOR

		HandleWorldDestroyed = GEngine->OnWorldDestroyed().AddLambda(
			[](UWorld* /*World*/)
			{
				FTsuContext::Destroy();
			});

		HandleEndPlayMap = FGameDelegates::Get().GetEndPlayMapDelegate().AddLambda(
			[]
			{
				FTsuContext::Destroy();
			});

		HandlePreExit = FCoreDelegates::OnPreExit.AddLambda(
			[]
			{
				FTsuContext::Destroy();
			});
	}

	void RemoveCleanupDelegates()
	{
#if WITH_EDITOR
		if (GEditor)
			GEditor->OnBlueprintPreCompile().Remove(HandlePreCompile);

		FEditorDelegates::BeginPIE.Remove(HandleBeginPIE);
#endif // WITH_EDITOR

		if (GEngine)
			GEngine->OnWorldDestroyed().Remove(HandleWorldDestroyed);

		FGameDelegates::Get().GetEndPlayMapDelegate().Remove(HandleEndPlayMap);

		FCoreDelegates::OnPreExit.Remove(HandlePreExit);
	}

	void* HandleV8 = nullptr;
	void* HandleV8LibBase = nullptr;
	void* HandleV8LibPlatform = nullptr;

	FDelegateHandle HandlePreCompile;
	FDelegateHandle HandleBeginPIE;
	FDelegateHandle HandleWorldDestroyed;
	FDelegateHandle HandleEndPlayMap;
	FDelegateHandle HandlePreExit;
};

IMPLEMENT_MODULE(FTsuRuntimeModule, TsuRuntime)
