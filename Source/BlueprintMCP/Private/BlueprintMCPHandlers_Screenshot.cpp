#include "BlueprintMCPServer.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Blueprint.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#include "HighResScreenshot.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "GraphEditor.h"
#include "SGraphPanel.h"
#include "Widgets/SVirtualWindow.h"
#include "Slate/WidgetRenderer.h"
#include "Layout/WidgetPath.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EdGraph/EdGraph.h"

// ============================================================
// HandleTakeScreenshot — capture a viewport screenshot
// ============================================================

FString FBlueprintMCPServer::HandleTakeScreenshot(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: take_screenshot()"));

	if (!bIsEditor)
	{
		return MakeErrorJson(TEXT("take_screenshot requires editor mode."));
	}

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available."));
	}

	FString Filename;
	if (!Json->TryGetStringField(TEXT("filename"), Filename) || Filename.IsEmpty())
	{
		Filename = FString::Printf(TEXT("Screenshot_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}

	// Ensure .png extension
	if (!Filename.EndsWith(TEXT(".png")))
	{
		Filename += TEXT(".png");
	}

	// Output directory
	FString OutputDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
	FString FullPath = OutputDir / Filename;

	// Prefer the PIE game viewport when playing — that's where gameplay (and the
	// framing component) actually renders. Falls back to the editor level viewport.
	FViewport* Viewport = nullptr;
	if (GEditor->PlayWorld && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Viewport = GEngine->GameViewport->Viewport;
	}
	else if (GEditor->GetLevelViewportClients().Num() > 0 && GEditor->GetLevelViewportClients()[0])
	{
		Viewport = GEditor->GetLevelViewportClients()[0]->Viewport;
	}

	if (!Viewport)
	{
		return MakeErrorJson(TEXT("No active viewport found."));
	}

	// Read pixels from viewport
	TArray<FColor> Bitmap;
	int32 Width = Viewport->GetSizeXY().X;
	int32 Height = Viewport->GetSizeXY().Y;

	if (Width <= 0 || Height <= 0)
	{
		return MakeErrorJson(TEXT("Viewport has invalid dimensions."));
	}

	bool bReadSuccess = Viewport->ReadPixels(Bitmap);
	if (!bReadSuccess || Bitmap.Num() == 0)
	{
		return MakeErrorJson(TEXT("Failed to read pixels from viewport."));
	}

	// Save as PNG (PNGCompressImageArray requires TArray64 in UE 5.7)
	TArray64<uint8> PngData;
	FImageUtils::PNGCompressImageArray(Width, Height, Bitmap, PngData);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	bool bSaved = FFileHelper::SaveArrayToFile(PngData, *FullPath);
	if (!bSaved)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to save screenshot to '%s'."), *FullPath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("filename"), Filename);
	Result->SetStringField(TEXT("fullPath"), FullPath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: Screenshot saved to '%s' (%dx%d)"), *FullPath, Width, Height);

	return JsonToString(Result);
}

// ============================================================
// HandleTakeHighResScreenshot — capture a high-resolution screenshot
// ============================================================

FString FBlueprintMCPServer::HandleTakeHighResScreenshot(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: take_high_res_screenshot()"));

	if (!bIsEditor)
	{
		return MakeErrorJson(TEXT("take_high_res_screenshot requires editor mode."));
	}

	if (!GEditor)
	{
		return MakeErrorJson(TEXT("Editor not available."));
	}

	double ResMultiplier = 2.0;
	Json->TryGetNumberField(TEXT("resolutionMultiplier"), ResMultiplier);
	if (ResMultiplier < 1.0) ResMultiplier = 1.0;
	if (ResMultiplier > 8.0) ResMultiplier = 8.0;

	FString Filename;
	if (!Json->TryGetStringField(TEXT("filename"), Filename) || Filename.IsEmpty())
	{
		Filename = FString::Printf(TEXT("HighRes_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}

	if (!Filename.EndsWith(TEXT(".png")))
	{
		Filename += TEXT(".png");
	}

	FString OutputDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
	FString FullPath = OutputDir / Filename;

	FLevelEditorViewportClient* ViewportClient = nullptr;
	if (GEditor->GetLevelViewportClients().Num() > 0)
	{
		ViewportClient = GEditor->GetLevelViewportClients()[0];
	}

	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return MakeErrorJson(TEXT("No active viewport found."));
	}

	// Configure high-res screenshot settings
	FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
	Config.SetResolution(
		ViewportClient->Viewport->GetSizeXY().X,
		ViewportClient->Viewport->GetSizeXY().Y,
		ResMultiplier
	);
	Config.SetFilename(FullPath);
	Config.bMaskEnabled = false;

	// Request the screenshot
	ViewportClient->Viewport->TakeHighResScreenShot();

	int32 FinalWidth = FMath::CeilToInt(ViewportClient->Viewport->GetSizeXY().X * ResMultiplier);
	int32 FinalHeight = FMath::CeilToInt(ViewportClient->Viewport->GetSizeXY().Y * ResMultiplier);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("filename"), Filename);
	Result->SetStringField(TEXT("fullPath"), FullPath);
	Result->SetNumberField(TEXT("resolutionMultiplier"), ResMultiplier);
	Result->SetNumberField(TEXT("estimatedWidth"), FinalWidth);
	Result->SetNumberField(TEXT("estimatedHeight"), FinalHeight);
	Result->SetStringField(TEXT("note"), TEXT("High-res screenshot is captured asynchronously. The file may take a moment to appear on disk."));

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: High-res screenshot requested at %dx multiplier -> '%s'"), (int32)ResMultiplier, *FullPath);

	return JsonToString(Result);
}

// ============================================================
// HandleScreenshotGraph — render a Blueprint graph (not the 3D viewport) to PNG
// ============================================================
// CLAUDE-NOTE: github.com/mirno-ehf/ue5-mcp#65. Renders an SGraphEditor off-screen via
// FWidgetRenderer/SVirtualWindow — the same headless-safe mechanism UE's own Content Browser
// thumbnail renderers use (WidgetBlueprintThumbnailRenderer.cpp), not FWidgetSnapshotService
// (which only captures already-visible native OS windows and can't target a widget built on the
// fly). No live Blueprint Editor tab is required — AssetEditorToolkit is left unset. SEH-wrapped
// since this is genuinely novel Slate/rendering code in this codebase with no prior art to lean on.

namespace
{
	bool ScreenshotGraphInner(UEdGraph* EdGraph, int32 Width, int32 Height, TArray64<uint8>& OutPngData)
	{
		TSharedRef<SGraphEditor> GraphEditorWidget = SNew(SGraphEditor)
			.GraphToEdit(EdGraph)
			.IsEditable(false)
			.DisplayAsReadOnly(true);

		// CLAUDE-NOTE: SGraphPanel only builds its child SGraphNode widgets reactively, in
		// SGraphPanel::Update() — fired off the OnGraphChanged delegate or a Tick's deferred-update
		// flag, never from Construct() itself. With no running Slate app tick loop driving this
		// off-screen SVirtualWindow, Update() would never run on its own and the panel stays
		// permanently empty (verified live: zoom/pan changed correctly but the canvas stayed blank
		// with 0 node widgets). Force it explicitly before layout/paint.
		if (SGraphPanel* GraphPanel = GraphEditorWidget->GetGraphPanel())
		{
			GraphPanel->Update();
		}

		GraphEditorWidget->SlatePrepass(1.0f);

		// CLAUDE-NOTE: SGraphPanel's ZoomToFit() only *schedules* the fit — it registers a Slate
		// ActiveTimer that interpolates view offset/zoom across several real application ticks
		// (SNodePanel::HandleZoomToFit). There's no window and no running Slate app tick loop here
		// (FWidgetRenderer draws this SVirtualWindow exactly once with DeltaTime=0), so the timer
		// never advances and the capture came out at the default 1:1 view showing nothing (verified
		// live against a 6-node test graph). Compute the fit ourselves from node positions and set
		// the view synchronously with SetViewLocation instead.
		float MinX = TNumericLimits<float>::Max();
		float MinY = TNumericLimits<float>::Max();
		float MaxX = TNumericLimits<float>::Lowest();
		float MaxY = TNumericLimits<float>::Lowest();
		bool bHasNodes = false;
		for (UEdGraphNode* Node : EdGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			bHasNodes = true;
			MinX = FMath::Min(MinX, Node->GetNodePosX());
			MinY = FMath::Min(MinY, Node->GetNodePosY());
			// Real node widths aren't known until laid out; a generous minimum keeps single-node
			// graphs from zooming in absurdly tight.
			MaxX = FMath::Max(MaxX, Node->GetNodePosX() + FMath::Max(Node->GetWidth(), 250.0f));
			MaxY = FMath::Max(MaxY, Node->GetNodePosY() + FMath::Max(Node->GetHeight(), 150.0f));
		}

		if (bHasNodes)
		{
			constexpr float Padding = 100.0f;
			MinX -= Padding; MinY -= Padding; MaxX += Padding; MaxY += Padding;
			const float BoundsWidth = FMath::Max(MaxX - MinX, 1.0f);
			const float BoundsHeight = FMath::Max(MaxY - MinY, 1.0f);
			const float FitZoom = FMath::Min((float)Width / BoundsWidth, (float)Height / BoundsHeight);
			const float ZoomAmount = FMath::Clamp(FitZoom, 0.1f, 1.0f);
			// CLAUDE-NOTE: SGraphEditor::GetViewLocation() returns GraphPanel->GetViewOffset()
			// verbatim, and SGraphPanel::GraphCoordToPanelCoord() is (GraphCoord - ViewOffset) *
			// Zoom — so "Location" is the graph-space coordinate that lands at the viewport's
			// TOP-LEFT corner, not the center. Passing the bounds' center here (an earlier attempt)
			// left the actual nodes off-screen to the bottom-right; verified live before landing on
			// this fix. Pass the padded bounds' top-left instead.
			GraphEditorWidget->SetViewLocation(FVector2D(MinX, MinY), ZoomAmount);
		}

		FWidgetRenderer Renderer(/*bUseGammaCorrection=*/true);
		UTextureRenderTarget2D* RenderTarget = Renderer.DrawWidget(GraphEditorWidget, FVector2D(Width, Height));
		if (!RenderTarget)
		{
			return false;
		}

		FRenderTarget* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
		if (!RTResource)
		{
			return false;
		}

		TArray<FColor> Bitmap;
		if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
		{
			return false;
		}

		FImageUtils::PNGCompressImageArray(Width, Height, Bitmap, OutPngData);
		return OutPngData.Num() > 0;
	}
}

int32 TryScreenshotGraphSEH(UEdGraph* EdGraph, int32 Width, int32 Height, TArray64<uint8>* OutPngData, bool* bOutSuccess)
{
	__try
	{
		*bOutSuccess = ScreenshotGraphInner(EdGraph, Width, Height, *OutPngData);
		return 0;
	}
	__except (1)
	{
		*bOutSuccess = false;
		return -1;
	}
}

FString FBlueprintMCPServer::HandleScreenshotGraph(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString BlueprintName = Json->GetStringField(TEXT("blueprint"));
	FString GraphName = Json->GetStringField(TEXT("graph"));
	if (BlueprintName.IsEmpty() || GraphName.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required fields: blueprint, graph"), MCPErrorCodes::InvalidInput);
	}

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: screenshot_graph('%s', '%s')"), *BlueprintName, *GraphName);

	if (!bIsEditor)
	{
		return MakeErrorJson(TEXT("screenshot_graph requires editor mode."));
	}

	FString LoadError;
	UBlueprint* BP = LoadBlueprintByName(BlueprintName, LoadError);
	if (!BP)
	{
		return MakeErrorJson(LoadError);
	}

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		return MakeErrorJson(FString::Printf(TEXT("Graph '%s' not found in Blueprint '%s'"), *GraphName, *BlueprintName), MCPErrorCodes::NotFound);
	}

	int32 Width = 1600;
	int32 Height = 1200;
	Json->TryGetNumberField(TEXT("width"), Width);
	Json->TryGetNumberField(TEXT("height"), Height);
	Width = FMath::Clamp(Width, 256, 8192);
	Height = FMath::Clamp(Height, 256, 8192);

	FString Filename;
	if (!Json->TryGetStringField(TEXT("filename"), Filename) || Filename.IsEmpty())
	{
		Filename = FString::Printf(TEXT("Graph_%s_%s_%s"), *BlueprintName, *GraphName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}
	if (!Filename.EndsWith(TEXT(".png")))
	{
		Filename += TEXT(".png");
	}

	FString OutputDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
	FString FullPath = OutputDir / Filename;

	TArray64<uint8> PngData;
	bool bRenderSuccess = false;
	int32 SEHCode = TryScreenshotGraphSEH(TargetGraph, Width, Height, &PngData, &bRenderSuccess);
	if (SEHCode != 0)
	{
		return MakeErrorJson(TEXT("screenshot_graph crashed while rendering the graph (SEH exception caught)."), MCPErrorCodes::OperationFailed);
	}
	if (!bRenderSuccess)
	{
		return MakeErrorJson(TEXT("Failed to render the graph to an image."), MCPErrorCodes::OperationFailed);
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	bool bSaved = FFileHelper::SaveArrayToFile(PngData, *FullPath);
	if (!bSaved)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to save graph screenshot to '%s'."), *FullPath));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint"), BlueprintName);
	Result->SetStringField(TEXT("graph"), GraphName);
	Result->SetStringField(TEXT("filename"), Filename);
	Result->SetStringField(TEXT("fullPath"), FullPath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: Graph screenshot saved to '%s' (%dx%d)"), *FullPath, Width, Height);

	return JsonToString(Result);
}
