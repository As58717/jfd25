using UnrealBuildTool;
using System.Collections.Generic;

public class kjkkkTarget : TargetRules
{
    public kjkkkTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        ExtraModuleNames.AddRange(new string[] { "kjkkk" });
    }
}
