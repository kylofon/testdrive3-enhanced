# Test Drive III Enhanced (the launcher)

`Test Drive III Enhanced.exe` starts `testdrive3-enhanced`, Test Drive III: The Passion on SDL3 with the
enhanced view, with the options chosen in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls,
like the Test Drive II Enhanced and Test Drive III launchers it is modelled on; the menus, Preferences
and Key Bindings follow Aces of the Pacific Enhanced's.

## The window

* **Game files**
  * **Folder**: the folder with the original game's files (default `Game` beside the launcher).
  * **Program**: `testdrive3-enhanced.exe` (default: beside the launcher).
  * The line below says how many cars and courses the folder has, or what is missing (`TDIII.EXE` or `TD3.EXE`,
    `PLAYDISK.DAT`, `DATAA/B/C.DAT`, `INSTR.DAT`). **Play** stays greyed out until the folder and the program
    are there.
* **Start**, as if chosen in the game's menus (which can still change them; the game remembers them in
  `PLAYDISK.DAT` like its own choices):
  * **Car** (`--car`) and **Course** (`--course`): the slots of `PLAYDISK.DAT` whose `.LST` is in the folder,
    named as their `.LST` names them. *Default* is the game's own last choice.
  * **Skill level** (`--skill`): 1-9 as the game shows it; 1-3 have an automatic gearbox.
* **Options**
  * **Game speed** (`--frame-ticks`, default 14, chosen by play-testing): timer ticks (145.6 a second) per frame while driving. The
    game moves the cars once per frame, so fewer ticks make everything faster: your car, the traffic, the police
    and the opponents. At 10 the scenery passes about as fast as the speedometer says; 23 (the faithful port's
    default) passes it at about a third. The race clock always counts real seconds. Stored as `GameSpeed`.
  * **Sound** (`--sound`): AdLib / Sound Blaster (through Nuked-OPL3) or the PC speaker. `TD3.CFG` is not
    changed.
  * **Window size** (`--scale`) and **Start in full screen** (`--fullscreen`; Alt+Enter switches).
  * **Finish direction on the compass** (`--finish-marker`, on by default): a green mark on the compass points
    to the leg's gas station. Stored as `FinishMarker` in `settings.ini`.
* **Graphics**: a summary of the picture settings and the **Graphics** button (as Game settings > Graphics).
* **Play** starts the game; the launcher stays open.

## Menus

* **File**: **Preferences** (**Always on top** keeps the launcher above other windows) and **Exit**.
* **Game settings > Graphics** shows the enhanced view's settings in the window (`../ENHANCED.md`). **Default**
  puts them all back, **Apply** keeps them and goes back, **Cancel** goes back without changing them.
  * **Graphics**: *Enhanced* (the 3D view and the mirror drawn again, smooth and at a high resolution) or
    *Original* (`--classic`: the game's own 320 x 200 picture, scaled; the settings below but the resolution
    are then greyed out).
  * **Resolution** (`--res-scale`, default 1280 x 800) and **Anti-aliasing** (`--aa`, default 2 x 2).
  * Sliders, each with a number field beside it to type the value (kept in step with the slider):
    * **Motion** (`--motion-delay`, 0-100 %, default 100: moves between the game's last two frames, never
      guesses; less guesses ahead, which answers sooner but overshoots when the steering changes).
    * **Draw distance** (`--draw-distance`, 0-18 cells around the camera, default 18 — the whole wrapping 32×16 leg map; 0 is the game's own, with its 3/6/10 cells). The far cells also carry their trees and the other cars, including across the map's west/east and north/south seams.
    * **Fog** (`--haze`, 0-100 % of the sky's colour at the edge of the view, default 70; 0 = off).
    * **Fog begins** (`--fog-start`, 0-90 % of the way to where it is full, default 10).
    * **Use original far models at distance** (`--far-lod`, off by default): the enhanced view normally draws the
      full-detail models at every distance; on, vehicles and some buildings use the original's reduced far models
      beyond half a map cell.
    * **Lights**: **Enhanced lights** (`--enh-lights`, on by default; off gives the original's beams, full
      strength with a hard edge) and, under it, the headlight beams **At night** (`--beam-night`, default 100 %), **By day** (`--beam-day`, default 35 %:
      the game switches them on in rain and snow) and **Soft edge** (`--beam-soft`, 0-10 of the original's
      pixels, default 3; 0 = the original's hard edge).
  Stored as `Classic`, `Resolution`, `AntiAliasing`, `EnhancedLights`, `FarLod`, `Motion`, `DrawDistance`, `Haze`,
  `FogStart`, `BeamNight`, `BeamDay` and `BeamSoft`.
* **Game settings > Key Bindings** shows the game's keys in the window, in three groups (driving, car, views and
  game). Click a key and press the new one ("Press new key"; Shift, Ctrl or Alt can be held with it, Esc
  cancels, **No key** leaves the action without one). A key already used by another action can be moved,
  leaving that one without a key. **Default** puts every key back, **Apply** keeps them and goes back,
  **Cancel** goes back without changing them. The game gets the changed ones with `--keys` and applies them
  while a race runs; the menus, the name entry and the answers to the game's messages keep the game's own
  keys. The keypad's 8 / 2 / 4 / 6, Enter (the gear gates), Esc and Alt+Enter keep their jobs. Stored under
  `[Keys]`.
* **About**: version, author and links.

Everything is remembered in `%APPDATA%\Test Drive III Enhanced\settings.ini` (`~/.config/test-drive-iii-enhanced` on Linux). A
folder or program left at its default is stored empty, so it follows the launcher if the whole folder moves.
Delete the file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). The main build makes it beside `testdrive3-enhanced.exe` (`-DTD3E_LAUNCHER=OFF` leaves it out):

```bash
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

It also builds on its own (`cmake -S launcher -B launcher/build -G Ninja`); then choose the program in the
window or copy the launcher next to it.

On Windows the build copies every DLL the launcher needs beside it (`copy_dlls.cmake`): the two wxWidgets DLLs
and the MSYS2 libraries they load. Keep them with the `.exe` in a release. The C++ runtime of the launcher
itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `keys.h`, `keys.cpp`: the game's keys that can be changed, their names and the "Press new key" prompt
  (the game's own list is `src/enhanced/enh_keys.c`).
* `launcher.h`, `launcher.cpp`: the window and the About box.
* `game.h`, `game.cpp`: reading `PLAYDISK.DAT` and the `.LST` names, the file checks and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, a banded road running to the horizon drawn in code. `make_icon.py`
  (Pillow) writes the same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy.
* `CMakeLists.txt`: the build, standalone or from the main project.
