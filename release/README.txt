Rance VI + Yomitan (xsystem4 web build with an in-game text layer)
====================================================================

What this is
------------
The open-source xsystem4 engine (which plays Rance VI) running in your
browser, modified so the game's text can be scanned by Yomitan directly.
No OCR, no texthooker, no GSM.

Hold Shift and hover a word in the message window -> Yomitan pops up.
Clicking without Shift works normally (advances text, menus, etc.).

First time
----------
1. Zip your Japanese Rance VI folder (right-click the folder ->
   Send to -> Compressed (zipped) folder). The folder must contain
   System40.ini and the game's .ald/.ain files.
2. Double-click "Start Rance (Yomitan).bat". A console window opens
   (keep it open while playing) and your browser opens the game page.
3. The page asks for the ZIP. Pick it and wait for the install to finish
   (it is copied into the browser's private storage; a few minutes).
4. Click "Start Game".

Sengoku Rance: use "Start Sengoku Rance (Yomitan).bat" instead. It runs on
its own port (8643), so it never replaces Rance VI or its saves. Each game
must always be started with its own .bat file.

After that, just double-click the .bat file to play.

Saves
-----
Your saves are normal files in the "saves" folder next to the .bat file.
While you play they are copied there a few seconds after the game saves;
when you start the game they are loaded from there. So they survive
reinstalling the game or clearing the browser, and you can copy that
folder to back them up or move them to another PC. The previous version of
each save is kept in saves\.previous, in case one gets overwritten by
mistake. (The first time you start this version, your existing saves are
copied into the folder automatically.)

System menu
-----------
Press F1 (or right-click the black bars beside the game picture) to open
the system menu: settings, Restart Game, and "Save to ZIP" / "Restore from
ZIP" for your saves.

Yomitan
-------
- Use Chrome or Edge with Yomitan installed and your dictionaries imported.
- Yomitan's default scan key is Shift. If you changed it, set it back to
  Shift (Yomitan settings -> Scanning -> Scan modifier key).
- Anki mining, audio, frequency lists etc. all work as on any web page.

Tips
----
- Ctrl+Alt+Y shows the invisible text layer in red, to check that it
  lines up with the game text.
- Use F11 for fullscreen, not the game's own fullscreen option, or the
  text layer will be hidden.
- Ctrl skips text, like in the original game. If it fires by accident,
  turn off "Ctrl key skips messages" in the system menu (F1).
- If the page says the port is busy, another copy is already running.

What was changed (for the curious)
----------------------------------
xsystem4/src/sprite.c, scene.c, text.c: the engine records where every
character is drawn on screen and sends the visible text lines to the page.
xsystem4/src/hll/SengokuRanceFont.c: same recording for Sengoku Rance's
own text drawing.
src/yomi.ts: places invisible, Shift-activated HTML text over the canvas
so Yomitan can scan it.
