#include "TsuPaths.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

FString FTsuPaths::PluginDir()
{
	static const FString Result = []
	{
		TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("TSU"));
		verify(ThisPlugin.IsValid());
		return ThisPlugin->GetBaseDir();
	}();

	return Result;
}

FString FTsuPaths::BinariesDir()
{
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
	return FPaths::Combine(
		PluginDir(),
		TEXT("Binaries"),
		TEXT("Win64")
	);
#else // PLATFORM_64BITS
	return FPaths::Combine(
		PluginDir(),
		TEXT("Binaries"),
		TEXT("Win32")
	);
#endif // PLATFORM_64BITS
#elif PLATFORM_MAC
	return FPaths::Combine(
		PluginDir(),
		TEXT("Binaries"),
		TEXT("Mac")
	);
#elif PLATFORM_LINUX
	return FPaths::Combine(
		PluginDir(),
		TEXT("Binaries"),
		TEXT("Linux")
	);
#else
	check(!"Unsupport OS");
	return TEXT("");
#endif
}

FString FTsuPaths::V8DllDir()
{
	return FPaths::Combine(
		FTsuPaths::PluginDir(),
		TEXT("Binaries"),
		TEXT("ThirdParty"),
		TEXT("V8"),
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
			TEXT("Win64")
#else // PLATFORM_64BITS
			TEXT("Win32")
#endif // PLATFORM_64BITS
#elif PLATFORM_MAC
		TEXT("Mac")
#elif PLATFORM_MAC
		TEXT("Linux")
#else
	#error Not implemented
#endif // PLATFORM_WINDOWS
	);
}

FString FTsuPaths::V8DllPath(const TCHAR* FileName)
{
	return FPaths::Combine(
		V8DllDir(),
#if PLATFORM_WINDOWS
		FString::Printf(TEXT("%s.dll"), FileName)
#elif PLATFORM_MAC
		FString::Printf(TEXT("lib%s.dylib"), FileName)
#endif // PLATFORM_WINDOWS
	);
}

FString FTsuPaths::ContentDir()
{
	return FPaths::Combine(PluginDir(), TEXT("Content/"));
}

FString FTsuPaths::SourceDir()
{
	return FPaths::Combine(PluginDir(), TEXT("Source/"));
}

FString FTsuPaths::ScriptsDir()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Scripts/"));
}

FString FTsuPaths::ScriptsSourceDir()
{
	return FPaths::Combine(ScriptsDir(), TEXT("Source/"));
}

FString FTsuPaths::ParserPath()
{
#if PLATFORM_WINDOWS
	return FPaths::Combine(BinariesDir(), TEXT("TsuParser.exe"));
#else
	return FPaths::Combine(BinariesDir(), TEXT("TsuParser"));
#endif
}

FString FTsuPaths::BootstrapPath()
{
	return FPaths::Combine(SourceDir(), TEXT("TsuBootstrap"), TEXT("dist"));
}

FString FTsuPaths::TypingsDir()
{
	return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Typings/"));
}

FString FTsuPaths::TypingPath(const TCHAR* TypeName)
{
	return FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("Typings"),
		TypeName,
		TEXT("index.d.ts"));
}
