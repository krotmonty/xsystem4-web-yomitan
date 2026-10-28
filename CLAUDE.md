# Rance VI + Yomitan (xsystem4-web fork)

Context for Claude Code, carried over from a claude.ai chat.

## Goal
Make Yomitan (browser pop-up dictionary) scan the text of AliceSoft System 4
games (Rance VI, Japanese version) directly — no OCR, no texthooker, no GSM.
Hold Shift + hover a word in the message window → Yomitan popup.
Status: WORKING on the user's Rance VI (confirmed by the user).

## How it works
- Base: kichikuou/xsystem4-web (browser build of nunuhara/xsystem4, the
  open-source System 4 engine). Engine is the `xsystem4/` submodule.
- Engine patch (xsystem4/src/sprite.c, scene.c, text.c; include/sprite.h,
  include/scene.h, include/gfx/font.h):
  - `gfx_layout_text()` in text.c lays out text exactly like the renderer
    (per-character x/advance) without drawing.
  - `sprite_text_draw()` records each character (SJIS bytes + position) in
    `sp->text.glyphs`; cleared by `sprite_text_clear()`, copied by
    `sprite_text_copy()`, freed by `sprite_free()`.
  - `scene_render()` calls `textmap_begin/collect/end` (bottom of sprite.c):
    collects lines from visible SACT sprites in screen coords, converts to
    UTF-8, and when changed sends JSON `{w,h,l:[[x,y,w,h,text],...]}` to the
    page via MAIN_THREAD_ASYNC_EM_ASM → `Module.shell.yomi.update(json)`.
  - `_free` added to EXPORTED_FUNCTIONS in xsystem4/CMakeLists.txt.
- Page side: src/yomi.ts (YomiOverlay), wired in src/shell.ts, styles at end
  of dist/play.css. Invisible absolutely-positioned text lines over the
  canvas (same letterboxing as video.c), scaleX-fitted to the game's width.
  Lines get pointer-events only while Shift is held; mouse events on them are
  re-dispatched to the canvas. Ctrl+Alt+Y = debug view (red text).
- Sengoku Rance (Kagura Steam build, C:\Program Files\Steam\steamapps\
  common\Sengoku Rance, runs Rance7.ain) draws text via
  SengokuRanceFont.SP_TextDraw, which bypasses sprite_text_draw();
  sp_text_draw() there calls sprite_text_record() so it is recorded too.
  Launcher: C:\AliceSoft\SengokuRance-Yomitan, port 8643 (serve.ps1
  -Port). One game per port: OPFS holds the game AND its saves, and
  installing replaces both. Make game ZIPs with UTF-8 names (MSYS2 zip).
- Keyboard Ctrl is unmapped in xsystem4/src/input.c (sdl_keytable) so it
  no longer triggers message skip; touch long-press still emulates Ctrl.
- Local-build tweaks: play.ts redirects to install.html when no game is
  installed; service worker + Google Analytics loader removed.
- release/: Windows launcher ("Start Rance (Yomitan).bat" → serve.ps1, a
  PowerShell HttpListener serving release/site with COOP/COEP headers on
  http://localhost:8642). release/site = built dist/ (minus .map files).
  release/source-changes/*.patch = diffs of all changes.

## Building
Needs Node.js, Emscripten (CI uses 5.0.3), CMake, Ninja, Bison, Flex.
  npm ci && npm run build      # → dist/ (xsystem4.js + xsystem4.wasm)
Build-environment workarounds made in the cloud sandbox (safe to keep or
revert): libwebp and cglm fetched via git instead of tarballs;
libjpeg-turbo built with -DWITH_SIMD=OFF (needed with emscripten 4.0.9).

### Building on Windows (this PC)
WSL is unavailable here (the Windows image lacks the feature), so the build
uses MSYS2 UCRT64 + emsdk 5.0.3, both under C:\dev\tools:
  build-windows.cmd             # → dist/, same as npm run build
- C:\dev\tools\msys64: MSYS2 with make, bison, flex, autotools, git and
  ucrt64 cmake/ninja/nodejs/python.
- C:\dev\tools\shims: sh launchers for emcc/emcmake/emconfigure/emmake/emar/
  emranlib (emsdk on Windows only ships .bat files).
- libffi in CMakeLists.txt is built in-source with `sh ./configure` and
  CC=emcc etc. by name, so it configures on Windows (still fine on Linux).
- Output differs in size from the cloud build (emscripten 4.0.9 there).
- release/site is NOT refreshed by the build; copy dist/ there (minus .map).

## Testing
A tiny test game exists in the chat history (alice-tools-compiled .ain that
draws Japanese text with SACT2.SP_TextDraw). Easier now: test on the real
game in the browser and use Ctrl+Alt+Y to check alignment.

## Ideas / possible next steps
- Other System 4 games (Rance 01/02, Sengoku Rance) may draw text through
  other paths (e.g. DrawGraph/text surfaces, Parts engine) — hook those if
  their text doesn't show in the debug view.
- Configurable scan key (currently Shift, matching Yomitan's default).
