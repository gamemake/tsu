using UnrealBuildTool;

public class TsuWebSocket : ModuleRules
{
	public TsuWebSocket( ReadOnlyTargetRules Target ) : base(Target)
	{
        PrivatePCHHeaderFile = "Private/TsuWebSocketPrivatePCH.h";

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "libWebSockets"
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
