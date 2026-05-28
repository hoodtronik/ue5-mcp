using UnrealBuildTool;

public class BlueprintMCP : ModuleRules
{
	public BlueprintMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"Json",
			"JsonUtilities",
			"HTTPServer",
			"Sockets",
			"Networking",
			// CLAUDE-NOTE: Niagara runtime modules — needed for UNiagaraSystem/UNiagaraEmitter
			// references in handler code. NiagaraEditor is editor-only (see Private list).
			"Niagara",
			"NiagaraCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"AssetTools",
			"Kismet",
			"KismetCompiler",
			"EditorSubsystem",
			"MaterialEditor",
			"AnimGraph",
			"AnimGraphRuntime",
			"RHI",
			"Slate",
			"UMG",
			"UMGEditor",
			"SlateCore",
			// CLAUDE-NOTE: NiagaraEditor exposes factories, the stack ViewModel, and module
			// library helpers — all editor-only. Wrap usage with WITH_EDITOR.
			"NiagaraEditor"
		});
	}
}
