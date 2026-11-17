// game.cpp -- PLAYDISK.DAT, the .LST names, the files the game needs and starting it.
#include "game.h"

#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <string>

namespace {

// A file of `dir` whose name matches `name` as written, in upper or in lower case
// (case matters outside Windows); empty if there is none.
wxString FindFile(const wxString& dir, const wxString& name) {
    for (const wxString& n : {name, name.Upper(), name.Lower()}) {
        const wxString path = wxFileName(dir, n).GetFullPath();
        if (wxFileName::FileExists(path)) return path;
    }
    return wxString();
}

// The bytes of a file of `dir` (at most `limit`), empty if it can't be read.
std::string ReadBytes(const wxString& dir, const wxString& name, size_t limit) {
    const wxString path = FindFile(dir, name);
    if (path.empty()) return std::string();
    wxLogNull quiet;
    wxFFile file(path, "rb");
    if (!file.IsOpened()) return std::string();
    std::string data(limit, '\0');
    data.resize(file.Read(data.data(), limit));
    return data;
}

// A zero-terminated string at the start of `data`, at most `max` bytes.
wxString CString(const std::string& data, size_t max) {
    std::string s = data.substr(0, max);
    s = s.substr(0, s.find('\0'));
    return wxString(s.c_str(), wxConvISO8859_1).Trim();
}

}  // namespace

bool FilePresent(const wxString& dir, const wxString& name) { return !dir.empty() && !FindFile(dir, name).empty(); }

wxString MissingGameFile(const wxString& dir) {
    if (!FilePresent(dir, "TDIII.EXE") && !FilePresent(dir, "TD3.EXE")) return "TDIII.EXE";
    for (const char* name : {"PLAYDISK.DAT", "DATAA.DAT", "DATAB.DAT", "DATAC.DAT", "INSTR.DAT"})
        if (!FilePresent(dir, name)) return name;
    return wxString();
}

// PLAYDISK.DAT (port/formats/descriptions.md): label 12h bytes, 14 car slots of 6 bytes, 8 scene
// slots of 8 bytes, then the selected car slot (u16), a word, the selected scene slot (u16) and the
// skill level 0..8 (u16). A slot is used when its base name starts with C (cars) / S (scenes) and its
// .LST is there; the car's name is the .LST's first 13h bytes, the scene's its first string.
Catalogue ReadCatalogue(const wxString& gameDir) {
    Catalogue cat;
    if (gameDir.empty()) return cat;
    const std::string pd = ReadBytes(gameDir, "PLAYDISK.DAT", 0xB3);
    if (pd.size() < 0xAE) return cat;
    auto word = [&](size_t at) { return static_cast<unsigned char>(pd[at]) | static_cast<unsigned char>(pd[at + 1]) << 8; };
    const int selCar = word(0xA6), selCourse = word(0xAA), skill = word(0xAC);
    for (int i = 0; i < 14; ++i) {
        const wxString code = CString(pd.substr(0x12 + 6 * i), 6);
        if (!code.StartsWith("C") || !FilePresent(gameDir, code + ".LST")) continue;
        if (i == selCar) cat.lastCar = static_cast<int>(cat.cars.size());
        cat.cars.push_back({code, CString(ReadBytes(gameDir, code + ".LST", 0x13), 0x13)});
    }
    for (int i = 0; i < 8; ++i) {
        const wxString code = CString(pd.substr(0x66 + 8 * i), 8);
        if (!code.StartsWith("S") || !FilePresent(gameDir, code + ".LST")) continue;
        if (i == selCourse) cat.lastCourse = static_cast<int>(cat.courses.size());
        cat.courses.push_back({code, CString(ReadBytes(gameDir, code + ".LST", 0x40), 0x40)});
    }
    if (skill <= 8) cat.lastSkill = skill + 1;
    return cat;
}

wxString LauncherDir() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }

wxString DefaultGameDir() { return wxFileName(LauncherDir(), "Game").GetFullPath(); }

wxString DefaultProgram() {
    wxFileName name(LauncherDir(), "testdrive3-enhanced");
#ifdef __WXMSW__
    name.SetExt("exe");
#endif
    return name.GetFullPath();
}

bool LaunchGame(const GameOptions& o, wxString& error) {
    if (!wxFileName::FileExists(o.program)) {
        error = wxString::Format("The game's program isn't there:\n\n%s", o.program);
        return false;
    }
    std::vector<wxString> args{o.program,
                               "--game-dir", o.gameDir,
                               "--scale", wxString::Format("%d", o.scale),
                               "--frame-ticks", wxString::Format("%d", o.frameTicks),
                               "--sound", o.speaker ? "speaker" : "adlib"};
    if (o.fullscreen) args.push_back("--fullscreen");
    args.insert(args.end(), {"--finish-marker", o.finishMarker ? "1" : "0"});
    args.insert(args.end(), {"--res-scale", wxString::Format("%d", o.resScale)});
    if (o.classic) args.push_back("--classic");
    else args.insert(args.end(), {"--aa", wxString::Format("%d", o.aa),
                                  "--motion-delay", wxString::Format("%d", o.motionDelay),
                                  "--haze", wxString::Format("%d", o.haze),
                                  "--draw-distance", wxString::Format("%d", o.drawDistance),
                                  "--far-lod", o.farLod ? "1" : "0",
                                  "--fog-start", wxString::Format("%d", o.fogStart),
                                  "--enh-lights", o.enhLights ? "1" : "0",
                                  "--beam-night", wxString::Format("%d", o.beamNight),
                                  "--beam-day", wxString::Format("%d", o.beamDay),
                                  "--beam-soft", wxString::Format("%d", o.beamSoft)});
    if (!o.keys.empty()) args.insert(args.end(), {"--keys", o.keys});
    if (!o.car.empty()) args.insert(args.end(), {"--car", o.car});
    if (!o.course.empty()) args.insert(args.end(), {"--course", o.course});
    if (o.skill > 0) args.insert(args.end(), {"--skill", wxString::Format("%d", o.skill)});

    std::vector<std::wstring> wide;
    for (const wxString& a : args) wide.push_back(a.ToStdWstring());
    std::vector<const wchar_t*> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    argv.push_back(nullptr);

    wxExecuteEnv env;  // an empty variable map: the game inherits the launcher's environment
    env.cwd = wxFileName(o.program).GetPath();
    long pid;
    {
        wxLogNull quiet;  // wxExecute would show its own error box
        pid = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &env);
    }
    if (pid == 0) {
        error = wxString::Format("Couldn't start %s.\n\n%s", wxFileName(o.program).GetFullName(),
                                 wxSysErrorMsgStr(wxSysErrorCode()));
        return false;
    }
    return true;
}
