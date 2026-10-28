# XSystem4 for Web + Yomitan

A fork of [xsystem4-web](https://github.com/kichikuou/xsystem4-web) that lets
[Yomitan](https://github.com/yomidevs/yomitan) read the text of AliceSoft
System 4 games: hold **Shift** and hover a word in the message window to look
it up. No OCR or texthooker needed. Tested with **Rance VI** and
**Sengoku Rance** (Japanese versions).

**[Download the latest release](https://github.com/krotmonty/xsystem4-web-yomitan/releases/latest)**:
unzip it, run `Start Rance (Yomitan).bat` (or the Sengoku Rance one) and pick
a ZIP of your game folder. Windows, Chrome or Edge. `README.txt` in the
download has the details.

## What this fork adds

- **Yomitan text layer.** The engine reports every line of text it draws, and
  the page places invisible, Shift-activated HTML text exactly over it
  (`src/yomi.ts`; engine side in `xsystem4/src/sprite.c`, `scene.c`, `text.c`
  and `hll/SengokuRanceFont.c`). Ctrl+Alt+Y shows the layer to check
  alignment. F11 fullscreens the whole page so the layer stays usable.
- **Saves as normal files.** The Windows launcher (`release/serve.ps1`) keeps
  saves in a `saves` folder next to it, synced with the browser by
  `src/savesync.ts`. The previous version of each save is kept in
  `saves\.previous`.
- **System menu on F1**, in addition to right-clicking outside the game
  picture.
- **Optional Ctrl skip.** "Ctrl key skips messages" in the system menu turns
  off message skip on keyboard Ctrl.
- **Windows build** with MSYS2 and emsdk: `build-windows.cmd` (see
  `CLAUDE.md`, "Building on Windows").

The rest of this README is from upstream xsystem4-web, the source of the
[XSystem4 Web Installer](https://xsystem4-pwa.web.app) website.

## Building the site

To build the site, you will need to have the following software installed:

- [Node.js](https://nodejs.org)
- [Emscripten](https://emscripten.org/)
- [CMake](https://cmake.org/)
- [Bison](https://www.gnu.org/software/bison/)
- [Flex](https://github.com/westes/flex)

First, clone the repository and install the dependencies (in this fork the
engine is included directly, so no submodules are needed):

```sh
git clone https://github.com/krotmonty/xsystem4-web-yomitan.git
cd xsystem4-web-yomitan
npm install
```

Then, build the site:

```sh
npm run build
```

The built site will be in the `dist` directory. You can serve the site locally
using the `serve` command:

```sh
npm run serve
```

## Running a game without installing

On desktop environments, you can run a game without installing it, by opening
`<server-address>/play.html` and dropping the game folder onto the page.

## License

The source code in this repository is licensed under the [MIT License](LICENSE).

[xsystem4](https://github.com/nunuhara/xsystem4) is licensed under
[GPL 2.0](https://github.com/kichikuou/xsystem4/blob/wasm/COPYING).

This site also uses the following open-source software:
- [Pico](https://picocss.com/) ([MIT License](https://github.com/picocss/pico/blob/v2.0.6/LICENSE.md))
- [Tabler Icons](https://tabler.io/icons) ([MIT License](https://tabler.io/license))
