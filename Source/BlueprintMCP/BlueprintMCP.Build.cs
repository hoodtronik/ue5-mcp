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
			"Networking"
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
			"HairStrandsCore",
			// CLAUDE-NOTE: added for the Voxel Sandbox -> StaticMesh baker (BlueprintMCPVoxelBaker.cpp).
			"ProceduralMeshComponent",
			"MeshDescription",
			"StaticMeshDescription",
			// CLAUDE-NOTE: added for the run_python bridge (BlueprintMCPHandlers_Python.cpp).
			"PythonScriptPlugin",
			// CLAUDE-NOTE: added for PCG graph authoring endpoints (BlueprintMCPHandlers_PCG.cpp) —
			// graph user-parameters + override-pin binding via the PCG editor C++ API.
			"PCG"
		});
	}
}
