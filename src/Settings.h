// Settings.h — JSON settings persisted to %LOCALAPPDATA%\SnapBGR\settings.json.
#pragma once

#include <string>

namespace rmbg {

// Application settings. Loaded on startup, saved on every change.
// All paths are stored as UTF-8 in JSON and exposed as std::wstring.
class Settings {
public:
    Settings();

    // Loads settings.json if present; missing/corrupt files fall back to
    // defaults without throwing.
    void Load();

    // Saves via write-temp-then-rename. Creates the
    // %LOCALAPPDATA%\SnapBGR directory if needed.
    void Save() const;

    const std::wstring& WatchedFolder() const noexcept { return m_watchedFolder; }
    const std::wstring& OutputFolder() const noexcept { return m_outputFolder; }
    const std::wstring& ModelPath() const noexcept { return m_modelPath; }
    // Output format: L"png" (transparent, default) or L"jpg" (white background).
    const std::wstring& OutputFormat() const noexcept { return m_outputFormat; }
    // CPU throttle mode: L"normal" (default), L"low" (below-normal priority,
    // half the cores) or L"efficiency" (EcoQoS power throttling, one core).
    const std::wstring& CpuThrottle() const noexcept { return m_cpuThrottle; }
    bool AutoStart() const noexcept { return m_autoStart; }

    void SetWatchedFolder(std::wstring folder);
    void SetOutputFolder(std::wstring folder);
    void SetModelPath(std::wstring path);
    void SetOutputFormat(std::wstring format);
    void SetCpuThrottle(std::wstring mode);
    void SetAutoStart(bool value);

    // Full path to settings.json (for diagnostics/UI).
    static std::wstring SettingsFilePath();

private:
    std::wstring m_watchedFolder;
    std::wstring m_outputFolder;
    std::wstring m_modelPath;   // empty = look next to the EXE
    std::wstring m_outputFormat = L"png";
    std::wstring m_cpuThrottle = L"normal";
    bool m_autoStart = false;
};

// UTF-8 <-> UTF-16 helpers shared across the codebase.
std::string ToUtf8(const std::wstring& w);
std::wstring ToWide(const std::string& s);

} // namespace rmbg
