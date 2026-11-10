# Test Drive III Enhanced — SDL3

An enhanced native port of Accolade's *Test Drive III: The Passion* (1990). Not an emulator. The original game
files are not included.

## Features

* Smooth 3D view at your display's refresh rate.
* High resolution (up to 2560 × 1600) with anti-aliasing.
* Longer draw distance and gradual distance fog.
* Smooth steering; spins, slides and the handbrake as in the original.
* Truer game speed; the race clock counts real seconds.
* Finish marker on the compass.
* Fixes to police, landings and other original quirks (see `CHANGELOG.md`).
* A launcher for car, course, skill and picture options.

## How to play (Windows)

1. Put your original game files in a folder named `Game`.
2. Place the program files next to it:

   ```text
   Test Drive III Enhanced\
   ├── Game\                           <- your game files (TDIII.EXE, DATAA.DAT, PLAYDISK.DAT, ...)
   ├── Test Drive III Enhanced.exe     <- the launcher
   ├── testdrive3-enhanced.exe         <- the game
   └── SDL3.dll and the other DLLs
   ```

3. Run `Test Drive III Enhanced.exe`, pick your options and press **Play**.

* The folder must be writable (high scores and choices are saved in `Game`).
* Alt+Enter: full screen.
* Launcher settings: `%APPDATA%\Test Drive III Enhanced\settings.ini`.

## Requirements

* Game files: `TDIII.EXE` (or `TD3.EXE`), `PLAYDISK.DAT`, `DATAA–C.DAT`, `INSTR.DAT`, the car (`C*`) and scene (`SCENE*`) files.
* A multi-core CPU for the default picture; lower the resolution or anti-aliasing on slow machines.
* To build: CMake 3.24+, C11, SDL 3; the launcher needs C++17 and wxWidgets 3.2.

## Supported game version

The port supports exactly one release of the game, and refuses any other with "… is a different release of Test
Drive III (load image … bytes; this port needs 166544)" or "… its program is not the TDIII.EXE this port needs".

* Game: *Test Drive III: The Passion* (Accolade, 1990), the DOS program `TDIII.EXE` built with Microsoft C 5.1.
  The port's notes do not record a version number.
* The program as reverse-engineered: `TDIII.EXE`, 125 251 bytes, compressed with EXEPACK; unpacked, its load
  image is **166 544 bytes** (0x28A90). The port accepts the file packed or unpacked: the load image is what it
  checks.
* `TDIII.EXE` with SHA-1 `a09b0d620b91247976a7d172fd433e8c6af25926` works (reported by a user).
* The program may also be named `TD3.EXE`, but the `TD3.EXE` copies reported so far are a different release
  (load image 179 568 bytes) that this port cannot load.

To see a file's hash on Windows, open a command prompt in the game folder and type
`certutil -hashfile TDIII.EXE SHA1`; on Linux or macOS, `sha1sum TDIII.EXE`. If you report a problem, give the
file's size and SHA-1.

`testdrive3-enhanced --check --game-dir Game` says whether the program in `Game` loads, without opening a window.

The car and course lists (in the game and the launcher) come from `PLAYDISK.DAT` only: to add the cars and
courses of a bonus or scenery disk, use the game's own installer or merge tool, so that it writes them into
`PLAYDISK.DAT`; copying the files alone does not list them.

## Build

In Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

* `-DTD3E_LAUNCHER=OFF` builds the game only.
* The MSYS2 `SDL3.dll` also needs `libiconv-2.dll` from `C:\msys64\mingw64\bin`.

## Run

```bash
./build/testdrive3-enhanced.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Game files folder (default `Game`) |
| `--scale N` | Window size, × 320×240 (default 3) |
| `--fullscreen` | Start in full screen |
| `--res-scale N` | Resolution, × 320×200 (default 4, 1–8) |
| `--aa N` | Anti-aliasing, N×N samples (default 2, 1 = off) |
| `--motion-delay P` | Smooth view delay, % of a game frame (default 100) |
| `--haze P` | Distance fog, % of the sky colour at the edge of the view (default 70, 0 = off) |
| `--draw-distance N` | Map cells drawn around the camera (default 7, 0 = original) |
| `--fog-start P` | Where the fog begins, % of the way to where it is full (default 10) |
| `--beam-night P`, `--beam-day P` | Headlight beams' strength at night / by day, % (default 100 / 35) |
| `--beam-soft N` | Headlight beams' soft edge, pixels (default 3, 0 = hard) |
| `--enh-lights 0\|1` | Enhanced headlight beams (default 1; 0 = the original's) |
| `--keys NAME=CODE,...` | Key bindings (the launcher's Game settings > Key Bindings) |
| `--finish-marker 0\|1` | Finish marker on the compass (default 1) |
| `--frame-ticks N` | Game speed, timer ticks per frame (default 14) |
| `--classic` | Original picture only |
| `--sound adlib\|speaker` | Sound device |
| `--car CODE`, `--course CODE` | Start car / course, e.g. `CCNSX`, `SCENE02` |
| `--skill N` | Skill level 1–9 (1–3 automatic) |
| `--check` | Check that `TDIII.EXE` loads, then exit |

## Controls

* Arrows / keypad: steer, gas, brake. Space: handbrake. A / Z: shift up / down.
* R mirror, H headlights, W wipers, C wheel centring, M radio.
* F1 window, F2 detail, F3 steering sensitivity, F5 chase view, F6 back to road, F7 mouse steering.
* F10 replay, F9 pause replay. Esc: leave.
* Ctrl-P pause, Ctrl-S sound, Ctrl-Q music, Ctrl-E engine sound, Ctrl-J joystick, Ctrl-K keyboard.
* The launcher's Game settings > Key Bindings changes the driving, car and game keys.

## Layout

* `src/` — the game (`ENGINE.md`; changes marked `ENH:`).
* `src/enhanced/` — the enhanced renderer (`ENHANCED.md`).
* `launcher/` — the launcher (`launcher/README.md`).
* `third_party/nuked-opl3/` — the OPL2 emulator.

## License

MIT (`LICENSE`). Nuked-OPL3 is LGPL 2.1+. *Test Drive III* is © Accolade; its files are not included.

## Support

https://buymeacoffee.com/krzysztofkania
