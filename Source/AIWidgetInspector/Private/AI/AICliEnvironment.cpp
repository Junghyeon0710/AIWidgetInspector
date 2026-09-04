// Copyright 2026 Junghyeon0710. All Rights Reserved.
// AI Widget Inspector

#include "AI/AICliEnvironment.h"

#include "AIWidgetInspectorLog.h"

#include "IModelContextProtocolModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ModelContextProtocolServer.h"
#include "ModelContextProtocolSettings.h"

namespace AIWidgetInspector::CliEnvironment
{
	namespace Private
	{
		/** 실행 파일 후보 확장자. npm으로 깔린 CLI는 Windows에서 .cmd 형태다. */
		static const TCHAR* ExecutableExtensions[] =
		{
#if PLATFORM_WINDOWS
			TEXT(".exe"), TEXT(".cmd"), TEXT(".bat"), TEXT(""),
#else
			TEXT(""),
#endif
		};

		/** 떠 있으면 서버를, 아니면 null을 돌려준다. 플러그인이 꺼져 있으면 모듈 자체가 없다. */
		static const FModelContextProtocolServer* FindRunningServer()
		{
			if (IModelContextProtocolModule* Module = IModelContextProtocolModule::Get())
			{
				if (const FModelContextProtocolServer* Server = Module->GetServer())
				{
					if (Server->IsServerRunning())
					{
						return Server;
					}
				}
			}

			return nullptr;
		}
	}

	bool FindExecutable(const FString& InExecutableName, FString& OutPath)
	{
		if (InExecutableName.IsEmpty())
		{
			return false;
		}

		const FString PathVariable = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
		if (PathVariable.IsEmpty())
		{
			return false;
		}

		TArray<FString> PathEntries;
#if PLATFORM_WINDOWS
		PathVariable.ParseIntoArray(PathEntries, TEXT(";"), true);
#else
		PathVariable.ParseIntoArray(PathEntries, TEXT(":"), true);
#endif

		for (const FString& PathEntry : PathEntries)
		{
			const FString TrimmedEntry = PathEntry.TrimStartAndEnd();
			if (TrimmedEntry.IsEmpty())
			{
				continue;
			}

			for (const TCHAR* Extension : Private::ExecutableExtensions)
			{
				const FString Candidate = TrimmedEntry / (InExecutableName + Extension);
				if (FPaths::FileExists(Candidate))
				{
					OutPath = Candidate;
					FPaths::NormalizeFilename(OutPath);
					return true;
				}
			}
		}

		return false;
	}

	bool IsEditorMcpRunning()
	{
		return Private::FindRunningServer() != nullptr;
	}

	FString GetEditorMcpUrl()
	{
		// 포트와 경로는 프로젝트별 설정이라 하드코딩하면 사용자가 바꿔 놓았을 때 조용히 어긋난다.
		//
		// 떠 있으면 설정값이 아니라 실제로 잡은 포트를 쓴다. 둘은 다를 수 있고, 그때 설정값을
		// 넘기면 CLI가 아무것도 없는 포트로 붙으러 간다.
		uint32 Port = UE::ModelContextProtocol::GetServerPortNumber();
		if (const FModelContextProtocolServer* Server = Private::FindRunningServer())
		{
			Port = Server->GetServerPort();
		}

		return FString::Printf(TEXT("http://127.0.0.1:%u%s"),
			Port,
			*UE::ModelContextProtocol::GetServerUrlPath());
	}

	FString WriteMcpConfigFile(const FString& InServerName)
	{
		const FString ConfigPath = FPaths::ProjectIntermediateDir() / TEXT("AIWidgetInspector") / TEXT("McpConfig.json");

		const FString Contents = FString::Printf(
			TEXT("{\"mcpServers\":{\"%s\":{\"type\":\"http\",\"url\":\"%s\"}}}"),
			*InServerName,
			*GetEditorMcpUrl());

		if (!FFileHelper::SaveStringToFile(Contents, *ConfigPath))
		{
			UE_LOG(LogAIWidgetInspector, Warning, TEXT("Could not write the MCP config file: %s"), *ConfigPath);
			return FString();
		}

		return ConfigPath;
	}
}
