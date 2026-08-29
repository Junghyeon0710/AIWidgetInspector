// AI Widget Inspector

#pragma once

#include "CoreMinimal.h"

class SWidget;
struct FAIWidgetInspectionResult;

/** 선택된 Widget을 만든 C++ 코드가 어디에 있는지. 못 찾은 항목은 비어 있다. */
struct FAIWidgetSourceInfo
{
	/** SNew이 기록해 둔 경로. 엔진 위젯이면 빌드 머신 경로라 이 컴퓨터에는 없다. */
	FString CreatedInFile;
	int32 CreatedInLine = 0;

	/** 위 경로를 이 컴퓨터에서 실제로 열 수 있는 경로로 옮긴 결과. */
	FString ResolvedFile;

	/** Native 부모 클래스가 선언/정의된 파일. */
	FString NativeHeaderPath;
	FString NativeSourcePath;

	/** 생성 위치 주변 코드. 줄 번호가 붙어 있다. */
	FString Snippet;
	int32 SnippetStartLine = 0;
	int32 SnippetEndLine = 0;

	bool HasResolvedFile() const { return !ResolvedFile.IsEmpty(); }
	bool HasSnippet() const { return !Snippet.IsEmpty(); }
};

/**
 * Widget을 만든 C++ 코드를 찾는다.
 *
 * Widget Blueprint는 에셋 경로 하나로 끝나지만 C++ Slate Widget은 그렇지 않다.
 * SNew이 남긴 파일/라인이 유일한 단서인데, 엔진 위젯의 경우 그 경로는 Epic의 빌드 머신
 * 경로("D:/build/++UE5/Sync/Engine/Source/...")라서 이 컴퓨터에는 존재하지 않는다.
 * 그래서 경로를 로컬 엔진 디렉터리로 옮겨 본 뒤에야 파일을 읽을 수 있다.
 */
class AIWIDGETINSPECTOR_API FAIWidgetSourceResolver
{
public:
	static FAIWidgetSourceInfo Resolve(const FAIWidgetInspectionResult& InInspection, const TSharedPtr<const SWidget>& InWidget);

	/** 기록된 경로를 이 컴퓨터에서 열 수 있는 경로로 옮긴다. */
	static bool ResolveLocalPath(const FString& InRecordedPath, FString& OutLocalPath);

	/** 생성 위치 위아래로 몇 줄까지 가져올지. AI에게 보내는 양을 여기서 묶는다. */
	static constexpr int32 SnippetContextLines = 10;

	/** 이보다 큰 파일은 읽지 않는다. 생성된 코드나 잘못 잡힌 경로에서 시간을 쓰지 않기 위해서. */
	static constexpr int64 MaxSourceFileSize = 2 * 1024 * 1024;

private:
	static bool ReadSnippet(const FString& InFilePath, int32 InLine, FAIWidgetSourceInfo& OutInfo);
};
