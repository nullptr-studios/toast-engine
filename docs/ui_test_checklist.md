# UI Manual Test Checklist

> Authors: Xein
>
> Created on: 18 Jul 2026

Manual QA for the in-game UI. Example assets live in `engine/assets/ui/examples/` and
`engine/assets/lua/examples/`. Tick each item on a fresh build.

## Assets & import

- [ ] Import a `.ttf` — appears as a **Font** asset, no thumbnail.
- [ ] Import a `.tga` — appears as a **UI Image** with a generated `.png` thumbnail.
- [ ] The **Texture** importer no longer accepts `.tga` (it is UI-only now).
- [ ] Create `.rml`, `.rcss`, `.tff`, `.color`, `.tloc`, `.tiloc` from the Asset Browser
      "New" menu; each opens/edits without error.
- [ ] `example.color` lists its named colors; `example.tff` shows a family name + font list.

## Project settings

- [ ] Project `[ui]` section exposes a global color scheme and a languages list.
- [ ] Adding a language updates the Table Editor's generated columns.

## Panels

- [ ] A **Panel** with `hud.rml` renders at viewport size and tracks window resize.
- [ ] A **Panel3D** with `world_sign.rml` renders in world space; scaling the node changes
      its texture resolution.
- [ ] A **PanelContext** with fonts/styles/schemes/localizations makes them available to
      every panel.
- [ ] HUD and pause menu are visible simultaneously.
- [ ] `self:enabled(false)` from Lua hides a panel (onDisable) and removes it from draw+input;
      re-enabling restores it (onEnable).

## Scripting

- [ ] `pause_controller.lua` `Resume()`/`Quit()` fire from `data-event-click` with no C++ glue.
- [ ] The vsync checkbox two-way binds: toggling it updates `self.ui_binds.vsync_enabled`, and
      setting the bind from Lua moves the checkbox.
- [ ] `hud_controller.lua` animates the bound health bar and `{{ammo}}` value each tick.
- [ ] Editing an `.rml` regenerates `.toast/lua/ui_binds.d.lua`; `---@type UIBinds.hud` gives
      completion on `self.ui_binds.` in the Lua LSP.
- [ ] Inline `onclick="Fn()"` also routes to a node method/script.

## Localization

- [ ] HUD/menu text shows the localized strings, not the raw ids.
- [ ] `UI.setLanguage("es")` from Lua re-translates every open document live.
- [ ] `data-loc-image` swaps the image `src` on language change.
- [ ] `${}` cases render: `${bold}`, `${italic}`, `${color:danger}`, `${bold,color:danger}`,
      `${clear}`, `${data:x}` → `{{x}}`, `$$` → `$`; unknown tag warns in the "UI" sink.

## Rendering

- [ ] Rounded `overflow` scroll area clips correctly (stencil clip mask).
- [ ] HUD health bar shows a linear gradient.
- [ ] Pause panel shows a drop-shadow / blur filter.
- [ ] CSS `transform` on an element renders correctly.
- [ ] High-DPI: UI is crisp and mouse hit-testing is accurate on a scaled display.
- [ ] Text input works (type into a field), including non-ASCII, and clipboard paste.

## Table Editor

- [ ] Double-clicking a `.tloc`/`.tiloc` opens the horizontal Table Editor.
- [ ] The header row (id + languages) is not editable.
- [ ] Add row, edit cells, delete row; Ctrl+S saves; autosave recovers after a crash.
- [ ] Closing with unsaved changes prompts to save/discard/cancel.

## Packaging & tooling

- [ ] Pack the project and run from `.pak`; all UI assets resolve through the VFS.
- [ ] Tracy shows the UI zones (`UISystem::buildDrawFrame`, context records, effect passes).
- [ ] RmlUi diagnostics appear under the "UI" log sink.
- [ ] `tests/ui/01-csv` and `tests/ui/02-text_format` pass in Debug and Release.
