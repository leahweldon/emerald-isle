Emerald Isle
============

Version @VERSION@.

A real-time renderer of Ireland and an endless sea of procedural islands, in C++17 and OpenGL 3.3.

Live demo (in the browser, no download): https://leahweldon.github.io/emerald-isle/
Source, screenshots and documentation:  https://github.com/leahweldon/emerald-isle

Needs a GPU with OpenGL 3.3. Keep the assets folder next to the program.


Running
-------

Windows
  Double-click emerald-isle.exe. If Windows reports a missing VCRUNTIME140.dll, install the
  Microsoft Visual C++ Redistributable (x64) from Microsoft.

macOS (Apple silicon and Intel, macOS 11 or later)
  The app is not signed, so macOS blocks it the first time. Either:
    - right-click (or Control-click) emerald-isle in Finder, choose Open, then Open again
      (on macOS 15 and later: try to open it once, then System Settings > Privacy & Security >
      Open Anyway); or
    - in Terminal, from this folder:  xattr -d com.apple.quarantine emerald-isle
  Then double-click it, or run ./emerald-isle in Terminal.

Linux (x86-64, X11 or XWayland)
  ./emerald-isle
  Needs OpenGL 3.3 drivers (Mesa or your GPU vendor's).

./emerald-isle --help lists every option, for example:
  --view glenbeigh --time 21:30     golden hour over Dingle Bay
  --view shore --rain 1             heavy rain on a beach
  --quality low                     for older or integrated GPUs


Controls
--------

  W A S D            fly forward, back, left, right
  Q / E              down / up
  Shift              5x faster;  [ and ]  change the speed
  Mouse or trackpad  two-finger scroll or right-drag looks around; Tab captures the pointer
  Arrow keys         turn and pitch
  C                  switch between orbit and free flight
  T                  pause or resume time;  , and .  step 15 minutes (Shift: 1 hour)
  R                  rain on or off;  Shift+R  light or heavy
  H or F1            the panel: performance, time, weather, rendering and debug views
  Esc                quit


License
-------

MIT (see LICENSE). Elevation data, textures and fonts are credited in CREDITS.md.
