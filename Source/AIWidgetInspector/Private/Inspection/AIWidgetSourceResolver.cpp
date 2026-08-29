// AI Widget Inspector

#include "Inspection/AIWidgetSourceResolver.h"

#include "Inspection/AIWidgetInspectionResult.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/StringBuilder.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "UObject/Class.h"
#include "Widgets/SWidget.h"

namespace AIWidgetSourceResolverPrivate
{
	/** 빌드 머신 경로에서 이 조각 뒤쪽을 떼어 로컬 엔진 디렉터리에 붙인다. */
	static const TCHAR* EngineSourceMarker = TEXT("/Engine/Source/");
	static const TCHAR* EnginePluginsMarker = TEXT("/Engine/Plugins/");
}

bool FAIWidgetSourceResolver::ResolveLocalPath(const FString& InRecordedPath, FString& OutLocalPath)
{
	using namespace AIWidgetSourceResolverPrivate;

	if (InRecordedPath.IsEmpty())
	{
		return false;
	}

	FString Normalized = InRecordedPath;
	FPaths::NormalizeFilename(Normalized);

	// 프로젝트나 플러그인 코드면 기록된 경로가 곧 이 컴퓨터의 경로다.
	if (FPaths::FileExists(Normalized))
	{
		OutLocalPath = Normalized;
		return true;
	}

	// 엔진 코드는 Epic의 빌드 머신 경로로 기록된다. 뒤쪽만 떼어 로컬 엔진에 붙인다.
	auto TryRemap = [&Normalized, &OutLocalPath](const TCHAR* InMarker, const FString& InLocalRoot) -> bool
	{
		int32 MarkerIndex = INDEX_NONE;
		if (!Normalized.FindLastChar(TEXT('/'), MarkerIndex))
		{
			return false;
		}

		const int32 Found = Normalized.Find(InMarker, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (Found == INDEX_NONE)
		{
			return false;
		}

		const FString Tail = Normalized.RightChop(Found + FCString::Strlen(InMarker));
		const FString Candidate = InLocalRoot / Tail;
		if (FPaths::FileExists(Candidate))
		{
			OutLocalPath = Candidate;
			return true;
		}

		return false;
	};

	if (TryRemap(EngineSourceMarker, FPaths::EngineDir() / TEXT("Source")))
	{
		return true;
	}

	if (TryRemap(EnginePluginsMarker, FPaths::EngineDir() / TEXT("Plugins")))
	{
		return true;
	}

	return false;
}

bool FAIWidgetSourceResolver::ReadSnippet(const FString& InFilePath, int32 InLine, FAIWidgetSourceInfo& OutInfo)
{
	if (InLine <= 0)
	{
		return false;
	}

	const int64 FileSize = IFileManager::Get().FileSize(*InFilePath);
	if (FileSize <= 0 || FileSize > MaxSourceFileSize)
	{
		return false;
	}

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *InFilePath))
	{
		return false;
	}

	if (!Lines.IsValidIndex(InLine - 1))
	{
		return false;
	}

	const int32 FirstLine = FMath::Max(1, InLine - SnippetContextLines);
	const int32 LastLine = FMath::Min(Lines.Num(), InLine + SnippetContextLines);

	TStringBuilder<2048> Builder;
	for (int32 LineNumber = FirstLine; LineNumber <= LastLine; ++LineNumber)
	{
		// 생성 위치를 표시해 둔다. AI가 어느 줄이 문제의 Widget인지 세지 않아도 되게.
		Builder.Appendf(TEXT("%s%5d | %s\n"),
			LineNumber == InLine ? TEXT(">") : TEXT(" "),
			LineNumber,
			*Lines[LineNumber - 1]);
	}

	OutInfo.Snippet = Builder.ToString();
	OutInfo.SnippetStartLine = FirstLine;
	OutInfo.SnippetEndLine = LastLine;
	return true;
}

FAIWidgetSourceInfo FAIWidgetSourceResolver::Resolve(const FAIWidgetInspectionResult& InInspection, const TSharedPtr<const SWidget>& InWidget)
{
	FAIWidgetSourceInfo Info;

	if (!InInspection.bIsValid)
	{
		return Info;
	}

	// SNew이 남긴 파일과 라인.
	const FName CreatedIn = InInspection.SlateCreatedIn;
	if (!CreatedIn.IsNone())
	{
		Info.CreatedInFile = CreatedIn.GetPlainNameString();
		Info.CreatedInLine = CreatedIn.GetNumber();

		if (ResolveLocalPath(Info.CreatedInFile, Info.ResolvedFile))
		{
			ReadSnippet(Info.ResolvedFile, Info.CreatedInLine, Info);
		}
	}

	// Native 부모 클래스가 어디에 선언됐는지. Blueprint로 감싸인 UI에서 실제 코드를 찾는 출발점이다.
	if (UClass* NativeParentClass = InInspection.NativeParentClass.Get())
	{
		FString HeaderPath;
		if (FSourceCodeNavigation::FindClassHeaderPath(NativeParentClass, HeaderPath))
		{
			Info.NativeHeaderPath = MoveTemp(HeaderPath);
		}

		FString SourcePath;
		if (FSourceCodeNavigation::FindClassSourcePath(NativeParentClass, SourcePath))
		{
			Info.NativeSourcePath = MoveTemp(SourcePath);
		}
	}

	return Info;
}
