// launcher.h -- the launcher window: choose the game folder, the car, course and skill the game starts
// with, the game's speed and the display and sound options, then Play. Game settings > Graphics and
// Game settings > Key Bindings are shown in the window in its place to change the picture and the keys.
#pragma once

#include <wx/frame.h>

#include <vector>

#include "game.h"
#include "keys.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSimplebook;
class wxSlider;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

// The enhanced view's settings (Game settings > Graphics).
struct Look {
    bool classic = false;   // --classic
    int resScale = 4;       // --res-scale
    int aa = 2;             // --aa
    int motionDelay = 100;  // --motion-delay
    int haze = 70;          // --haze
    int fogStart = 10;      // --fog-start
    int drawDistance = 18;  // --draw-distance
    bool farLod = false;    // --far-lod
    bool enhLights = true;  // --enh-lights
    int beamNight = 100;    // --beam-night
    int beamDay = 35;       // --beam-day
    int beamSoft = 3;       // --beam-soft
};

class LauncherFrame : public wxFrame {
public:
    explicit LauncherFrame(const wxString& gameDirArg = wxString());

private:
    wxWindow* CreateMainPage(wxWindow* parent);
    wxWindow* CreateGraphicsPage(wxWindow* parent);
    wxWindow* CreateKeysPage(wxWindow* parent);
    void CreateMenus();
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    // Reads the folder's cars and courses again and refills their lists, keeping the chosen ones
    // where they still exist.
    void Reload();
    // Checks the folder and the program and enables Play and the controls to match.
    void UpdateState();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void Preferences();
    void SetTopmost(bool on);
    void Save();

    // The pages shown in place of the main one: Graphics and Key Bindings.
    void OpenPage(int page);
    void ClosePage();
    void FitPage();
    // Graphics: the controls from / to a Look.
    void ShowLook(const Look& look);
    Look EditedLook() const;
    void UpdateGraphics();
    void SetSliderValue(int i, int v);
    // Key Bindings: a new key for action i, the buttons from edited_.
    void ChangeKey(int action);
    void ShowEditedKeys();

    wxSimplebook* book_ = nullptr;
    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxChoice* car_ = nullptr;     // "Default", then the cars
    wxChoice* course_ = nullptr;  // "Default", then the courses
    wxChoice* skill_ = nullptr;   // "Default", then 1..9
    wxSpinCtrl* speed_ = nullptr;
    wxChoice* sound_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxCheckBox* finishMarker_ = nullptr;
    wxStaticText* lookNote_ = nullptr;  // the main page's summary of the graphics
    wxButton* play_ = nullptr;

    // The Graphics page.
    wxChoice* graphics_ = nullptr;    // enhanced / original
    wxChoice* resolution_ = nullptr;  // 1..8 times 320x200
    wxChoice* aa_ = nullptr;          // off, 2x2, 3x3, 4x4
    wxCheckBox* lights_ = nullptr;    // enhanced lights
    wxCheckBox* farLod_ = nullptr;    // the original's reduced far models
    struct SliderRow {
        wxSlider* slider;
        wxSpinCtrl* spin;     // the value typed
        wxStaticText* value;  // the unit
        wxString (*format)(int);
    };
    std::vector<SliderRow> sliders_;  // in the order of SLIDERS
    Look look_;                       // saved

    // The Key Bindings page: one button per action.
    std::vector<wxButton*> keyButtons_;
    keys::Bindings bindings_;  // saved
    keys::Bindings edited_;    // on the Key Bindings page

    Catalogue catalogue_;
    // The choices, kept while the lists are refilled; empty codes / 0 are "Default".
    wxString carCode_;
    wxString courseCode_;
    bool topmost_ = false;
    bool loading_ = true;  // no edits are recorded while the window is built
};
