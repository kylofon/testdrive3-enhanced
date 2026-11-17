// app.cpp -- the Test Drive III Enhanced launcher: the game folder, the start choices
// and the options, then testdrive3-enhanced.
#include <wx/app.h>
#include <wx/cmdline.h>

#include "launcher.h"

class LauncherApp : public wxApp {
public:
    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddOption(wxEmptyString, "game-dir",
                         "Folder with the original game files (preselects in the launcher)",
                         wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if (!wxApp::OnCmdLineParsed(parser)) return false;
        parser.Found("game-dir", &gameDir_);
        return true;
    }

    bool OnInit() override {
        SetAppName("Test Drive III Enhanced");
        SetVendorName("Krzysztof Kania");
        if (!wxApp::OnInit()) return false;
        auto* frame = new LauncherFrame(gameDir_);
        SetTopWindow(frame);
        frame->Show();
        return true;
    }

private:
    wxString gameDir_;
};

wxIMPLEMENT_APP(LauncherApp);
