// DirectoryWatcher.h — watches a single folder for new files using
// ReadDirectoryChangesW on a dedicated thread.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

namespace rmbg {

// Watches one directory for FILE_ACTION_ADDED events.
// - Unicode throughout (std::wstring).
// - One background thread per watcher instance.
// - Callback is invoked on the watcher thread; callers must marshal to the
//   UI thread themselves (AppState does this via DispatcherQueue).
// - StopWatching() is idempotent and called by the destructor.
class DirectoryWatcher {
public:
    using FileAddedCallback = std::function<void(const std::wstring& fullPath)>;

    DirectoryWatcher() = default;
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    // Starts watching `folder`. Returns false if the folder cannot be
    // opened. Stops any previous watch first.
    bool StartWatching(const std::wstring& folder, FileAddedCallback onFileAdded);

    // Signals the watcher thread and joins it. Safe to call repeatedly.
    void StopWatching();

    bool IsWatching() const noexcept { return m_running.load(); }
    const std::wstring& WatchedFolder() const noexcept { return m_folder; }

private:
    void WatchLoop();

    std::wstring m_folder;
    FileAddedCallback m_callback;
    std::mutex m_callbackMutex;         // guards m_callback against Stop/callback races
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    HANDLE m_directoryHandle = INVALID_HANDLE_VALUE;
    HANDLE m_stopEvent = nullptr;       // signaled by StopWatching()
};

} // namespace rmbg
