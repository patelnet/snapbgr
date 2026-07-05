// Settings.cpp — see Settings.h.
#include "pch.h"
#include "Settings.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace rmbg {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                          out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                          nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                          out.data(), len);
    return out;
}

namespace {
fs::path SettingsDirectory() {
    PWSTR raw = nullptr;
    fs::path dir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        dir = fs::path(raw) / L"SnapBGR";
    } else {
        dir = fs::temp_directory_path() / L"SnapBGR"; // defensive fallback
    }
    if (raw) ::CoTaskMemFree(raw);
    return dir;
}

// Pre-rename data folder (the app was previously "BackgroundRemover").
// Used once to migrate existing user settings.
fs::path LegacySettingsDirectory() {
    PWSTR raw = nullptr;
    fs::path dir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        dir = fs::path(raw) / L"BackgroundRemover";
    }
    if (raw) ::CoTaskMemFree(raw);
    return dir;
}
} // namespace

std::wstring Settings::SettingsFilePath() {
    return (SettingsDirectory() / L"settings.json").wstring();
}

Settings::Settings() {
    // Sensible defaults: user's Pictures folder for input, a subfolder for output.
    PWSTR raw = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &raw))) {
        m_watchedFolder = raw;
        m_outputFolder = (fs::path(raw) / L"BackgroundRemoved").wstring();
    }
    if (raw) ::CoTaskMemFree(raw);
}

void Settings::Load() {
    try {
        const fs::path file = SettingsDirectory() / L"settings.json";

        // One-time migration from the pre-rename "BackgroundRemover" data
        // folder: copy settings (and any drop-in models) if the new
        // location has no settings yet. The old folder is left untouched.
        if (!fs::exists(file)) {
            const fs::path legacy = LegacySettingsDirectory();
            std::error_code ec;
            if (!legacy.empty() && fs::exists(legacy / L"settings.json", ec)) {
                fs::create_directories(file.parent_path(), ec);
                fs::copy_file(legacy / L"settings.json", file,
                              fs::copy_options::skip_existing, ec);
                if (fs::exists(legacy / L"models", ec)) {
                    fs::create_directories(file.parent_path() / L"models", ec);
                    fs::copy(legacy / L"models", file.parent_path() / L"models",
                             fs::copy_options::skip_existing, ec);
                }
            }
        }

        std::ifstream in(file);
        if (!in.is_open()) return; // first run — keep defaults

        json j = json::parse(in, /*cb=*/nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded()) return; // corrupt file — keep defaults

        if (j.contains("watchedFolder") && j["watchedFolder"].is_string())
            m_watchedFolder = ToWide(j["watchedFolder"].get<std::string>());
        if (j.contains("outputFolder") && j["outputFolder"].is_string())
            m_outputFolder = ToWide(j["outputFolder"].get<std::string>());
        if (j.contains("modelPath") && j["modelPath"].is_string())
            m_modelPath = ToWide(j["modelPath"].get<std::string>());
        if (j.contains("outputFormat") && j["outputFormat"].is_string()) {
            const auto f = ToWide(j["outputFormat"].get<std::string>());
            if (f == L"png" || f == L"jpg") m_outputFormat = f;
        }
        if (j.contains("autoStart") && j["autoStart"].is_boolean())
            m_autoStart = j["autoStart"].get<bool>();
    } catch (...) {
        // Never let settings I/O crash the app; defaults remain in effect.
    }
}

void Settings::Save() const {
    try {
        const fs::path dir = SettingsDirectory();
        fs::create_directories(dir);

        json j;
        j["watchedFolder"] = ToUtf8(m_watchedFolder);
        j["outputFolder"] = ToUtf8(m_outputFolder);
        j["modelPath"] = ToUtf8(m_modelPath);
        j["outputFormat"] = ToUtf8(m_outputFormat);
        j["autoStart"] = m_autoStart;

        // Write to a temp file first, then swap it in, to avoid torn writes.
        const fs::path tmp = dir / L"settings.json.tmp";
        const fs::path dest = dir / L"settings.json";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << j.dump(2);
        }
        // std::filesystem::rename fails if dest exists on Windows in some
        // implementations; MoveFileExW with REPLACE_EXISTING is reliable.
        ::MoveFileExW(tmp.c_str(), dest.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } catch (...) {
        // Non-fatal; the app keeps running with in-memory settings.
    }
}

void Settings::SetWatchedFolder(std::wstring folder) {
    m_watchedFolder = std::move(folder);
    Save();
}

void Settings::SetOutputFolder(std::wstring folder) {
    m_outputFolder = std::move(folder);
    Save();
}

void Settings::SetModelPath(std::wstring path) {
    m_modelPath = std::move(path);
    Save();
}

void Settings::SetOutputFormat(std::wstring format) {
    m_outputFormat = (format == L"jpg") ? L"jpg" : L"png";
    Save();
}

void Settings::SetAutoStart(bool value) {
    m_autoStart = value;
    Save();
}

} // namespace rmbg
