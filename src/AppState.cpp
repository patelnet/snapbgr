// AppState.cpp — see AppState.h.
#include "pch.h"
#include "AppState.h"

#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace rmbg {

namespace {
AppState* g_instance = nullptr; // documented singleton; created in ctor
} // namespace

AppState::AppState() {
    g_instance = this;
}

AppState::~AppState() {
    Shutdown();
    if (g_instance == this) g_instance = nullptr;
}

AppState& AppState::Instance() {
    // App.xaml.cpp constructs the AppState before any page can call this.
    return *g_instance;
}

void AppState::Initialize() {
    m_settings.Load();

    // The model lives next to the executable ("modnet.onnx", copied by the
    // build) with a fallback to the repo-relative models folder for dev
    // runs. A missing/placeholder model simply enables the synthetic mask.
    wchar_t exePath[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const fs::path exeDir = fs::path(exePath).parent_path();

    bool loaded = m_service.LoadModel((exeDir / L"modnet.onnx").wstring());
    if (!loaded) {
        loaded = m_service.LoadModel((exeDir / L"models" / L"modnet.onnx").wstring());
    }
    Log(loaded ? L"ONNX model loaded."
               : L"Model not available - using synthetic mask fallback.");

    // Tray menu -> AppState actions.
    m_tray.SetOnStart([this] { StartWatching(); });
    m_tray.SetOnStop([this] { StopWatching(); });
    m_tray.SetOnOpenOutput([this] {
        const std::wstring out = OutputFolder();
        std::error_code ec;
        fs::create_directories(out, ec); // best-effort so Explorer can open it
        ::ShellExecuteW(nullptr, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    // Exit is wired by the app layer (needs to close the WinUI window).

    m_tray.Initialize(L"SnapBGR - idle");
    UpdateStatus(L"Idle");
}

void AppState::Shutdown() {
    StopWatching();
    m_tray.Shutdown();
}

bool AppState::StartWatching() {
    const std::wstring folder = m_settings.WatchedFolder();
    if (folder.empty() || !fs::exists(folder)) {
        UpdateStatus(L"Watch folder does not exist");
        Log(L"Cannot start: watch folder missing: " + folder);
        m_tray.ShowBalloon(L"SnapBGR", L"Watch folder does not exist.", true);
        return false;
    }

    const bool ok = m_watcher.StartWatching(
        folder, [this](const std::wstring& path) { OnNewFile(path); });
    if (ok) {
        UpdateStatus(L"Watching: " + folder);
        Log(L"Started watching " + folder);
        m_tray.SetTooltip(L"SnapBGR - watching");
    } else {
        UpdateStatus(L"Failed to start watching");
        Log(L"ReadDirectoryChangesW setup failed for " + folder);
        m_tray.ShowBalloon(L"SnapBGR", L"Failed to watch folder.", true);
    }
    return ok;
}

void AppState::StopWatching() {
    if (!m_watcher.IsWatching()) return;
    m_watcher.StopWatching();
    UpdateStatus(L"Stopped");
    Log(L"Stopped watching.");
    m_tray.SetTooltip(L"SnapBGR - idle");
}

void AppState::OnNewFile(std::wstring fullPath) {
    // Watcher-thread context: hand off to a worker immediately so the
    // watcher loop keeps draining events.
    ProcessFileAsync(std::move(fullPath));
}

void AppState::ProcessFileAsync(std::wstring fullPath) {
    Log(L"New file: " + fullPath);

    std::thread([this, path = std::move(fullPath)] {
        // A file that was just FILE_ACTION_ADDED may still be mid-copy;
        // retry opening exclusively for a short window before processing.
        for (int attempt = 0; attempt < 10; ++attempt) {
            HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, 0 /*exclusive*/,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                ::CloseHandle(h);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        UpdateStatus(L"Processing " + fs::path(path).filename().wstring());
        const auto result = m_service.ProcessImage(path, OutputFolder());
        if (result) {
            Log(L"Saved: " + *result);
            UpdateStatus(L"Done");
            m_tray.ShowBalloon(L"SnapBGR",
                               L"Processed " + fs::path(path).filename().wstring());
        } else {
            // Non-image files landing in the folder are expected; log quietly.
            Log(L"Skipped (not an image or failed): " + path);
            UpdateStatus(m_watcher.IsWatching() ? L"Watching: " + WatchedFolder()
                                                : L"Idle");
        }
    }).detach();
}

void AppState::UpdateStatus(const std::wstring& status) {
    StatusCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_statusCallback;
    }
    if (cb) RunOnUiThread([cb, status] { cb(status); });
}

void AppState::Log(const std::wstring& message) {
    LogCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        cb = m_logCallback;
    }
    if (cb) RunOnUiThread([cb, message] { cb(message); });
}

void AppState::SetStatusChangedCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_statusCallback = std::move(cb);
}

void AppState::SetLogCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_logCallback = std::move(cb);
}

void AppState::SetWatchedFolder(const std::wstring& folder) {
    m_settings.SetWatchedFolder(folder); // persists immediately
    // If currently watching, restart on the new folder.
    if (m_watcher.IsWatching()) {
        StopWatching();
        StartWatching();
    }
}

void AppState::SetOutputFolder(const std::wstring& folder) {
    m_settings.SetOutputFolder(folder); // persists immediately
}

void AppState::RunOnUiThread(std::function<void()> fn) {
#ifdef BACKGROUNDREMOVER_WINUI
    if (m_dispatcher) {
        // TryEnqueue returns false when the queue is shutting down; drop
        // the update in that case rather than touching dead UI.
        m_dispatcher.TryEnqueue([fn = std::move(fn)] { fn(); });
        return;
    }
#endif
    fn(); // no dispatcher (console test) — invoke inline
}

} // namespace rmbg
