// game.h -- what a game folder holds (the car and course slots of PLAYDISK.DAT with the names from
// their .LST files), where testdrive3-enhanced is, and starting it.
#pragma once

#include <wx/string.h>

#include <vector>

// A car or course slot of PLAYDISK.DAT whose .LST is in the folder: its base name (the code the
// game takes, e.g. "CCNSX", "SCENE02") and the name its .LST starts with ("Acura NSX",
// "Cape Cod - Niagara").
struct Slot {
    wxString code;
    wxString name;
};

struct Catalogue {
    std::vector<Slot> cars;
    std::vector<Slot> courses;
    int lastCar = -1;     // PLAYDISK.DAT's selected slots (index into the lists above), -1 unknown
    int lastCourse = -1;
    int lastSkill = 0;    // 1..9 as the game shows it, 0 unknown
};

// The cars and courses of a game folder; empty lists if it has no PLAYDISK.DAT.
Catalogue ReadCatalogue(const wxString& gameDir);

// The folder the launcher runs from.
wxString LauncherDir();

// Where things are by default: "Game" and testdrive3-enhanced(.exe) beside the launcher.
wxString DefaultGameDir();
wxString DefaultProgram();

// Whether `dir` holds the file `name` (as written, or in upper or lower case).
bool FilePresent(const wxString& dir, const wxString& name);

// The first of the game's own files missing from `dir`, or empty if they are all there.
wxString MissingGameFile(const wxString& dir);

struct GameOptions {
    wxString program;       // testdrive3-enhanced(.exe)
    wxString gameDir;       // --game-dir
    wxString car;           // --car: empty = the game's own last choice
    wxString course;        // --course: empty = the game's own last choice
    int skill = 0;          // --skill 1..9: 0 = the game's own last choice
    int frameTicks = 14;    // --frame-ticks: the game's speed
    bool speaker = false;   // --sound speaker (else adlib)
    int scale = 3;          // --scale: the window is 320x240 times this
    bool fullscreen = false;  // --fullscreen
    bool finishMarker = true; // --finish-marker: the finish's direction on the compass
    bool randomRivals = true; // --random-rivals: the opponents drive cars drawn from the other cars
    bool weatherFix = true;   // --weather-fix: separate wind/rain/snow trigger direction, keep lost transitions
    int weight = 1;         // --weight: car weight/gravity multiplier (1 = original, 2, 3)
    bool classic = false;   // --classic: the original picture instead of the enhanced view
    int resScale = 4;       // --res-scale: the picture is 320x200 times this
    int aa = 2;             // --aa: anti-aliasing, aa x aa samples a pixel (1 = off)
    int motionDelay = 50;   // --motion-delay: percent of a game frame the smooth view runs behind
    int haze = 70;          // --haze: distance fog, percent of the horizon's sky colour (0 = off)
    int drawDistance = 7;   // --draw-distance: map cells drawn around the camera (0 = the game's own)
    bool farLod = false;    // --far-lod: the original's reduced far models instead of the full ones
    int fogStart = 10;      // --fog-start: where the fog begins, percent of the way to where it is full
    bool enhLights = true;  // --enh-lights: the enhanced headlight beams (the --beam-* below)
    int beamNight = 100;    // --beam-night: headlight beams' strength at night, percent
    int beamDay = 35;       // --beam-day: ... by day, percent
    int beamSoft = 3;       // --beam-soft: the beams' soft rim, view pixels (0 = hard)
    wxString keys;          // --keys: the keys not on their defaults (keys::Argument)
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
