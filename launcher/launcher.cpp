// launcher.cpp -- the launcher window.
#include "launcher.h"

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef __WXMSW__
#include <wx/msw/wrapcctl.h>
#include <shellapi.h>
#endif

#include "icon.h"
#include "settings.h"
#include "version.h"

const char* const APP_TITLE = "Test Drive III Enhanced";

namespace {

const char* const WEBSITE = "https://kkania.com";
const char* const SUPPORT = "https://buymeacoffee.com/krzysztofkania";
const char* const SECTION = "Game";

const int ID_GRAPHICS = wxID_HIGHEST + 1, ID_KEY_BINDINGS = wxID_HIGHEST + 2, ID_ABOUT = wxID_HIGHEST + 3;
enum Page { PAGE_MAIN, PAGE_GRAPHICS, PAGE_KEYS };

const int MIN_SCALE = 1, MAX_SCALE = 6, DEFAULT_SCALE = 3;
// The game's speed: timer ticks (145.6 a second) per frame while driving (testdrive3-enhanced --frame-ticks).
const int MIN_TICKS = 5, MAX_TICKS = 40, DEFAULT_TICKS = 14;
// The enhanced view: the picture's resolution (320x200 times this) and anti-aliasing (samples per pixel along
// each axis).
const int MIN_RES = 1, MAX_RES = 8;
const int MAX_AA = 4;

wxString FogPercent(int v) { return v == 0 ? wxString("off") : wxString::Format("%d %%", v); }
wxString Cells(int v) { return v == 0 ? wxString("the game's own") : wxString::Format("%d cells", v); }

// The unit after a slider's number field, with what the value means where it is special.
wxString Percent(int) { return "%"; }
wxString FogUnit(int v) { return v == 0 ? "% (off)" : "%"; }
wxString CellsUnit(int v) { return v == 0 ? "cells (the game's own)" : "cells"; }
wxString Pixels(int v) { return v == 0 ? "px (hard edge)" : "px"; }
wxString Behind(int v) { return v == 100 ? "% (smooth)" : v == 0 ? "% (most direct)" : "%"; }

// The Graphics page's sliders: the setting in settings.ini, the label, the tip, the range and default,
// the field of Look and the unit after its number field. The group is the box it is in.
struct SliderDef {
    int group;  // 0 motion and distance, 1 headlights
    const char* setting;
    const char* label;
    const char* tip;
    int min, max, def;
    int Look::*field;
    wxString (*format)(int);
};
const SliderDef SLIDERS[] = {
    {0, "Motion", "&Motion:",
     "The game moves everything about 6 times a second; the enhanced view moves the camera and the cars smoothly "
     "in between. 100 % runs a whole game frame behind and only moves between frames it has, so small steering "
     "corrections stay smooth; less guesses ahead, which answers sooner but overshoots when the steering changes.",
     0, 100, 100, &Look::motionDelay, Behind},
    {0, "DrawDistance", "&Draw distance:",
     "How far the scenery reaches: the enhanced view also draws the map cells around the camera that the game "
     "itself leaves out (its detail level builds 3, 6 or 10 cells ahead), with their trees and cars.",
     0, 8, 7, &Look::drawDistance, CellsUnit},
    {0, "Haze", "F&og:",
     "How much of the sky's colour at the horizon the farthest scenery takes on. The scenery sinks into it "
     "gradually as it gets farther, the ground too; at 100 % the edge of the view fades out completely.",
     0, 100, 70, &Look::haze, FogUnit},
    {0, "FogStart", "Fog &begins:",
     "Where the fog begins, as a share of the way to where it is full: lower brings it closer to the car.",
     0, 90, 10, &Look::fogStart, Percent},
    {1, "BeamNight", "At &night:",
     "How strongly the headlight beams light the road at night.",
     0, 100, 100, &Look::beamNight, Percent},
    {1, "BeamDay", "By d&ay:",
     "How strongly the headlight beams light the road by day. The game switches them on in rain and snow, where "
     "the day's colours lit up are far brighter than the night's.",
     0, 100, 35, &Look::beamDay, Percent},
    {1, "BeamSoft", "Soft &edge:",
     "How far the beams' edges fade out, in the original's pixels on each side of the edge; 0 is the "
     "original's hard edge.",
     0, 10, 3, &Look::beamSoft, Pixels},
};
const int SLIDER_COUNT = static_cast<int>(sizeof SLIDERS / sizeof SLIDERS[0]);

#ifdef __WXMSW__
HRESULT CALLBACK AboutCallback(HWND hwnd, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(hwnd, L"open", reinterpret_cast<LPCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
#else
// A label and a link on one line, for the portable About box.
void AddLink(wxWindow* parent, wxSizer* sizer, const wxString& label, const wxString& text, const wxString& url) {
    auto* line = new wxBoxSizer(wxHORIZONTAL);
    line->Add(new wxStaticText(parent, wxID_ANY, label + " "), 0, wxALIGN_CENTER_VERTICAL);
    line->Add(new wxHyperlinkCtrl(parent, wxID_ANY, text, url), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(line);
}
#endif

wxStaticText* GreyText(wxWindow* parent, const wxString& text = wxEmptyString) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return label;
}

// A label, a text field and a Browse button in a row of a two-column grid.
wxTextCtrl* PathRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip,
                    wxButton** browse) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(300), -1));
    text->SetToolTip(tip);
    *browse = new wxButton(parent, wxID_ANY, "B&rowse...");
    row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    row->Add(*browse, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(row, 1, wxEXPAND);
    return text;
}

// A label and a choice in a row of a two-column grid.
wxChoice* ChoiceRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* choice = new wxChoice(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->FromDIP(220), -1));
    choice->SetToolTip(tip);
    grid->Add(choice, 0, wxALIGN_CENTER_VERTICAL);
    return choice;
}

// Fills a slot list: "Default (...)" first, then the slots; selects `code` (Default when it isn't
// there). `last` is the game's own choice, shown in the Default entry.
void FillSlotChoice(wxChoice* choice, const std::vector<Slot>& slots, int last, wxString& code) {
    choice->Clear();
    choice->Append(last >= 0 ? wxString::Format("Default (the game's last choice: %s)", slots[last].name)
                             : wxString("Default (the game's last choice)"));
    int pick = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        choice->Append(slots[i].name);
        if (slots[i].code.CmpNoCase(code) == 0) pick = static_cast<int>(i) + 1;
    }
    choice->SetSelection(pick);
    if (pick == 0) code.clear();
}

Look LoadLook() {
    Look look;
    look.classic = settings::GetInt(SECTION, "Classic", 0) != 0;
    look.resScale = wxMax(MIN_RES, wxMin(MAX_RES, settings::GetInt(SECTION, "Resolution", look.resScale)));
    look.aa = wxMax(1, wxMin(MAX_AA, settings::GetInt(SECTION, "AntiAliasing", look.aa)));
    look.enhLights = settings::GetInt(SECTION, "EnhancedLights", 1) != 0;
    look.farLod = settings::GetInt(SECTION, "FarLod", 0) != 0;
    for (const SliderDef& s : SLIDERS)
        look.*s.field = wxMax(s.min, wxMin(s.max, settings::GetInt(SECTION, s.setting, s.def)));
    return look;
}

void SaveLook(const Look& look) {
    settings::SetInt(SECTION, "Classic", look.classic ? 1 : 0);
    settings::SetInt(SECTION, "Resolution", look.resScale);
    settings::SetInt(SECTION, "AntiAliasing", look.aa);
    settings::SetInt(SECTION, "EnhancedLights", look.enhLights ? 1 : 0);
    settings::SetInt(SECTION, "FarLod", look.farLod ? 1 : 0);
    for (const SliderDef& s : SLIDERS) settings::SetInt(SECTION, s.setting, look.*s.field);
}

}  // namespace

LauncherFrame::LauncherFrame(const wxString& gameDirArg)
    : wxFrame(nullptr, wxID_ANY, APP_TITLE, wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)) {
    SetIcons(AppIcons());
    topmost_ = settings::GetInt("Preferences", "AlwaysOnTop", 0) != 0;
    if (topmost_) SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
    bindings_ = keys::Load();
    edited_ = bindings_;
    look_ = LoadLook();

    // The launcher and, in its place while they are open, Graphics and Key Bindings.
    book_ = new wxSimplebook(this);
    book_->AddPage(CreateMainPage(book_), "Game");
    book_->AddPage(CreateGraphicsPage(book_), "Graphics");
    book_->AddPage(CreateKeysPage(book_), "Key Bindings");
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(book_, 1, wxEXPAND);
    SetSizer(all);
    CreateMenus();

    // Settings from the last run (command-line --game-dir takes precedence when given).
    wxString dir = gameDirArg.empty() ? settings::GetString(SECTION, "GameFolder", "") : gameDirArg;
    wxString program = settings::GetString(SECTION, "Program", "");
    folder_->ChangeValue(dir.empty() ? DefaultGameDir() : dir);
    program_->ChangeValue(program.empty() ? DefaultProgram() : program);
    carCode_ = settings::GetString(SECTION, "Car", "");
    courseCode_ = settings::GetString(SECTION, "Course", "");
    skill_->Append("Default (the game's last choice)");
    for (int s = 1; s <= 9; ++s) skill_->Append(wxString::Format(s <= 3 ? "%d (automatic gearbox)" : "%d", s));
    skill_->SetSelection(wxMax(0, wxMin(9, settings::GetInt(SECTION, "Skill", 0))));
    speed_->SetValue(wxMax(MIN_TICKS, wxMin(MAX_TICKS, settings::GetInt(SECTION, "GameSpeed", DEFAULT_TICKS))));
    sound_->SetSelection(settings::GetInt(SECTION, "Speaker", 0) != 0 ? 1 : 0);
    scale_->SetSelection(
        wxMax(MIN_SCALE, wxMin(MAX_SCALE, settings::GetInt(SECTION, "Scale", DEFAULT_SCALE))) - MIN_SCALE);
    fullscreen_->SetValue(settings::GetInt(SECTION, "Fullscreen", 0) != 0);
    finishMarker_->SetValue(settings::GetInt(SECTION, "FinishMarker", 1) != 0);
    randomRivals_->SetValue(settings::GetInt(SECTION, "RandomRivals", 1) != 0);
    weatherFix_->SetValue(settings::GetInt(SECTION, "WeatherFix", 1) != 0);
    {
        int v = settings::GetInt(SECTION, "AiOpponentRouting", -1);
        if (v < 0) { int old = settings::GetInt(SECTION, "AiRouteFinding", 0); v = (old != 0) ? 2 : 0; }
        aiOpponentRouting_->SetSelection(wxMax(0, wxMin(2, v)));
    }
    debugKeys_->SetValue(settings::GetInt(SECTION, "DebugKeys", 0) != 0);
    weight_->SetSelection(wxMax(0, wxMin(2, settings::GetInt(SECTION, "Weight", 1) - 1)));
    ShowLook(look_);
    ShowEditedKeys();
    loading_ = false;
    Reload();

    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (event.GetActive()) Reload();  // files may have been copied in meanwhile
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        Save();
        Destroy();
    });

    FitPage();
    if (!settings::RestoreWindowPosition(SECTION, this)) Centre();
}

wxWindow* LauncherFrame::CreateMainPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    // Game files
    auto* filesBox = new wxStaticBoxSizer(wxVERTICAL, page, "Game files");
    wxWindow* fb = filesBox->GetStaticBox();
    auto* filesGrid = new wxFlexGridSizer(2, gap, gap);
    filesGrid->AddGrowableCol(1);
    wxButton* browseFolder = nullptr;
    wxButton* browseProgram = nullptr;
    folder_ = PathRow(fb, filesGrid, "&Folder:",
                      "The folder with the original game's files (TDIII.EXE or TD3.EXE and the rest).", &browseFolder);
    program_ = PathRow(fb, filesGrid, "&Program:", "testdrive3-enhanced, the game.", &browseProgram);
    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusIcon_ = new wxStaticBitmap(fb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    statusNote_ = new wxStaticText(fb, wxID_ANY, wxEmptyString);
    statusRow->Add(statusIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    statusRow->Add(statusNote_, 1, wxALIGN_CENTER_VERTICAL);
    filesBox->Add(filesGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(statusRow, 0, wxEXPAND | wxALL, gap);
    folder_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) Reload();
    });
    program_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    browseFolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseFolder(); });
    browseProgram->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseProgram(); });

    // Start
    auto* startBox = new wxStaticBoxSizer(wxVERTICAL, page, "Start");
    wxWindow* sb = startBox->GetStaticBox();
    startBox->Add(GreyText(sb, "As if chosen in the game's menus."), 0, wxLEFT | wxRIGHT | wxTOP, gap);
    auto* startGrid = new wxFlexGridSizer(2, gap, gap);
    car_ = ChoiceRow(sb, startGrid, "&Car:", "The car the game starts with.");
    course_ = ChoiceRow(sb, startGrid, "C&ourse:", "The course (scenery) the game starts with.");
    skill_ = ChoiceRow(sb, startGrid, "&Skill level:",
                       "The game's skill level. 1-3 have an automatic gearbox; from 4 on you shift yourself and "
                       "over-revving damages the engine.");
    startBox->Add(startGrid, 0, wxALL, gap);
    car_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int i = car_->GetSelection();
        carCode_ = i > 0 ? catalogue_.cars[i - 1].code : wxString();
    });
    course_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int i = course_->GetSelection();
        courseCode_ = i > 0 ? catalogue_.courses[i - 1].code : wxString();
    });

    // Options
    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, page, "Options");
    wxWindow* ob = optionsBox->GetStaticBox();
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Game &speed:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* speedRow = new wxBoxSizer(wxHORIZONTAL);
    speed_ = new wxSpinCtrl(ob, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(64), -1),
                            wxSP_ARROW_KEYS, MIN_TICKS, MAX_TICKS, DEFAULT_TICKS);
    speed_->SetToolTip("Timer ticks per frame while driving (145.6 ticks a second). The game moves the cars "
                       "once per frame, so fewer ticks make everything faster. The race clock always counts real "
                       "seconds. 14 is the recommended speed; at 10 the scenery passes about as fast as the "
                       "speedometer says; 23 is the faithful port's default; 5 is the original program's limit.");
    speedRow->Add(speed_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    speedRow->Add(GreyText(ob, "ticks a frame: 14 recommended"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(speedRow, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "So&und:"), 0, wxALIGN_CENTER_VERTICAL);
    sound_ = new wxChoice(ob, wxID_ANY);
    sound_->Append("AdLib / Sound Blaster");
    sound_->Append("PC speaker");
    sound_->SetToolTip("The sound device the game uses. Ctrl+S and Ctrl+Q switch sound and music in the game.");
    grid->Add(sound_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&Window size:"), 0, wxALIGN_CENTER_VERTICAL);
    scale_ = new wxChoice(ob, wxID_ANY);
    for (int s = MIN_SCALE; s <= MAX_SCALE; ++s) scale_->Append(wxString::Format(L"%d × %d", 320 * s, 240 * s));
    scale_->SetToolTip("The window's size when the game starts. Alt+Enter switches to full screen.");
    grid->Add(scale_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Car w&eight:"), 0, wxALIGN_CENTER_VERTICAL);
    weight_ = new wxChoice(ob, wxID_ANY);
    weight_->Append("1x (original)");
    weight_->Append("2x");
    weight_->Append("3x");
    weight_->SetToolTip("Scales gravity so cars stay planted over hills. Higher values make the car heavier "
                        "(less air over crests, faster fall, more climbing resistance and grip).");
    grid->Add(weight_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "AI &Opponent Routing:"), 0, wxALIGN_CENTER_VERTICAL);
    aiOpponentRouting_ = new wxChoice(ob, wxID_ANY);
    aiOpponentRouting_->Append("Legacy");
    aiOpponentRouting_->Append("Modern: Deterministic");
    aiOpponentRouting_->Append("Modern: Random");
    aiOpponentRouting_->SetToolTip("Legacy: use the scene's original opponent scripts. "
                                   "Modern: Deterministic: generate stable routes and give opponents separate routes when possible. "
                                   "Modern: Random: generate an independent random valid route per opponent.");
    grid->Add(aiOpponentRouting_, 0, wxALIGN_CENTER_VERTICAL);
    optionsBox->Add(grid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    fullscreen_ = new wxCheckBox(ob, wxID_ANY, "Start in f&ull screen (Alt+Enter switches)");
    optionsBox->Add(fullscreen_, 0, wxALL, gap);
    finishMarker_ = new wxCheckBox(ob, wxID_ANY, "Finis&h direction on the compass");
    finishMarker_->SetToolTip("A green mark on the compass points to the gas station at the end of the leg "
                              "(an arrow at the compass's edge when it lies further to the side).");
    optionsBox->Add(finishMarker_, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
    randomRivals_ = new wxCheckBox(ob, wxID_ANY, "Random rival &cars");
    randomRivals_->SetToolTip("The two computer cars drive a different pair of cars from the play disk on each "
                              "course, including the two expansion cars. Off: the original's selection from the "
                              "three-car game. The choice stays the same through all legs of a course.");
    optionsBox->Add(randomRivals_, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);
    weatherFix_ = new wxCheckBox(ob, wxID_ANY, "Weather &trigger fix");
    weatherFix_->SetToolTip("Give wind, rain and snow their own trigger direction and don't lose a weather "
                            "trigger crossed during an external view. Off: the original's shared direction, "
                            "which can make rain or snow fail to deactivate.");
    optionsBox->Add(weatherFix_, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);

    debugKeys_ = new wxCheckBox(ob, wxID_ANY, "Debug keys");
    debugKeys_->SetToolTip("Enable the original's dormant debug keys (TD3_DEBUG_KEYS=1): T rain, S snow, N night, "
                           "F8 end leg. Also makes the car invulnerable.");
    optionsBox->Add(debugKeys_, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);

    // Graphics: a summary and the way to the page
    auto* lookBox = new wxStaticBoxSizer(wxVERTICAL, page, "Graphics");
    wxWindow* lb = lookBox->GetStaticBox();
    lookNote_ = GreyText(lb);
    auto* lookButton = new wxButton(lb, wxID_ANY, wxString::FromUTF8("&Graphics…"));
    lookButton->SetToolTip("Resolution, anti-aliasing, motion, draw distance, fog and the lights.");
    lookBox->Add(lookNote_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    lookBox->Add(lookButton, 0, wxALL, gap);
    lookButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenPage(PAGE_GRAPHICS); });

    // Buttons
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    play_ = new wxButton(page, wxID_ANY, "&Play");
    auto* close = new wxButton(page, wxID_CLOSE, "Close");
    buttons->AddStretchSpacer();
    buttons->Add(play_, 0, wxRIGHT, gap);
    buttons->Add(close);
    play_->SetDefault();
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Play(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

    // Two columns: the files and the start on the left, the options and the graphics on the right.
    auto* left = new wxBoxSizer(wxVERTICAL);
    left->Add(filesBox, 0, wxEXPAND);
    left->Add(startBox, 1, wxEXPAND | wxTOP, margin);
    auto* right = new wxBoxSizer(wxVERTICAL);
    right->Add(optionsBox, 0, wxEXPAND);
    right->Add(lookBox, 1, wxEXPAND | wxTOP, margin);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    columns->Add(left, 0, wxEXPAND);
    columns->Add(right, 0, wxEXPAND | wxLEFT, margin);
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(columns, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

wxWindow* LauncherFrame::CreateGraphicsPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    auto* heading = new wxStaticText(page, wxID_ANY, "Graphics");
    heading->SetFont(GetFont().Bold().Scaled(1.2f));
    auto* intro = GreyText(page, "The enhanced view draws the 3D view and the mirror again, smoothly and at a high "
                                 "resolution. Point at a setting for what it does.");

    // Picture
    auto* pictureBox = new wxStaticBoxSizer(wxVERTICAL, page, "Picture");
    wxWindow* pb = pictureBox->GetStaticBox();
    auto* pictureGrid = new wxFlexGridSizer(2, gap, gap);
    graphics_ = ChoiceRow(pb, pictureGrid, "&Graphics:",
                          "Enhanced: the 3D view and the mirror drawn again at the resolution below, anti-aliased, "
                          "and moving smoothly at the screen's rate between the game's own frames (about 6 a "
                          "second). Original: the game's own 320 x 200 picture, scaled up.");
    graphics_->Append("Enhanced: smooth, high resolution");
    graphics_->Append("Original: 320 x 200, as the game drew it");
    resolution_ = ChoiceRow(pb, pictureGrid, "&Resolution:",
                            "The size of the picture the game draws. The window shows it scaled to fit; lower it "
                            "on a slow computer.");
    for (int r = MIN_RES; r <= MAX_RES; ++r)
        resolution_->Append(wxString::Format(L"%d × %d%s", 320 * r, 200 * r, r == 1 ? " (original)" : ""));
    aa_ = ChoiceRow(pb, pictureGrid, "Anti-a&liasing:",
                    "Smooth polygon edges: every pixel of the 3D view is the average of several samples.");
    aa_->Append("Off");
    for (int a = 2; a <= MAX_AA; ++a) aa_->Append(wxString::Format(L"%d × %d samples", a, a));
    pictureBox->Add(pictureGrid, 0, wxALL, gap);
    graphics_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateGraphics(); });

    // The sliders, in two boxes.
    const char* const titles[2] = {"Motion, distance and fog", "Lights"};
    wxStaticBoxSizer* boxes[2];
    wxFlexGridSizer* grids[2];
    for (int g = 0; g < 2; ++g) {
        boxes[g] = new wxStaticBoxSizer(wxVERTICAL, page, titles[g]);
        grids[g] = new wxFlexGridSizer(4, small, gap);
        grids[g]->AddGrowableCol(1);
        boxes[g]->Add(grids[g], 0, wxEXPAND | wxALL, gap);
    }
    // The lights: the checkbox, then its settings indented under it.
    {
        wxWindow* box = boxes[1]->GetStaticBox();
        lights_ = new wxCheckBox(box, wxID_ANY, "&Enhanced lights");
        lights_->SetToolTip("Headlight beams with soft edges and their own strength by day and at night. Off: "
                            "the original's beams, at full strength with a hard edge.");
        boxes[1]->Insert(0, lights_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
        boxes[1]->GetItem(1)->SetFlag(wxEXPAND | wxBOTTOM | wxRIGHT | wxTOP | wxLEFT);
        boxes[1]->GetItem(1)->SetBorder(gap);
        lights_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateGraphics(); });
    }
    // The original's reduced far models: off, the enhanced view draws the full ones at every distance.
    {
        wxWindow* box = boxes[0]->GetStaticBox();
        farLod_ = new wxCheckBox(box, wxID_ANY, "Use original &far models at distance");
        farLod_->SetToolTip("Off: the full-detail models at every distance, as a powerful computer can. On: the "
                            "original's reduced models for vehicles and some buildings beyond half a map cell, as "
                            "the original game drew them.");
        boxes[0]->Insert(0, farLod_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
        boxes[0]->GetItem(1)->SetFlag(wxEXPAND | wxBOTTOM | wxRIGHT | wxTOP | wxLEFT);
        boxes[0]->GetItem(1)->SetBorder(gap);
        farLod_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { UpdateGraphics(); });
    }
    for (const SliderDef& s : SLIDERS) {
        wxWindow* box = boxes[s.group]->GetStaticBox();
        auto* label = new wxStaticText(box, wxID_ANY, s.label);
        auto* slider = new wxSlider(box, wxID_ANY, s.def, s.min, s.max, wxDefaultPosition, wxSize(FromDIP(220), -1),
                                    wxSL_HORIZONTAL);
        // The value can also be typed: a number field beside the slider, kept in step with it.
        auto* spin = new wxSpinCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(64), -1),
                                    wxSP_ARROW_KEYS, s.min, s.max, s.def);
        auto* value = new wxStaticText(box, wxID_ANY, s.format(s.min), wxDefaultPosition, wxDefaultSize,
                                       wxST_NO_AUTORESIZE);
        value->SetMinSize(wxSize(FromDIP(130), -1));
        label->SetToolTip(s.tip);
        slider->SetToolTip(s.tip);
        spin->SetToolTip(wxString::Format("%s (%d - %d)", s.tip, s.min, s.max));
        grids[s.group]->Add(label, 0, wxALIGN_CENTER_VERTICAL);
        grids[s.group]->Add(slider, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
        grids[s.group]->Add(spin, 0, wxALIGN_CENTER_VERTICAL);
        grids[s.group]->Add(value, 0, wxALIGN_CENTER_VERTICAL);
        const int i = static_cast<int>(sliders_.size());
        sliders_.push_back({slider, spin, value, s.format});
        slider->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent&) { SetSliderValue(i, sliders_[i].slider->GetValue()); });
        // Typed values count as they are typed (wxEVT_SPINCTRL comes only once the field is left or Enter is
        // pressed); out of range they wait until they are in it.
        spin->Bind(wxEVT_SPINCTRL, [this, i](wxSpinEvent&) { SetSliderValue(i, sliders_[i].spin->GetValue()); });
        spin->Bind(wxEVT_TEXT, [this, i](wxCommandEvent& event) {
            long v;
            const SliderDef& d = SLIDERS[i];
            if (event.GetString().ToLong(&v) && v >= d.min && v <= d.max) {
                sliders_[i].slider->SetValue(static_cast<int>(v));
                sliders_[i].value->SetLabel(sliders_[i].format(static_cast<int>(v)));
            }
        });
    }

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* defaults = new wxButton(page, wxID_ANY, "&Default");
    defaults->SetToolTip("Put every setting back as it was when the launcher was new.");
    auto* apply = new wxButton(page, wxID_APPLY, "&Apply");
    apply->SetToolTip("Keep these settings and go back.");
    auto* cancel = new wxButton(page, wxID_CANCEL, "Cancel");
    cancel->SetToolTip("Go back without changing the settings.");
    buttons->Add(defaults);
    buttons->AddStretchSpacer();
    buttons->Add(apply, 0, wxRIGHT, gap);
    buttons->Add(cancel);
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowLook(Look()); });
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        look_ = EditedLook();
        SaveLook(look_);
        ClosePage();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClosePage(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, small);
    all->Add(pictureBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(boxes[0], 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(boxes[1], 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

wxWindow* LauncherFrame::CreateKeysPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    auto* heading = new wxStaticText(page, wxID_ANY, "Key Bindings");
    heading->SetFont(GetFont().Bold().Scaled(1.2f));
    auto* intro = GreyText(page, "Click a key to change it. Keys are the keyboard's places, whatever its layout. "
                                 "They apply while you drive; the menus keep the game's own keys.");

    // A box per group with a label and a key button per action. Car shares its column with the keys that
    // can't be changed.
    wxStaticBoxSizer* boxes[keys::GROUP_COUNT];
    wxFlexGridSizer* grids[keys::GROUP_COUNT];
    for (int g = 0; g < keys::GROUP_COUNT; ++g) {
        boxes[g] = new wxStaticBoxSizer(wxVERTICAL, page, keys::GROUP_TITLES[g]);
        grids[g] = new wxFlexGridSizer(2, small, gap);
        grids[g]->AddGrowableCol(1);
        boxes[g]->Add(grids[g], 0, wxEXPAND | wxALL, gap);
    }
    for (int i = 0; i < keys::ACTION_COUNT; ++i) {
        const keys::Action& a = keys::ACTIONS[i];
        wxWindow* box = boxes[a.group]->GetStaticBox();
        grids[a.group]->Add(new wxStaticText(box, wxID_ANY, a.label), 0, wxALIGN_CENTER_VERTICAL);
        auto* button = new wxButton(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1));
        button->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { ChangeKey(i); });
        grids[a.group]->Add(button, 0, wxEXPAND);
        keyButtons_.push_back(button);
    }
    auto* fixedBox = new wxStaticBoxSizer(wxVERTICAL, page, "Keys that keep their job");
    wxWindow* xb = fixedBox->GetStaticBox();
    auto* fixedGrid = new wxFlexGridSizer(2, small, FromDIP(16));
    for (int i = 0; i < keys::FIXED_COUNT; ++i) {
        fixedGrid->Add(new wxStaticText(xb, wxID_ANY, keys::FIXED[i].keys));
        fixedGrid->Add(GreyText(xb, keys::FIXED[i].what));
    }
    fixedBox->Add(fixedGrid, 0, wxALL, gap);

    auto* first = new wxBoxSizer(wxVERTICAL);
    first->Add(boxes[keys::DRIVING], 0, wxEXPAND);
    first->Add(fixedBox, 0, wxEXPAND | wxTOP, margin);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    columns->Add(first, 0, wxEXPAND);
    columns->Add(boxes[keys::CAR], 0, wxEXPAND | wxLEFT, margin);
    columns->Add(boxes[keys::GAME], 0, wxEXPAND | wxLEFT, margin);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* defaults = new wxButton(page, wxID_ANY, "&Default");
    defaults->SetToolTip("Put every key back as the game has it.");
    auto* apply = new wxButton(page, wxID_APPLY, "&Apply");
    apply->SetToolTip("Keep these keys and go back.");
    auto* cancel = new wxButton(page, wxID_CANCEL, "Cancel");
    cancel->SetToolTip("Go back without changing the keys.");
    buttons->Add(defaults);
    buttons->AddStretchSpacer();
    buttons->Add(apply, 0, wxRIGHT, gap);
    buttons->Add(cancel);
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        edited_ = keys::Defaults();
        ShowEditedKeys();
    });
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        bindings_ = edited_;
        keys::Save(bindings_);
        ClosePage();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClosePage(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, small);
    all->Add(columns, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

void LauncherFrame::CreateMenus() {
    auto* file = new wxMenu;
    file->Append(wxID_PREFERENCES, wxString::FromUTF8("&Preferences…"));
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    auto* settingsMenu = new wxMenu;
    settingsMenu->Append(ID_GRAPHICS, wxString::FromUTF8("&Graphics…"));
    settingsMenu->Append(ID_KEY_BINDINGS, wxString::FromUTF8("&Key Bindings…"));

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(settingsMenu, "&Game settings");
#ifndef __WXMSW__
    auto* about = new wxMenu;
    about->Append(ID_ABOUT, wxString::FromUTF8("&About ") + APP_TITLE + wxString::FromUTF8("…"));
    bar->Append(about, "A&bout");
#endif
    SetMenuBar(bar);
#ifdef __WXMSW__
    // A top-level About item that acts straight away, which wxWidgets menus
    // can't express; MSWWindowProc handles its command.
    const HMENU native = static_cast<HMENU>(bar->GetHMenu());
    AppendMenuW(native, MF_STRING, ID_ABOUT, L"A&bout");
    DrawMenuBar(static_cast<HWND>(GetHWND()));
#endif

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Preferences(); }, wxID_PREFERENCES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPage(PAGE_GRAPHICS); }, ID_GRAPHICS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPage(PAGE_KEYS); }, ID_KEY_BINDINGS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { About(); }, ID_ABOUT);
}

#ifdef __WXMSW__
WXLRESULT LauncherFrame::MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) {
    if (msg == WM_COMMAND && LOWORD(wParam) == ID_ABOUT && lParam == 0) {
        CallAfter([this] { About(); });
        return 0;
    }
    return wxFrame::MSWWindowProc(msg, wParam, lParam);
}
#endif

void LauncherFrame::Reload() {
    catalogue_ = ReadCatalogue(folder_->GetValue());
    FillSlotChoice(car_, catalogue_.cars, catalogue_.lastCar, carCode_);
    FillSlotChoice(course_, catalogue_.courses, catalogue_.lastCourse, courseCode_);
    skill_->SetString(0, catalogue_.lastSkill > 0
                             ? wxString::Format("Default (the game's last choice: %d)", catalogue_.lastSkill)
                             : wxString("Default (the game's last choice)"));
    UpdateState();
}

void LauncherFrame::UpdateState() {
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    const bool haveProgram = wxFileName::FileExists(program);
    const wxString missing = MissingGameFile(dir);
    wxString note;
    if (!missing.empty())
        note = wxString::Format("This folder needs the game's files: %s is missing.", missing);
    else if (catalogue_.cars.empty() || catalogue_.courses.empty())
        note = "PLAYDISK.DAT lists no car or course whose .LST file is in this folder.";
    else if (!haveProgram)
        note = wxString::Format("%s isn't there.", wxFileName(program).GetFullName());
    else
        note = wxString::Format("Found %d %s and %d %s.", static_cast<int>(catalogue_.cars.size()),
                                catalogue_.cars.size() == 1 ? "car" : "cars",
                                static_cast<int>(catalogue_.courses.size()),
                                catalogue_.courses.size() == 1 ? "course" : "courses");
    const bool ok = haveProgram && missing.empty() && !catalogue_.cars.empty() && !catalogue_.courses.empty();
    statusIcon_->Show(!ok);
    statusNote_->SetLabel(note);
    car_->Enable(!catalogue_.cars.empty());
    course_->Enable(!catalogue_.courses.empty());
    play_->Enable(ok);
    if (look_.classic)
        lookNote_->SetLabel("Original: the game's own 320 x 200 picture.");
    else
        lookNote_->SetLabel(wxString::Format(L"Enhanced at %d × %d%s,\nfog %s, draw distance %s.", 320 * look_.resScale,
                                             200 * look_.resScale,
                                             look_.aa > 1 ? wxString::Format(L", %d × %d anti-aliasing", look_.aa, look_.aa)
                                                          : wxString(),
                                             FogPercent(look_.haze), Cells(look_.drawDistance)));
    play_->GetParent()->Layout();
}

void LauncherFrame::BrowseFolder() {
    wxDirDialog dialog(this, "Choose the folder with the game's files", folder_->GetValue(),
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) folder_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherFrame::BrowseProgram() {
    wxFileName current(program_->GetValue());
#ifdef __WXMSW__
    const char* const filter = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
#else
    const char* const filter = "All files|*";
#endif
    wxFileDialog dialog(this, "Choose testdrive3-enhanced", current.GetPath(), current.GetFullName(), filter,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) program_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherFrame::Play() {
    Save();
    GameOptions options;
    options.program = wxFileName(program_->GetValue()).GetFullPath();
    {
        wxFileName gameDir = wxFileName::DirName(folder_->GetValue());
        gameDir.MakeAbsolute();
        options.gameDir = gameDir.GetFullPath();
    }
    options.car = carCode_;
    options.course = courseCode_;
    options.skill = skill_->GetSelection();
    options.frameTicks = speed_->GetValue();
    options.speaker = sound_->GetSelection() == 1;
    options.scale = scale_->GetSelection() + MIN_SCALE;
    options.fullscreen = fullscreen_->GetValue();
    options.finishMarker = finishMarker_->GetValue();
    options.randomRivals = randomRivals_->GetValue();
    options.weatherFix = weatherFix_->GetValue();
    options.aiOpponentRouting = aiOpponentRouting_->GetSelection();
    options.debugKeys = debugKeys_->GetValue();
    options.weight = weight_->GetSelection() + 1;
    options.classic = look_.classic;
    options.resScale = look_.resScale;
    options.aa = look_.aa;
    options.motionDelay = look_.motionDelay;
    options.haze = look_.haze;
    options.fogStart = look_.fogStart;
    options.drawDistance = look_.drawDistance;
    options.farLod = look_.farLod;
    options.enhLights = look_.enhLights;
    options.beamNight = look_.beamNight;
    options.beamDay = look_.beamDay;
    options.beamSoft = look_.beamSoft;
    options.keys = keys::Argument(bindings_);
    wxString error;
    if (!LaunchGame(options, error)) wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
}

void LauncherFrame::Save() {
    // A folder or program left at its default is stored empty, so it follows the launcher if it moves.
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    settings::SetString(SECTION, "GameFolder",
                        wxFileName(dir).SameAs(wxFileName(DefaultGameDir())) ? wxString() : dir);
    settings::SetString(SECTION, "Program",
                        wxFileName(program).SameAs(wxFileName(DefaultProgram())) ? wxString() : program);
    settings::SetString(SECTION, "Car", carCode_);
    settings::SetString(SECTION, "Course", courseCode_);
    settings::SetInt(SECTION, "Skill", skill_->GetSelection());
    settings::SetInt(SECTION, "GameSpeed", speed_->GetValue());
    settings::SetInt(SECTION, "Speaker", sound_->GetSelection() == 1 ? 1 : 0);
    settings::SetInt(SECTION, "Scale", scale_->GetSelection() + MIN_SCALE);
    settings::SetInt(SECTION, "Fullscreen", fullscreen_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "FinishMarker", finishMarker_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "RandomRivals", randomRivals_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "WeatherFix", weatherFix_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "AiOpponentRouting", aiOpponentRouting_->GetSelection());
    settings::SetInt(SECTION, "DebugKeys", debugKeys_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "Weight", weight_->GetSelection() + 1);
    SaveLook(look_);
    settings::SaveWindowPosition(SECTION, this);
}

void LauncherFrame::Preferences() {
    wxDialog dialog(this, wxID_ANY, "Preferences");
    auto* all = new wxBoxSizer(wxVERTICAL);
    auto* topmost = new wxCheckBox(&dialog, wxID_ANY, "Always on &top");
    topmost->SetValue(topmost_);
    topmost->SetToolTip("Keep the launcher above other windows.");
    all->Add(topmost, 0, wxALL, dialog.FromDIP(12));
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
             dialog.FromDIP(12));
    dialog.SetSizerAndFit(all);
    dialog.SetMinSize(wxSize(dialog.FromDIP(300), -1));
    dialog.Fit();
    dialog.CentreOnParent();
    topmost->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return;
    if (topmost->GetValue() != topmost_) SetTopmost(topmost->GetValue());
}

void LauncherFrame::SetTopmost(bool on) {
    topmost_ = on;
    const long style = GetWindowStyleFlag();
    SetWindowStyleFlag(on ? style | wxSTAY_ON_TOP : style & ~wxSTAY_ON_TOP);
    settings::SetInt("Preferences", "AlwaysOnTop", on ? 1 : 0);
}

// The window takes the size of the page shown (the book on its own would take the largest page's).
void LauncherFrame::FitPage() {
    SetClientSize(book_->GetCurrentPage()->GetBestSize());
}

void LauncherFrame::OpenPage(int page) {
    if (book_->GetSelection() == page) return;
    if (page == PAGE_GRAPHICS) ShowLook(look_);
    if (page == PAGE_KEYS) {
        edited_ = bindings_;
        ShowEditedKeys();
    }
    book_->ChangeSelection(page);
    FitPage();
    GetMenuBar()->Enable(ID_GRAPHICS, false);
    GetMenuBar()->Enable(ID_KEY_BINDINGS, false);
    wxWindow* shown = book_->GetPage(page);
    SetDefaultItem(shown->FindWindow(wxID_APPLY));
    if (page == PAGE_GRAPHICS) graphics_->SetFocus();
    else keyButtons_.front()->SetFocus();
}

void LauncherFrame::ClosePage() {
    book_->ChangeSelection(PAGE_MAIN);
    FitPage();
    GetMenuBar()->Enable(ID_GRAPHICS, true);
    GetMenuBar()->Enable(ID_KEY_BINDINGS, true);
    SetDefaultItem(play_);
    UpdateState();
    play_->SetFocus();
}

void LauncherFrame::ShowLook(const Look& look) {
    graphics_->SetSelection(look.classic ? 1 : 0);
    resolution_->SetSelection(look.resScale - MIN_RES);
    aa_->SetSelection(look.aa - 1);
    lights_->SetValue(look.enhLights);
    farLod_->SetValue(look.farLod);
    for (int i = 0; i < SLIDER_COUNT; ++i) {
        const int v = look.*SLIDERS[i].field;
        SetSliderValue(i, v);
    }
    UpdateGraphics();
}

Look LauncherFrame::EditedLook() const {
    Look look;
    look.classic = graphics_->GetSelection() == 1;
    look.resScale = resolution_->GetSelection() + MIN_RES;
    look.aa = aa_->GetSelection() + 1;
    look.enhLights = lights_->GetValue();
    look.farLod = farLod_->GetValue();
    for (int i = 0; i < SLIDER_COUNT; ++i) look.*SLIDERS[i].field = sliders_[i].slider->GetValue();
    return look;
}

// A slider, its number field and its unit, all showing v.
void LauncherFrame::SetSliderValue(int i, int v) {
    SliderRow& row = sliders_[i];
    if (row.slider->GetValue() != v) row.slider->SetValue(v);
    if (row.spin->GetValue() != v) row.spin->SetValue(v);
    row.value->SetLabel(row.format(v));
}

// The original picture has none of the enhanced view's settings but the resolution.
void LauncherFrame::UpdateGraphics() {
    const bool enhanced = graphics_->GetSelection() == 0;
    aa_->Enable(enhanced);
    lights_->Enable(enhanced);
    farLod_->Enable(enhanced);
    for (int i = 0; i < SLIDER_COUNT; ++i) {
        const bool on = enhanced && (SLIDERS[i].group != 1 || lights_->GetValue());
        sliders_[i].slider->Enable(on);
        sliders_[i].spin->Enable(on);
        sliders_[i].value->Enable(on);
    }
}

void LauncherFrame::ShowEditedKeys() {
    for (int i = 0; i < keys::ACTION_COUNT; ++i) {
        const wxString name = keys::Name(edited_[i]);
        // Buttons treat '&' as a mnemonic marker.
        wxString label = name;
        label.Replace("&", "&&");
        keyButtons_[i]->SetLabel(label);
        keyButtons_[i]->SetToolTip(wxString::Format("%s: %s. Click to change it.", keys::ACTIONS[i].label, name));
        // A key that differs from the game's own stands out.
        wxFont font = keyButtons_[i]->GetParent()->GetFont();
        if (edited_[i] != keys::ACTIONS[i].def) font.MakeBold();
        keyButtons_[i]->SetFont(font);
    }
    book_->GetPage(PAGE_KEYS)->Layout();
}

void LauncherFrame::ChangeKey(int action) {
    const wxString label = keys::ACTIONS[action].label;
    const int key = keys::AskKey(this, label);
    if (key < 0 || key == edited_[action]) return;
    if (key != 0) {
        for (int other = 0; other < keys::ACTION_COUNT; ++other) {
            if (other == action || edited_[other] != key) continue;
            const wxString question = wxString::Format(
                "%s is already the key for %s.\n\nUse it for %s instead? %s will have no key.", keys::Name(key),
                keys::ACTIONS[other].label, label, keys::ACTIONS[other].label);
            if (wxMessageBox(question, "Key Bindings", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
            edited_[other] = 0;
        }
    }
    edited_[action] = key;
    ShowEditedKeys();
}

void LauncherFrame::About() {
    const wxString title = wxString("About ") + APP_TITLE;
    const wxString heading = wxString(APP_TITLE) + " " + APP_VERSION_TEXT;
    const wxString blurb = "Starts testdrive3-enhanced, Test Drive III: The Passion (Accolade, 1990) on SDL3 with a smooth, high-resolution view.";
#ifdef __WXMSW__
    // The Windows task dialog.
    const wxString content = wxString::Format(
        "%s\n\n"
        "Author: Krzysztof Kania\n"
        "Website: <a href=\"%s\">kkania.com</a>\n"
        "Support: <a href=\"%s\">buymeacoffee.com/krzysztofkania</a>",
        blurb, WEBSITE, SUPPORT);
    wxIcon icon;
    icon.CopyFromBitmap(AppBitmap(FromDIP(32)));
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof dialog;
    dialog.hwndParent = static_cast<HWND>(GetHWND());
    dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons = TDCBF_OK_BUTTON;
    dialog.pszWindowTitle = title.wc_str();
    dialog.hMainIcon = static_cast<HICON>(icon.GetHICON());
    dialog.pszMainInstruction = heading.wc_str();
    dialog.pszContent = content.wc_str();
    dialog.pfCallback = AboutCallback;
    TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr);
#else
    wxDialog dialog(this, wxID_ANY, title);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(new wxStaticBitmap(&dialog, wxID_ANY, AppBitmap(dialog.FromDIP(48))), 0, wxALL, dialog.FromDIP(12));

    auto* text = new wxBoxSizer(wxVERTICAL);
    auto* headingText = new wxStaticText(&dialog, wxID_ANY, heading);
    headingText->SetFont(dialog.GetFont().Bold().Scaled(1.3f));
    text->Add(headingText, 0, wxBOTTOM, dialog.FromDIP(8));
    text->Add(new wxStaticText(&dialog, wxID_ANY, blurb), 0, wxBOTTOM, dialog.FromDIP(12));
    text->Add(new wxStaticText(&dialog, wxID_ANY, "Author: Krzysztof Kania"));
    AddLink(&dialog, text, "Website:", "kkania.com", WEBSITE);
    AddLink(&dialog, text, "Support:", "buymeacoffee.com/krzysztofkania", SUPPORT);
    body->Add(text, 1, wxTOP | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(body, 1, wxEXPAND);
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
    dialog.SetSizerAndFit(all);
    dialog.CentreOnParent();
    dialog.ShowModal();
#endif
}
