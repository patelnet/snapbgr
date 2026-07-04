// AppState.h — central coordinator wiring Settings, DirectoryWatcher,
// BackgroundRemovalService, and TrayController together.
#pragma once

#include "BackgroundRemovalService.h"
#include "DirectoryWatcher.h"
#include "Settings.h"
#include "TrayController.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#ifdef BACKGROUNDREMOVER_WINUI
#include <winrt/Microsoft.UI.Dispatching.h>
#endif

namespace rmbg {

// Application-wide state and orchestration. Created once at startup
// (App.xaml.cpp) and shared with the UI pages. Documented singleton-style
// access is provided via Instance() for XAML code-behind convenience.
//
// Threading model:
// - DirectoryWatcher callbacks arrive on the watcher thread.
// - Image processing runs on a detached background thread per file.
// - All UI-facing callbacks (status/log) are marshaled through
//   DispatcherQueue.TryEnqueue when a dispatcher is attached.
class AppState {
public:
    using StatusCallback = std::function<void(const std::wstring&)>;
    using LogCallback = std::function<void(const std::wstring&)>;

    AppState();
    ~AppState();

    // Documented global accessor. App.xaml.cpp creates the instance;
    // pages retrieve it here instead of plumbing pointers through XAML.
    static AppState& Instance();

#ifdef BACKGROUNDREMOVER_WINUI
    // Attach the UI thread dispatcher; must be called from the UI thread
    // before Start/Stop so status and log updates marshal correctly.
    void SetDispatcherQueue(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue) {
        m_dispatcher = queue;
    }
#endif

    // --- Watching lifecycle --------------------------------------------------
    bool StartWatching();
    void StopWatching();
    bool IsWatching() const { return m_watcher.IsWatching(); }

    // Called by DirectoryWatcher (watcher thread) for each new file.
    // Kicks off processing on a background thread.
    void OnNewFile(std::wstring fullPath);

    // Process one file immediately (used by drag-and-drop). Runs on a
    // background thread; results surface via status/log callbacks.
    void ProcessFileAsync(std::wstring fullPath);

    // --- UI plumbing ----------------------------------------------------------
    void UpdateStatus(const std::wstring& status);
    void Log(const std::wstring& message);
    void SetStatusChangedCallback(StatusCallback cb);
    void SetLogCallback(LogCallback cb);

    // --- Settings passthrough -------------------------------------------------
    void SetWatchedFolder(const std::wstring& folder);
    void SetOutputFolder(const std::wstring& folder);
    std::wstring WatchedFolder() const { return m_settings.WatchedFolder(); }
    std::wstring OutputFolder() const { return m_settings.OutputFolder(); }

    Settings& GetSettings() { return m_settings; }
    TrayController& Tray() { return m_tray; }
    BackgroundRemovalService& Service() { return m_service; }

    // Loads settings + model, wires tray callbacks. Call once at startup.
    void Initialize();
    void Shutdown();

private:
    // Runs a callback on the UI thread when a dispatcher is attached,
    // otherwise invokes inline (console-test scenario).
    void RunOnUiThread(std::function<void()> fn);

    Settings m_settings;
    DirectoryWatcher m_watcher;
    BackgroundRemovalService m_service;
    TrayController m_tray;

    std::mutex m_callbackMutex;
    StatusCallback m_statusCallback;
    LogCallback m_logCallback;

#ifdef BACKGROUNDREMOVER_WINUI
    winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{nullptr};
#endif
};

} // namespace rmbg
