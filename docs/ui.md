# In-Game UI

> Authors: Xein
>
> Created on: 18 Jul 2026

The engine renders HUDs, menus and world-space panels with [RmlUi](https://mikke89.github.io/RmlUiDoc/),
an HTML/CSS-like retained-mode UI library. Documents are authored as `.rml` (markup) and
`.rcss` (style) assets; panels are nodes you drop into a scene; and the glue between the UI
and gameplay is expected to live in Lua. A button's `data-event-click="Resume()"` calls a
plain `function M:Resume()` in the panel's script with zero C++ code, and a bar bound to
`{{health}}` updates when a script writes `self.ui_binds.health`.

## Motivation

RmlUi already speaks a familiar dialect — flexbox, selectors, transitions, media queries —
so authoring UI feels like authoring a web page, and artists can iterate on `.rcss` without
touching code. The work was in wiring it to the engine on our terms rather than RmlUi's.

RmlUi expects to own the window, device and swapchain; our renderer owns all of that and
runs threaded. So the vendored Vulkan backend (`ui/render/rmlui_renderer_vk.*`) was stripped
of its device/swapchain code and rebuilt on `VulkanCore` with dynamic rendering. Because
`Rml::Context` is not thread-safe and touches Lua data models, all `Update`/`Render` calls
happen on the main thread; the backend records UI draw commands into **secondary command
buffers** there, and the render thread replays them with `vkCmdExecuteCommands` inside the
`UIPass` and `WorldUIPass`. Everything is buffered per frame-in-flight so the two threads
never fight over a resource.

Each panel owns one `Rml::Context`. That gives every panel isolated data-model state,
natural show/hide via `onEnable`/`onDisable`, and independent sizing — a screen-space
`Panel` tracks the viewport, a world-space `Panel3D` renders to a texture sized by its
transform. `ui::UISystem` owns the library lifetime, drives every context centrally, and
routes input topmost-first.

The scripting bridge is the point of the whole system. Documents are preprocessed on load:
we scan for bind names (`{{expr}}`, `data-*`) and event handlers (`data-event-*`, inline
`on*`), inject the panel's global stylesheets, and tag `<body>` with the shared data model.
Events route through a global `EventListenerInstancer` and per-panel data-model callbacks to
`Node::call()`, which dispatches to both reflected C++ methods and every attached Lua script.
Value binds are backed by an `Rml::Variant` store exposed to Lua as `self.ui_binds.<name>`
(two-way: the UI writes land in the store, Lua writes mark the variable dirty and refresh the
document).

## Uses

### Authoring documents

A `.rml` document links its styles and references assets through VFS URIs, so it works the
same in the editor, from disk, or out of a `.pak`:

```html
<rml>
<head>
	<link type="text/rcss" href="hud.rcss"/>
</head>
<body data-model="binds">
	<div class="bar"><div class="fill" data-style-width="health"/></div>
	<span>hud_ammo</span> <span class="value">{{ammo}}</span>
</body>
</rml>
```

Relative `href`/`src` resolve against the document's own URI; absolute `scheme://` URIs are
left untouched. See the ready-to-run set under `engine/assets/ui/examples/`.

### Panels

Three nodes, all under the `ui` namespace:

- **Panel** (`Node`) — screen-space, sized to the viewport. Assign a UI element, styles,
  fonts, a color scheme and localization tables in the inspector.
- **Panel3D** (`Node3D`) — world-space. Renders to a texture whose resolution is the world
  scale times `pixels_per_meter`, drawn as a depth-tested quad by the World UI pass.
- **PanelContext** (`Node`) — holds long-lived global assets (fonts, styles, schemes,
  localizations) shared by every panel. Fonts cannot be unloaded in RmlUi, which is why they
  belong on a context that lives for the whole session.

`onEnable`/`onDisable` show/hide the document and include or exclude it from the draw and
input paths, so toggling `self:enabled(false)` from Lua hides a menu cleanly. Many panels can
be active at once.

### Lua events and binds

Attach a script to the panel node. Event attributes call methods by name:

```html
<button data-event-click="Resume()">menu_resume</button>
<input type="checkbox" data-checked="vsync_enabled"/>
```

```lua
---@class PauseController : Panel
---@type UIBinds.pause_menu
local M = {}

function M:init()
	self.ui_binds.vsync_enabled = true   -- seeds the two-way checkbox
end

function M:Resume()                      -- called by data-event-click, zero glue
	self:enabled(false)
end

function M:tick()
	if self.ui_binds.vsync_enabled then --[[ apply setting ]] end
end
```

`self.ui_binds.<name>` is the canonical accessor; `self.<name>` also works as sugar for any
bind name not shadowed by a reflected field or script variable. The editor generates
`{project}/.toast/lua/ui_binds.d.lua` from your `.rml` files (one `---@class UIBinds.<file>`
per document, `boolean` fields for `data-checked`), so a single `---@type UIBinds.hud`
annotation gives full completion.

### Localization and `${}` formatting

Localization tables are CSV assets edited in the horizontal **Table Editor**: `.tloc` maps a
string id to text per language, `.tiloc` maps ids to image references. The supported
languages come from the project's `[ui].languages`. The generated header row (`id,en,es,…`)
is not editable.

Any text node whose content matches a string id is translated automatically; the localized
string then runs through the `${}` formatter, which emits RML markup:

| Directive | Effect |
| --- | --- |
| `${clear}` / `$c` | close all open styling spans |
| `${italic}` / `$i` | italic span |
| `${bold}` / `$b` | bold span |
| `${color:name}` | colored span (scheme name, else a literal CSS color) |
| `${bold,color:red}` | combine styles in one span |
| `${data:var}` | data binding, becomes `{{var}}` |
| `$$` | a literal `$` |

Unknown directives log a warning under the "UI" sink and are stripped; `<`, `>`, `&` in the
text are escaped so localized content cannot inject markup. Colors resolve against the
panel's color scheme first, then the global and project schemes.

Switch language at runtime from Lua with `UI.setLanguage("es")`; every open document
re-translates and re-applies its localized images.

### Rendering effects

The Vulkan backend implements the full RmlUi effect set: stencil clip masks (`overflow` +
`border-radius`), the layer stack, filters (blur, drop-shadow, opacity, the color-matrix
family) and gradient shaders (linear/radial/conic, repeating). The `pause_menu` example
exercises a rounded scroll area (clip mask), a drop-shadow and a gradient fill.

## Performance

UI geometry is recorded once per frame on the main thread and replayed on the render thread,
so panels cost the render thread only a `vkCmdExecuteCommands`. Panels that are disabled or
have an empty context are skipped entirely — `buildDrawFrame` returns early when nothing is
visible. Every UI entry point carries a Tracy zone (`UISystem::buildDrawFrame`, context
records, effect passes), and RmlUi's own logging is routed to the "UI" sink. Fonts and global
styles are retained on the `PanelContext` to avoid reloading.

## Unit tests

`tests/ui/` covers the two pieces of pure logic that are easy to get wrong:

- `01-csv` — the RFC-4180 CSV reader/writer: quoted fields, embedded commas and newlines,
  escaped quotes, CRLF, trailing-newline handling, and a write/parse round-trip.
- `02-text_format` — the `${}` formatter: each directive, combined directives, `${data:}`
  substitution, `$$` escaping, markup escaping, scheme vs literal colors, and unknown-tag
  stripping.

Run them for both Debug and Release (asserts differ between configs).

## Expansion

- **World-space input.** World panels render but do not yet receive input; a ray-to-panel
  hit test feeding `Context::ProcessMouse*` would close that gap.
- **RmlUi debugger.** RmlUi ships an in-context debugger (`Rml::Debugger`) that could be
  toggled behind a dev flag for live inspection of the element tree and styles.
- **Image-localization pickers.** The Table Editor currently edits `.tiloc` cells as plain
  asset references; a drag-drop asset picker would match the inspector's control.

## Changelog

Initial version: asset pipeline (`.rml`/`.rcss`/`.ttf`/`.tga`, `.tff`/`.color`,
`.tloc`/`.tiloc`), `Panel`/`Panel3D`/`PanelContext` nodes, the vendored-and-expanded Vulkan
backend with clip masks/filters/shaders, the UI and World UI passes, input with high-DPI and
clipboard, Lua events and two-way data binds, localization with `${}` formatting, the Avalonia
Table Editor, generated Lua bind stubs, and the example content under `engine/assets/ui/examples/`.
