// CLAUDE-NOTE: run_python bridge. Executes arbitrary UE Editor Python via IPythonScriptPlugin and
// returns captured log output. This gives the MCP client the full reflected editor API (incl. the
// PCG framework: unreal.PCGGraph / PCGComponent / nodes / settings) without a dedicated tool per
// feature. Mirrors HandleExecCommand's parse/validate/return shape.

#include "BlueprintMCPServer.h"
#include "IPythonScriptPlugin.h"
#include "Dom/JsonObject.h"

FString FBlueprintMCPServer::HandleRunPython(const FString& Body)
{
	TSharedPtr<FJsonObject> Json = ParseBodyJson(Body);
	if (!Json.IsValid())
	{
		return MakeErrorJson(TEXT("Invalid JSON body."));
	}

	FString Code;
	if (!Json->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
	{
		return MakeErrorJson(TEXT("Missing required field: 'code'."));
	}

	if (!bIsEditor)
	{
		return MakeErrorJson(TEXT("run_python is only available in editor mode. Open the UE5 editor to use this tool."));
	}

	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (Python == nullptr || !Python->IsPythonAvailable())
	{
		return MakeErrorJson(TEXT("Python is not available. Ensure the PythonScriptPlugin is enabled for this project."));
	}

	UE_LOG(LogTemp, Display, TEXT("BlueprintMCP: run_python (%d chars)"), Code.Len());

	// mode "eval" returns the value of a single expression; default runs a full (multi-line) script.
	FString Mode;
	Json->TryGetStringField(TEXT("mode"), Mode);

	FPythonCommandEx Cmd;
	Cmd.Command = Code;
	Cmd.ExecutionMode = Mode.Equals(TEXT("eval"), ESearchCase::IgnoreCase)
		? EPythonCommandExecutionMode::EvaluateStatement
		: EPythonCommandExecutionMode::ExecuteFile;
	// Unattended: don't pop modal dialogs during execution (we're headless from the client's view).
	Cmd.Flags = EPythonCommandFlags::Unattended;

	const bool bSuccess = Python->ExecPythonCommandEx(Cmd);

	// Concatenate captured stdout/stderr/log lines emitted during execution.
	FString Output;
	for (const FPythonLogOutputEntry& Entry : Cmd.LogOutput)
	{
		if (!Output.IsEmpty()) { Output += TEXT("\n"); }
		Output += Entry.Output;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("output"), Output);
	if (!Cmd.CommandResult.IsEmpty())
	{
		Result->SetStringField(TEXT("result"), Cmd.CommandResult);
	}
	return JsonToString(Result);
}
