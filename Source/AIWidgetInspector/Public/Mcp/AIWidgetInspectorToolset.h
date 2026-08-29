// AI Widget Inspector

#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "AIWidgetInspectorToolset.generated.h"

/**
 * Lets an AI assistant read and change the widget the user has selected in the editor.
 *
 * Unreal's ModelContextProtocol plugin exposes these functions as MCP tools, so an AI CLI calls
 * them directly instead of returning JSON for the plugin to interpret. It sees each result and can
 * correct itself. No server of our own: the engine already ships one.
 *
 * These functions are the whitelist. Arguments go through the same parser and validator as the
 * response-JSON path, so neither route can drift into accepting what the other rejects.
 *
 * The comment above each function is what the model reads when choosing a tool. English on
 * purpose — it is the one language every assistant handles well, whatever the user is typing.
 */
UCLASS(BlueprintType, MinimalAPI)
class UAIWidgetInspectorToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Describes the UMG or Slate widget currently selected in the editor.
	 *
	 * Returns its type, current state (visibility, enabled, size, geometry), slot, parent, owning
	 * Widget Blueprint, and the C++ location that created it along with the surrounding lines.
	 * Call this before deciding what to change.
	 * If nothing is selected it says so; ask the user to click a widget with Inspect Mode.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString GetSelectedWidget();

	/**
	 * Lists every widget inside the UserWidget that owns the current selection.
	 *
	 * Gives name, class, and current visibility. The only names accepted by TargetWidget are the
	 * ones in this list, so call this before changing anything other than the selected widget.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString ListWidgetTree();

	/**
	 * Applies a change to the live widget on screen. The asset file is untouched.
	 *
	 * Cheap to undo: RevertPreview restores the original value. Always use this first for anything
	 * the user needs to look at, such as a colour or an opacity.
	 *
	 * @param Operation      One of SetVisibility, SetEnabled, SetText, SetRenderOpacity, SetRenderTranslation, SetColorAndOpacity. Anything else is rejected.
	 * @param TargetWidget   Name of the widget to change. Must be a name returned by ListWidgetTree.
	 * @param ValueJson      A JSON value whose type the operation decides. SetVisibility takes a quoted string, one of "Visible", "Collapsed", "Hidden", "HitTestInvisible", "SelfHitTestInvisible". SetEnabled takes true or false. SetText takes a quoted string. SetRenderOpacity takes a number from 0 to 1. SetRenderTranslation takes {"x":30,"y":0}. SetColorAndOpacity takes "#RRGGBB" or "#RRGGBBAA", read as sRGB.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString PreviewWidgetChange(
		const FString& Operation,
		const FString& TargetWidget,
		const FString& ValueJson);

	/**
	 * Writes a change into the Widget Blueprint asset. Undoing it takes Ctrl+Z in the editor.
	 *
	 * Does not save, so the user still has to save for it to reach the file. Unlike a preview this
	 * edits the asset, so only call it when the user has clearly asked for that. To show how
	 * something would look, use PreviewWidgetChange.
	 *
	 * @param Operation      Same as PreviewWidgetChange.
	 * @param TargetWidget   Same as PreviewWidgetChange.
	 * @param ValueJson      Same as PreviewWidgetChange.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString ApplyWidgetChangeToAsset(
		const FString& Operation,
		const FString& TargetWidget,
		const FString& ValueJson);

	/**
	 * Saves the Widget Blueprint changed by ApplyWidgetChangeToAsset to disk.
	 *
	 * After this the change survives closing the editor, and Ctrl+Z no longer takes the file back.
	 * Only call it when the user has asked to save. Reports back if there was nothing to save.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString SaveWidgetAsset();

	/** Restores every value changed by PreviewWidgetChange to what it was. Asset changes are not affected. */
	UFUNCTION(meta = (AICallable), Category = "AIWidgetInspector")
	static AIWIDGETINSPECTOR_API FString RevertPreview();
};
