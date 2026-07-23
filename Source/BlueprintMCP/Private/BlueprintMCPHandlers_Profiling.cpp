// Minimal profiling capability (scoped-down version of the "performance/profiling service"
// VibeUE-port idea — full trace-capture is out of scope, this is a one-shot frame-timing
// snapshot). Read-only, no mutation, no new module dependency risk beyond RenderCore, which
// Engine already pulls in transitively — added explicitly to Build.cs for clarity.

#include "BlueprintMCPServer.h"
#include "RenderTimer.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================
// HandleGetFrameTiming — one-shot game/render/RHI thread timing snapshot
// ============================================================

FString FBlueprintMCPServer::HandleGetFrameTiming(const FString& Body)
{
	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: get_frame_timing()"));

	// CLAUDE-NOTE: these globals (RenderCore/Public/RenderTimer.h) are set once per frame in
	// FViewport::Draw and hold cycle counts, not milliseconds — FPlatformTime::ToMilliseconds
	// does the conversion. This is a SNAPSHOT of whatever the last drawn frame measured, not a
	// live/streaming trace; there is no averaging or history. A headless commandlet with no
	// viewport draw calls will report zeros, not an error, since these are just global counters.
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("gameThreadMs"), FPlatformTime::ToMilliseconds(GGameThreadTime));
	Result->SetNumberField(TEXT("gameThreadWaitMs"), FPlatformTime::ToMilliseconds(GGameThreadWaitTime));
	Result->SetNumberField(TEXT("renderThreadMs"), FPlatformTime::ToMilliseconds(GRenderThreadTime));
	Result->SetNumberField(TEXT("renderThreadWaitMs"), FPlatformTime::ToMilliseconds(GRenderThreadWaitTime));
	Result->SetNumberField(TEXT("rhiThreadMs"), FPlatformTime::ToMilliseconds(GRHIThreadTime));
	Result->SetNumberField(TEXT("swapBufferMs"), FPlatformTime::ToMilliseconds(GSwapBufferTime));
	Result->SetNumberField(TEXT("gameThreadCriticalPathMs"), FPlatformTime::ToMilliseconds(GGameThreadTimeCriticalPath));
	Result->SetNumberField(TEXT("renderThreadCriticalPathMs"), FPlatformTime::ToMilliseconds(GRenderThreadTimeCriticalPath));
	Result->SetStringField(TEXT("note"), TEXT("Single-frame snapshot from the last viewport draw, not a live trace. Zeros in commandlet mode (no viewport draws) are expected, not an error."));
	return JsonToString(Result);
}
