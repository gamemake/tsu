using System;
using System.IO;
using UnrealBuildTool;

public class V8 : ModuleRules
{
	public V8(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		var ModulePath = UnrealBuildTool.RulesCompiler.GetFileNameFromType(GetType());
		var ModuleBaseDirectory = Path.GetDirectoryName(ModulePath);

		var DLLPrefix = "lib";
		var DLLSuffix = ".so";
		var LibPrefix = "lib";
		var LibSuffix = ".a";
		var IsWindows = false;
		var IsDynamic = true;
		var IsDebug = false;
		var Configuration = "Release";
		var LibraryBase = Target.Platform.ToString();

		switch (Target.Configuration)
		{
			case UnrealTargetConfiguration.Debug:
				IsDynamic = true;
				IsDebug = true;
				Configuration = "Release";
				break;
			case UnrealTargetConfiguration.DebugGame:
				IsDynamic = true;
				IsDebug = true;
				Configuration = "Release";
				break;
			case UnrealTargetConfiguration.Development:
				IsDynamic = true;
				Configuration = "Release";
				break;
			case UnrealTargetConfiguration.Shipping:
				IsDynamic = false;
				Configuration = "Shipping";
				break;
			case UnrealTargetConfiguration.Test:
				IsDynamic = false;
				Configuration = "Shipping";
				break;
		}

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
		{
			DLLPrefix = "";
			DLLSuffix = ".dll";
			LibPrefix = "";
			LibSuffix = ".lib";
			IsWindows = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			DLLSuffix = ".dylib";
			if (Configuration == "Release")
			{
				IsDynamic = false;
				Configuration = "Shipping";
			}
		}
		else  if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			DLLSuffix = ".dylib";
			IsDynamic = false;
			if (Configuration == "Shipping")
			{
				Configuration = "Release";
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			LibraryBase = Path.Combine(
				"Android",
				Target.Architecture
			);
		}

		if (IsDynamic)
		{
			PublicDefinitions.AddRange(new string[] {
//				"USING_V8_SHARED",
				"V8_DEPRECATION_WARNINGS",
				"V8_IMMINENT_DEPRECATION_WARNINGS"
			});
		}
		else
		{
			PublicDefinitions.AddRange(new string[] {
				"V8_DEPRECATION_WARNINGS",
				"V8_IMMINENT_DEPRECATION_WARNINGS"
			});
		}

		var IncludeDirectory = Path.Combine(ModuleBaseDirectory, "Include");
		PublicSystemIncludePaths.Add(IncludeDirectory);

		var LibraryDirectory = Path.Combine(
			ModuleBaseDirectory,
			"Lib",
			LibraryBase,
			Configuration
		);

		if (IsDynamic)
		{
			var DLLs = new string[]
			{
				"v8",
				"v8_libbase",
				"v8_libplatform"
			};

			if (IsWindows)
			{
				PublicLibraryPaths.Add(LibraryDirectory);
				foreach (var DLL in DLLs)
				{
					var LibName = string.Format("{0}{1}.dll{2}", LibPrefix, DLL, LibSuffix);
					PublicAdditionalLibraries.Add(LibName);
					var DLLName = string.Format("{0}{1}{2}", DLLPrefix, DLL, DLLSuffix);
					PublicDelayLoadDLLs.Add(DLLName);
				}
				PrivateDefinitions.Add("TSU_DLL_DELAY_LOAD");
			}
			else 
			{
				foreach (var DLL in DLLs)
				{
					var DLLName = string.Format("{0}{1}{2}", DLLPrefix, DLL, DLLSuffix);
					PublicDelayLoadDLLs.Add(Path.Combine(LibraryDirectory, DLLName));
				}
			}
		}
		else
		{
			PublicAdditionalLibraries.Add(Path.Combine(
				LibraryDirectory,
				LibPrefix + "v8_monolith" + LibSuffix
			));
		}
	}
}
