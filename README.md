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
enabled state, render opacity, render translation, colour, and (on a `TextBlock`) its text. The asset on
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

## Letting the AI do it

The plugin registers a toolset with Unreal's own **ModelContextProtocol** plugin, so any MCP client
— `claude`, or an interactive session pointed at `http://127.0.0.1:8000/mcp` — can drive the editor
directly:

| Tool | Effect |
|---|---|
| `GetSelectedWidget` | The full context block for whatever is selected |
| `ListWidgetTree` | Every widget name and class in the owning UserWidget |
| `PreviewWidgetChange` | Applies to the live instance; the asset is untouched |
| `ApplyWidgetChangeToAsset` | Writes into the Widget Blueprint; Ctrl+Z undoes it |
| `SaveWidgetAsset` | Writes the dirty Blueprint to disk |
| `RevertPreview` | Restores every previewed property to its original |

Picking **Claude Code (Unreal MCP)** and pressing **Request Change** runs the whole loop: the plugin
writes an MCP config pointing at the editor, launches the CLI against it, and the model reads the
selection and applies the change through the tools. Nothing is pasted, and no JSON is parsed out of
the reply — by the time it arrives the change has already happened, and the text explains what was
done.

The same CLI is registered twice on purpose. One entry returns JSON for the plugin to apply, the
other lets the model act. Which one you are using is visible in the provider list rather than hidden
behind a setting.

The editor's MCP server has to be running for the tool provider: **Project Settings → Model Context
Protocol → Auto Start Server**. The port and path are read from those same settings, so changing
them does not strand the CLI at a stale address.

This does not widen what the AI can do. The five tools are the whitelist, the arguments go through
the same parser and validator as the JSON path, and preview and asset writes are separate tools
rather than a flag — so the difference is visible in the tool name and in the log, not buried in a
parameter. What changes is that the model sees each result and can correct itself, instead of
emitting one JSON block and hoping.

Preview and asset writes stay separate deliberately. A preview costs one call to undo; an asset
write edits a file and recompiles a Blueprint.

## Requesting a change

**Request Change** sends the same context as **Ask AI**, plus instructions telling the model to
answer with an executable JSON block rather than prose:

```json
{
  "changes": [
    { "operation": "SetRenderOpacity",   "target_widget": "Btn_Upgrade", "value": 0.5 },
    { "operation": "SetText",            "target_widget": "Txt_Label",   "value": "Ultra" },
    { "operation": "SetColorAndOpacity", "target_widget": "Txt_Label",   "value": "#4FC3F7" }
  ]
}
```

With a CLI provider the loop closes on its own: the answer arrives, the plugin builds a plan from
it, and applies the entries that passed to the **live widget** so you can see the result. The asset
on disk is untouched, and **Revert Preview** puts everything back. The plan stays on screen, so
**Apply to Asset** is the next click when you like what you see.

Nothing reaches the asset without that click. A preview costs one button to undo; an asset write
edits a file and compiles a Blueprint, so a person decides it.

With the **Clipboard** provider there is no answer to read, so the flow is manual: paste the model's
reply into the response box and press **Parse Change**, then **Apply Preview**. **Cancel** discards
the plan.

Nothing the model writes is executed as code. Every change goes through three gates:

1. **Parse** — only the six whitelisted operations are recognised, and each one's `value` must be
   the JSON type that operation declares. `"true"` as a string is rejected for a boolean, because
   Unreal's lenient JSON conversion would silently turn `"yes"` into `false` and apply the opposite
   of what was asked.
2. **Validate** — the named widget must resolve to the selected widget or a sibling in the same
   UserWidget, must accept that operation, and the value must be in range.
3. **Apply** — the runtime executor re-checks the whitelist and skips anything that would not change
   the current value.

### The whitelist

| Operation | Value | Applies to |
|---|---|---|
| `SetVisibility` | one of `Visible` / `Collapsed` / `Hidden` / `HitTestInvisible` / `SelfHitTestInvisible` | any widget |
| `SetEnabled` | boolean | any widget |
| `SetRenderOpacity` | number, 0–1 | any widget |
| `SetRenderTranslation` | `{ "x": number, "y": number }` | any widget |
| `SetText` | string | `TextBlock` |
| `SetColorAndOpacity` | `"#RRGGBB"` or `"#RRGGBBAA"` | `TextBlock`, `Image`, `Button`, `UserWidget` |

Adding an operation means implementing it in the executor and registering it in four places — the
enum, the name table, the parser's allowed list, and its value-type gate. That is deliberate
friction: an operation cannot reach the widget by being named in a model's output alone.

Colour strings are read as sRGB and converted to linear, because that is what the colour picker in
the Details panel shows you. Skipping the conversion compiles and runs and simply produces colours
brighter than the ones you asked for. Malformed strings are rejected rather than passed to
`FColor::FromHex`, which answers transparent black for anything it cannot read — a single typo
would otherwise apply as "the text disappeared".

Applied changes are runtime previews, so **Revert Preview** undoes them. Writing to the Blueprint
asset is a separate mechanism.

### Saving

**Save to Asset**, in the Runtime Preview section, takes whatever is currently previewed, writes it
into the Widget Blueprint, and saves — one click, no typing. It reads the live widget rather than
the preview record, because the record keeps the *original* value in order to revert; the value you
are looking at is on the widget.

Committed previews are then dropped from the list rather than reverted. Once the values are in the
asset they are not temporary any more, and leaving them in would let **Revert Preview** undo what
was just saved, leaving the screen and the asset disagreeing.

Underneath it is still three steps, and they stay separable:

| | Survives | How to undo |
|---|---|---|
| Preview | until the widget rebuilds | **Revert Preview** |
| Apply to asset | until the editor closes | Ctrl+Z |
| Save | permanently | reopen the file from source control |

A preview never makes the asset dirty, so **Save Asset** stays greyed out after one — that is not a
bug, there is nothing to save yet. Apply to the asset first.

**Ctrl+S** saves while the inspector panel has focus. The binding lives on the panel's own command
list rather than globally, so it does not shadow the editor's Ctrl+S anywhere else.
**Ctrl+Shift+I** toggles Inspect Mode.

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
| **Claude Code (Unreal MCP)** | Same CLI, connected to the editor — it calls the widget tools itself | `claude` on `PATH`, MCP server running |
| **Codex** | Pipes the prompt to `codex exec -` and shows what comes back | `codex` on `PATH` |

A provider that cannot run right now says so in the panel, above the question box, with the command
that fixes it — `claude` missing names the npm install line; the MCP provider with the editor's
server switched off names the setting and points at the non-MCP provider as a way to keep working
meanwhile. A disabled button and a tooltip are not enough: nobody hovers a greyed control to find
out why it is grey, and the plugin ends up looking broken rather than unconfigured.

The MCP provider also reports itself unavailable when the server is off, rather than accepting the
request and failing at the 180-second timeout.

The CLI providers hold no credentials. The prompt goes to the tool's stdin, the answer comes back
on stdout, and authentication is whatever that CLI already has — the plugin never stores an API key
and never opens a socket itself. A CLI that is not installed stays in the list with the send button
disabled and the reason in its tooltip, rather than vanishing.

The CLI providers are run as responders, not as coding agents. Invoked plainly, `claude` would open
an agentic session in the project directory and start reading and editing files — answering a
request to recolour a label with instructions for doing it by hand rather than with the JSON the
plugin is waiting for. The prompt already carries everything needed, so the file and command tools
are switched off.

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

`AIWidgetInspector.Request` covers the prompt itself — that a change request carries the response
schema, that a plain question does not, and that every whitelisted operation is both named in the
schema and readable back by the parser. Those two lists drifting apart is invisible at runtime: the
model is told an operation does not exist and politely says so, which reads as a refusal rather than
a missing wire.

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
