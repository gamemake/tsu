using UnrealBuildTool;

public class TsuInspector : ModuleRules
{
	public TsuInspector( ReadOnlyTargetRules Target ) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "TsuWebSocket",
                "TsuRuntime",
                "V8"
            }
        );

		PrivateDependencyModuleNames.AddRange(
			new string[] 
			{
                "Core",
                "CoreUObject",
                "Engine",
                "zlib"
            }
		);
	}
}
