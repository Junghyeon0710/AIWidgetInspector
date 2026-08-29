# AI Widget Inspector

Click any Slate or UMG widget in the Unreal Editor, see exactly what created it, and hand that
context to an AI assistant.

Unreal's built-in Widget Reflector tells you *what* a widget is. This plugin also tells you
*where it came from* — the `UWidget`, the owning `UUserWidget`, the Widget Blueprint asset, the
C++ file and line — and is built so that information can be packaged up and sent to an AI for
questions or change requests.

**Engine:** Unreal Engine 5.8 · **Type:** Editor-only plugin · **Platforms:** Win64, Mac, Linux

## Using it

1. Press **AI Widget** in the level editor toolbar (or open *Window ▸ Tools ▸ AI Widget Inspector*).
2. Move the mouse over the editor. The widget under the cursor is outlined in green, its ancestors
   more faintly.
3. Click to select. Inspect Mode ends and the Inspector panel opens with the selection outlined in
   orange. Press **ESC** or right-click to cancel without selecting.
4. A click always lands on the deepest widget under the cursor — often an `STextBlock` when you
   meant the `SButton` around it. Use **Parent** / **Child**, or the widget path list, to move the
   selection along the path.

While Inspect Mode is on, the plugin consumes mouse clicks so that clicking selects a widget
instead of pressing it. Everything returns to normal as soon as a widget is picked or you cancel.

## What it reports

| | |
|---|---|
| **Slate** | Widget type, visibility, enabled state, desired size, arranged geometry |
| **UMG** | Widget name, UMG type, owning UserWidget and its class, native parent class, slot type, parent widget, child count |
| **Source** | Widget Blueprint asset path, `SNew` file and line, native parent header, with **Open Blueprint** / **Open Source** buttons |

## Runtime preview

The **Runtime Preview** section changes the live `UWidget` instance and nothing else — visibility,
enabled state, render opacity, render translation, and (on a `TextBlock`) its text. The asset on
disk is untouched, and the change disappears when the widget is rebuilt. It answers "what would
this look like" without committing to anything.

Because these changes bypass the transaction system, Ctrl+Z will not undo them. The plugin keeps
the value each property had the first time you touched it, so **Revert Preview** restores the
originals no matter how many times you adjusted them in between. Requests that would not change
anything are ignored rather than recorded, so selecting a widget never marks it as modified.

Persistent edits to the Widget Blueprint asset are a separate mechanism, and are not part of this
section.

## Asking AI about the selection

The **Ask AI** section packages the selected widget into a prompt and hands it to a provider.
Type a question, pick a provider, press **Ask AI**. **Copy Context** does the same without a
question, when you just want the widget dump.

The context is deliberately small — the selected widget, its state, its slot, its source, and the
part of the widget path that belongs to your UI. The path is trimmed at the `SObjectWidget`
boundary, because everything above it is editor chrome that only wastes the model's attention:

```
[Selected Widget]
Name: Txt_UpgradeLabel
Slate Type: STextBlock
UMG Type: TextBlock
Owner: EUW_AIInspectorSample_C_0
Owner Class: EUW_AIInspectorSample_C
Native Parent: EditorUtilityWidget

[State]
Visibility = Visible
Enabled = true
DesiredSize = 122 x 39
Geometry = pos 2819, -210  size 122 x 39
Slot Type: ButtonSlot
UMG Parent: Btn_Upgrade

[Widget Path]
... (14 levels above omitted)
SObjectWidget (EUW_AIInspectorSample_C_0)
  SVerticalBox (UpgradePanel)
    SButton (Btn_Upgrade)
      STextBlock (Txt_UpgradeLabel)   <-- selected

[Source]
Blueprint: /AIWidgetInspector/Samples/EUW_AIInspectorSample
C++: TextBlock.cpp:362
C++ Path: D:/Epic Games/UE_5.8/Engine/Source/Runtime/UMG/Private/Components/TextBlock.cpp
Native Header: .../Engine/Source/Editor/Blutility/Public/EditorUtilityWidget.h

[Source Snippet] TextBlock.cpp:352-372  ('>' marks the line this widget was created on)
   352 |
   353 | TSharedRef<SWidget> UTextBlock::RebuildWidget()
...
>  362 |   MyTextBlock = SNew(STextBlock)
   363 |     .SimpleTextMode(bSimpleTextMode);
...

[User Question]
Why does this button not respond to clicks?
```

### What actually gets sent

Only the block above. The plugin does not read your project, walk your includes, or attach files —
the source it sends is the 21 lines around the `SNew` that built the selected widget, and nothing
else. If that file cannot be found on this machine, or is larger than 2 MB, the snippet is omitted
and the rest of the context still goes.

Engine widgets record the path of Epic's build machine, not yours, so the plugin remaps the tail of
those paths onto your local engine directory before reading. When the remap fails there is no
`C++ Path` line at all, rather than a path that does not exist — a model handed a fake path will try
to open it.

## Requesting a change

**Request Change** sends the same context as **Ask AI**, plus instructions telling the model to
answer with an executable JSON block rather than prose:

```json
{
  "changes": [
    { "operation": "SetRenderOpacity", "target_widget": "Btn_Upgrade", "value": 0.5 },
    { "operation": "SetText",          "target_widget": "Txt_Label",   "value": "Ultra" }
  ]
}
```

Paste the model's answer back into the response box and press **Parse Change**. Nothing runs yet —
the plugin builds a plan and shows it, one line per change, with rejected entries marked and the
reason given. **Apply Preview** then runs only the entries that passed; **Cancel** discards the plan.

Nothing the model writes is executed as code. Every change goes through three gates:

1. **Parse** — only the five whitelisted operations are recognised, and each one's `value` must be
   the JSON type that operation declares. `"true"` as a string is rejected for a boolean, because
   Unreal's lenient JSON conversion would silently turn `"yes"` into `false` and apply the opposite
   of what was asked.
2. **Validate** — the named widget must resolve to the selected widget or a sibling in the same
   UserWidget, must accept that operation, and the value must be in range.
3. **Apply** — the runtime executor re-checks the whitelist and skips anything that would not change
   the current value.

Applied changes are runtime previews, so **Revert Preview** undoes them. Writing to the Blueprint
asset is a separate mechanism.

## Writing the change into the asset

**Apply Preview** touches the live instance only. **Apply to Asset** edits the widget stored in the
Widget Blueprint, so the change survives into every instance built afterwards and into the saved
file.

The two act on different objects, and that distinction is the whole point: the live widget is a copy,
and editing it can never reach the asset. Apply to Asset resolves the target inside
`WidgetBlueprint->WidgetTree` instead, calls `Modify()` on it before writing, wraps the whole batch in
one `FScopedTransaction`, and recompiles the Blueprint afterwards. One **Ctrl+Z** undoes the entire
batch.

It deliberately does **not** save. Saving on apply would leave the file changed after an undo, so the
asset stays dirty and **Save Asset** in the Source section commits it when you are ready. Until then,
undo is the whole story.

### Providers

`IAIWidgetProvider` is the extension point. Three ship with the plugin:

| Provider | What it does | Requires |
|---|---|---|
| **Clipboard** | Copies the prompt so you can paste it into whatever assistant you already use | nothing |
| **Claude Code** | Pipes the prompt to `claude -p` and shows what comes back | `claude` on `PATH` |
| **Codex** | Pipes the prompt to `codex exec -` and shows what comes back | `codex` on `PATH` |

The CLI providers hold no credentials. The prompt goes to the tool's stdin, the answer comes back
on stdout, and authentication is whatever that CLI already has — the plugin never stores an API key
and never opens a socket itself. A CLI that is not installed stays in the list with the send button
disabled and the reason in its tooltip, rather than vanishing.

The process runs on a thread pool with a 180-second cap, so a slow or hung CLI never freezes the
editor; only the completion callback returns to the game thread. On Windows the launcher goes
through `cmd.exe` when the tool resolves to a `.cmd` or `.bat`, which is how npm-installed CLIs land.

Anything else plugs in behind the same interface, under the same two rules: run asynchronously, and
complete on the game thread.

UMG fields are filled in from the reflection metadata UMG attaches to the Slate widgets it builds.
Widgets written directly in C++ Slate have no such metadata, so those fields read `-` rather than
being guessed at. When a widget has no metadata of its own but an ancestor does, the panel says so
instead of quietly presenting the ancestor's data as the widget's own.

## Sample content

`/AIWidgetInspector/Samples/EUW_AIInspectorSample` is an Editor Utility Widget — a live UMG widget
you can run without entering PIE. Open it, press **Run Utility Widget**, then inspect the button in
the tab that appears to see the UMG and Source sections fill in.

It has to be a *live* widget: the UMG designer wraps its preview in an `EVisibility::HitTestInvisible`
layer (`SDesignerView.cpp`) so that it can handle drag-selection itself, which means hit testing
stops at the designer surface and never reaches the preview. Inspect running UI — an Editor Utility
Widget, or your game UI in PIE — not the designer canvas.

## Notes

- The hover and selection outlines are drawn through `FSlateApplication`'s widget reflector hook.
  That hook holds a single reflector, so opening Unreal's own Widget Reflector takes it over; this
  plugin reclaims it every time Inspect Mode starts.
- Nothing in the engine is modified, and the plugin depends only on engine modules.

## Tests

```bash
UnrealEditor-Cmd <project>.uproject -ExecCmds="Automation RunTests AIWidgetInspector;Quit" -unattended -nullrhi
```

`AIWidgetInspector.CommandParser` covers the surface that reads model output: JSON buried in prose and
code fences, braces inside string values, rejection of non-whitelisted operations, wrong value types,
missing fields, and malformed JSON.

`AIWidgetInspector.PersistentApplier` covers the asset path against the sample Blueprint — that the
template widget really changes, that operations the widget cannot take are rejected, and that a single
undo restores every property the batch touched. That last one is worth a test: forget one `Modify()`
and the value still changes, so nothing looks wrong until a user tries to undo.

`AIWidgetInspector.SourceResolver` covers path remapping and snippet extraction. This code fails
quietly by design — a bad path just yields empty fields, and the model still answers, so a broken
resolver looks like a widget that happens to have no C++ information. The test pins the remap
against the real engine install and checks the snippet window at both ends of a file.

## Installing

Clone into your project's `Plugins/` folder — the repository root is the plugin root, so it lands
in the right shape:

```bash
git clone https://github.com/Junghyeon0710/AIWidgetInspector.git Plugins/AIWidgetInspector
```

Then regenerate project files and rebuild the editor target. The plugin needs a C++ project; a
Blueprint-only project has no editor target to compile into.

## Building from source

To package it for distribution:

```bash
RunUAT BuildPlugin -Plugin="<path>/AIWidgetInspector.uplugin" -Package="<output>" -Rocket
```
