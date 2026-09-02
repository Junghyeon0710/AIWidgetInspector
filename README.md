# AI Widget Inspector

Click any Slate or UMG widget in the Unreal Editor, see exactly what created it, and hand that
context to an AI assistant.

Unreal's built-in Widget Reflector tells you *what* a widget is. This plugin also tells you
*where it came from* — the `UWidget`, the owning `UUserWidget`, the Widget Blueprint asset, the
C++ file and line — and is built so that information can be packaged up and sent to an AI for
questions or change requests.

**Engine:** Unreal Engine 5.8 · **Type:** Editor-only plugin · **Platform:** Win64

> **Built on Experimental engine plugins.** This links against **Terminal**,
> **ModelContextProtocol** and **ToolsetRegistry**, all three Experimental in 5.8. They ship
> disabled, but you do not have to hunt them down: they are declared as dependencies, so enabling
> this plugin pulls all three in. Because they are Experimental their APIs can change between
> engine versions without deprecation, so this build is pinned to 5.8: a newer engine version will
> need a build made against it rather than dropping this one in as-is.
>
> Win64 only for now. The shell commands have a POSIX branch and it is covered by tests, but
> nothing here has been built or run on Mac or Linux, so the manifest does not claim them.

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

Pick UMG widgets while they are live, not on the Widget Blueprint designer canvas. The designer
wraps its preview so that it can handle drag-selection itself, so a click there stops at the designer
surface and never reaches the `SObjectWidget` that marks where your UserWidget begins — the UMG and
Source rows stay empty. Run the widget first, as an Editor Utility Widget or as your game UI in PIE,
and the rows fill in from the live instance. See [Sample content](#sample-content).

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

Picking a **Terminal** provider and pressing **Request Change** runs the whole loop: the plugin
writes an MCP config pointing at the editor, starts the CLI in the panel against it, and the model
reads the selection and applies the change through the tools. Nothing is pasted, and no JSON is
parsed out of the reply — by the time it arrives the change has already happened, and the text
explains what was done.

The tools are one of two ways the CLI can act. It also has the project as its working directory, so
work the tools do not cover — a new C++ class, a fix in existing code — it does by editing files.
A job often needs both, and the context sent with each request says so.

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
| **Claude Code (Terminal)** | Runs `claude` in the CLI Session below, connected to the editor | `claude` on `PATH` |
| **Codex (Terminal)** | Runs `codex` in the CLI Session below, connected to the editor | `codex` on `PATH` |

Earlier versions also shipped one-shot providers that piped the prompt to `claude -p` or
`codex exec -` and showed the reply. They are gone. A run with no way to ask for approval has to be
given its permissions up front, so they were handed the editor's tools and nothing else — no reading
or writing code. Asked to add a C++ base class they explained why they could not, which is a worse
answer than a prompt you can say yes to.

The two Terminal providers differ in one way worth knowing before you pick. This panel keeps its own
conversation so that closing the editor to rebuild — which in Unreal is constant — does not throw
away what you were in the middle of. `claude` can be told which conversation to resume, so the panel
pins one by id and resumes only that. `codex` has no such flag: the most it can do is resume the
most recent codex conversation in the working directory. So if you have run `codex` in this project
folder from another terminal, the panel may resume that one instead. The CLI Session says so, both
before you start it and after it resumes, and stays amber rather than green while that is the case.

A provider that cannot run right now says so in the panel, above the question box, with the command
that fixes it. A disabled button and a tooltip are not enough: nobody hovers a greyed control to find
out why it is grey, and the plugin ends up looking broken rather than unconfigured.

The CLI Session says what state it is in, in colour: what needs a hand, what is still starting, and
what is running. It also says whether the editor's MCP server is actually attached — checked by
asking the server whether it is running, not by reading the auto-start setting, which stays true
even when the port was already taken.

The CLI providers hold no credentials. The prompt is typed into the terminal the CLI is running in,
the answer comes back in that same terminal, and authentication is whatever that CLI already has —
the plugin never stores an API key and never opens a socket to a model itself. A CLI that is not
installed stays in the list rather than vanishing, with the reason and the command that installs it
stated in the panel.

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

## Non-Latin text in the terminal

If you type Korean, Japanese or Chinese into the CLI Session, two separate things go wrong. The
first is fixable, the second is not — not from this plugin.

**Missing glyphs.** The terminal's default font is `CascadiaMono`, which has no CJK glyphs, so the
text comes out as boxes. The codepoints are intact; only the font is wrong. Change it in
*Editor Preferences ▸ Terminal ▸ Font Family*, or in `DefaultEditorPerProjectUserSettings.ini`:

```ini
[/Script/Terminal.TerminalSettings]
FontFamily=NGULIM
```

The value is a filename stem, not a font name. On Windows the engine resolves it as
`%WINDIR%\Fonts\<stem>.ttf` and looks nowhere else, so it has to be a `.ttf` that is actually in
that folder. Font *collections* cannot be used: `gulim.ttc`, `batang.ttc`, `msgothic.ttc` and
`simsun.ttc` are all `.ttc`, so none of them can be selected however you spell them. Pick a
monospace `.ttf` that carries the glyphs you need — on a Korean Windows install `NGULIM.TTF` is one.

**Drifting borders.** Fixing the font does not fix the layout. The engine terminal stores one
`TCHAR` per cell with no width, and paints every cell at the same `CellWidth`; there is no
`wcwidth` or East Asian Width handling anywhere in it. CLIs assume the opposite — they count a
full-width character as two columns when they draw their boxes and wrap their text. So while
CJK text is on screen the TUI borders drift, and the drift persists until that text scrolls away.

There is no setting for this and no way to correct it from outside the terminal widget; it would
have to be fixed in the engine's Terminal plugin. Asking in English avoids it. Otherwise the answer
is still readable — the frame around it is not.

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

Enabling this plugin also enables **Terminal**, **ModelContextProtocol** and **ToolsetRegistry**,
which it depends on. You do not have to enable them yourself.

For the assistant to change widgets in the running editor rather than only read and write files,
turn on *Project Settings ▸ Plugins ▸ Model Context Protocol ▸ Auto Start Server*. The CLI Session
says which of the two you are in, so you do not have to guess.

Install `claude` or `codex` on `PATH`. The panel runs one of them inside the editor; the plugin
does not hold any API key of its own and does not talk to a model directly.

## Building from source

To package it for distribution:

```bash
RunUAT BuildPlugin -Plugin="<path>/AIWidgetInspector.uplugin" -Package="<output>" -Rocket
```

## License

Free to use. Copyright (c) 2026 Junghyeon0710. All rights reserved.  See [LICENSE](LICENSE).

Use it in your own projects, personal or commercial, including ones you sell — nothing you make
with it is encumbered. Read the source, and modify your copy. What is not granted is
redistribution: do not republish it or a derivative anywhere, marketplace or otherwise.

Point people at the original rather than passing on a copy, so everyone ends up with the version
that is still being fixed and reports arrive somewhere they can be acted on.

The source is public so you can see what the plugin does before you run it, and so the parts that
touch your project and your AI account can be audited rather than taken on trust.

The plugin holds no API key and talks to no model itself. It runs an AI CLI you installed, and
hands it a context file describing the selected widget: names, classes, the Widget Blueprint asset
path, the C++ file and line that created it, and a short snippet of that source. Whatever that CLI
then sends onward is between you and its provider.
