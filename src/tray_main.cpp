// tray_main.cpp — standalone Win32 tray application for SnapBGR.
//
// This is the CMake-buildable GUI app packaged by the MSI. It runs in the
// system tray (it is NOT a Windows service) and reuses the same core
// components as the WinUI app: Settings, DirectoryWatcher,
// BackgroundRemovalService, TrayController.
//
// Threading model:
//   - Main thread: message loop + tray icon + pickers (Shell_NotifyIcon and
//     IFileDialog are thread-affine, so all of that happens here).
//   - Watcher thread (DirectoryWatcher): enqueues new file paths.
//   - Worker thread: runs the heavy ProcessImage pipeline off the UI and
//     watcher threads, then marshals results back to the main thread via
//     PostThreadMessage + a mutex-guarded queue.
#include "pch.h"

#include "BackgroundRemovalService.h"
#include "DirectoryWatcher.h"
#include "Settings.h"
#include "TrayController.h"

#include <shobjidl.h>

#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <queue>

namespace {

// Posted to the main thread when a UI notification is ready.
constexpr UINT WM_APP_NOTIFY = WM_APP + 100;

constexpr wchar_t kModelsHelpUrl[] =
    L"https://github.com/patelnet/snapbgr/blob/main/models/README.md";

struct Notification {
    std::wstring title;
    std::wstring message;
    bool isError = false;
};

// Cross-thread notification queue (worker -> main thread).
std::mutex g_notifyMutex;
std::deque<Notification> g_notifications;
DWORD g_mainThreadId = 0;

void PostNotification(std::wstring title, std::wstring message, bool isError = false) {
    {
        std::lock_guard<std::mutex> lock(g_notifyMutex);
        g_notifications.push_back({std::move(title), std::move(message), isError});
    }
    ::PostThreadMessageW(g_mainThreadId, WM_APP_NOTIFY, 0, 0);
}

std::wstring ExeDirectory() {
    wchar_t buf[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path().wstring();
}

// Modern folder/file picker (IFileOpenDialog). Returns nullopt on cancel.
// Must run on a COM-initialized STA thread (the main thread here).
std::optional<std::wstring> ShowPicker(bool pickFolders, const wchar_t* title,
                                       const std::wstring& initialDir = {}) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dlg)))) {
        return std::nullopt;
    }
    std::optional<std::wstring> result;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (pickFolders) opts |= FOS_PICKFOLDERS;
    dlg->SetOptions(opts);
    dlg->SetTitle(title);

    if (!pickFolders) {
        static const COMDLG_FILTERSPEC filters[] = {
            {L"ONNX models (*.onnx)", L"*.onnx"},
            {L"All files (*.*)", L"*.*"},
        };
        dlg->SetFileTypes(ARRAYSIZE(filters), filters);
    }

    if (!initialDir.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(::SHCreateItemFromParsingName(initialDir.c_str(), nullptr,
                                                    IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }

    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                ::CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

// Simple single-worker task queue so image processing never blocks the
// watcher or UI threads. Exposes queue depth + current item for the
// status display in the tray menu.
class WorkQueue {
public:
    void Start(std::function<void(const std::wstring&)> handler) {
        m_handler = std::move(handler);
        m_running = true;
        m_thread = std::thread([this] { Run(); });
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }

    void Enqueue(std::wstring path) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_items.push(std::move(path));
        }
        m_cv.notify_one();
    }

    size_t Pending() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    // Filename currently being processed, or empty.
    std::wstring Current() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_current;
    }

    ~WorkQueue() { Stop(); }

private:
    void Run() {
        for (;;) {
            std::wstring item;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_running || !m_items.empty(); });
                if (!m_running && m_items.empty()) return;
                if (m_items.empty()) continue;
                item = std::move(m_items.front());
                m_items.pop();
                m_current = std::filesystem::path(item).filename().wstring();
            }
            m_handler(item);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_current.clear();
            }
        }
    }

    std::function<void(const std::wstring&)> m_handler;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::wstring> m_items;
    std::wstring m_current;
    bool m_running = false;
};

bool IsImageFile(const std::wstring& path) {
    auto ext = std::filesystem::path(path).extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
    return ext == L".jpg" || ext == L".jpeg" || ext == L".png" ||
           ext == L".bmp" || ext == L".webp" || ext == L".tif" || ext == L".tiff";
}

std::wstring Abbreviate(const std::wstring& path, size_t max = 48) {
    if (path.size() <= max) return path;
    return L"…" + path.substr(path.size() - max + 1);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Single instance: a second launch just exits (the tray icon already
    // provides all interaction points).
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, L"Local\\SnapBGRTrayApp");
    if (!mutex || ::GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    g_mainThreadId = ::GetCurrentThreadId();

    // STA COM for IFileOpenDialog (pickers run on this thread).
    const HRESULT comInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    rmbg::Settings settings;
    settings.Load();

    // Make sure the watched/output folders exist so first run works out of
    // the box (defaults: Pictures and Pictures\BackgroundRemoved).
    std::error_code ec;
    std::filesystem::create_directories(settings.WatchedFolder(), ec);
    std::filesystem::create_directories(settings.OutputFolder(), ec);
    settings.Save();

    rmbg::BackgroundRemovalService service;
    // The worker thread runs inference while the main thread may reload the
    // model (Select Model…); serialize access.
    std::mutex serviceMutex;

    // Tracks which model file actually loaded, for the status display.
    std::wstring activeModelPath;

    // Model resolution order — no fixed filename required:
    //   1. The explicitly selected model (Select Model…, persisted).
    //   2. Any *.onnx in %LOCALAPPDATA%\SnapBGR\models
    //      (drop-in folder; no rename needed).
    //   3. Any *.onnx next to the EXE.
    // Candidates are tried alphabetically until one loads; missing/invalid
    // models fall back to the synthetic mask.
    auto loadModel = [&]() -> bool {
        std::lock_guard<std::mutex> lock(serviceMutex);
        activeModelPath.clear();

        if (!settings.ModelPath().empty() && service.LoadModel(settings.ModelPath())) {
            activeModelPath = settings.ModelPath();
            return true;
        }

        std::vector<std::filesystem::path> searchDirs = {
            std::filesystem::path(rmbg::Settings::SettingsFilePath()).parent_path() / L"models",
            ExeDirectory(),
        };
        for (const auto& dir : searchDirs) {
            std::error_code iterEc;
            std::vector<std::filesystem::path> candidates;
            for (const auto& entry : std::filesystem::directory_iterator(dir, iterEc)) {
                if (!entry.is_regular_file(iterEc)) continue;
                auto ext = entry.path().extension().wstring();
                for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
                if (ext == L".onnx") candidates.push_back(entry.path());
            }
            std::sort(candidates.begin(), candidates.end());
            for (const auto& candidate : candidates) {
                if (service.LoadModel(candidate.wstring())) {
                    activeModelPath = candidate.wstring();
                    return true;
                }
            }
        }
        return false;
    };
    loadModel();

    rmbg::DirectoryWatcher watcher;
    WorkQueue work;

    // Counters for the status display (main thread reads, worker writes).
    std::atomic<int> processedCount{0};
    std::atomic<int> failedCount{0};

    work.Start([&](const std::wstring& path) {
        if (!IsImageFile(path)) return;
        // New files may still be mid-copy; wait until the file opens for
        // exclusive read or ~3 s elapse.
        for (int i = 0; i < 30; ++i) {
            HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) { ::CloseHandle(h); break; }
            ::Sleep(100);
        }
        std::optional<std::wstring> out;
        {
            std::lock_guard<std::mutex> lock(serviceMutex);
            out = service.ProcessImage(path, settings.OutputFolder(),
                                       settings.OutputFormat());
        }
        auto name = std::filesystem::path(path).filename().wstring();
        if (out) {
            ++processedCount;
            PostNotification(L"Background removed",
                             name + L" -> " +
                             std::filesystem::path(*out).filename().wstring());
        } else {
            ++failedCount;
            PostNotification(L"Processing failed", name, /*isError=*/true);
        }
    });

    rmbg::TrayController tray;
    const std::wstring iconPath = ExeDirectory() + L"\\assets\\appicon.ico";
    if (!tray.Initialize(L"SnapBGR — starting…", iconPath)) {
        if (SUCCEEDED(comInit)) ::CoUninitialize();
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
        return 1;
    }

    auto startWatching = [&] {
        if (watcher.IsWatching()) return;
        const bool ok = watcher.StartWatching(
            settings.WatchedFolder(),
            [&](const std::wstring& fullPath) {
                // Skip our own outputs so watch folder == output folder
                // does not loop forever on freshly written PNGs.
                if (rmbg::BackgroundRemovalService::IsGeneratedOutput(fullPath)) return;
                work.Enqueue(fullPath);
            });
        if (ok) {
            tray.SetTooltip(L"SnapBGR — watching " + settings.WatchedFolder());
            tray.ShowBalloon(L"Watching started", settings.WatchedFolder());
        } else {
            tray.SetTooltip(L"SnapBGR — stopped");
            tray.ShowBalloon(L"Could not watch folder", settings.WatchedFolder(),
                             /*isError=*/true);
        }
    };
    auto stopWatching = [&] {
        watcher.StopWatching();
        tray.SetTooltip(L"SnapBGR — stopped");
        tray.ShowBalloon(L"Watching stopped", L"");
    };

    // Status header rendered at the top of the tray menu.
    tray.SetStatusProvider([&]() -> std::vector<std::wstring> {
        std::vector<std::wstring> lines;
        lines.push_back(watcher.IsWatching()
                            ? L"Status: watching " + Abbreviate(settings.WatchedFolder())
                            : L"Status: stopped");
        const auto current = work.Current();
        const auto pending = work.Pending();
        if (!current.empty()) {
            lines.push_back(L"Processing: " + Abbreviate(current, 40) +
                            L"  (" + std::to_wstring(pending) + L" queued)");
        } else {
            lines.push_back(L"Queue: " + std::to_wstring(pending) + L" file(s)");
        }
        lines.push_back(L"Done: " + std::to_wstring(processedCount.load()) +
                        L"   Failed: " + std::to_wstring(failedCount.load()));
        {
            std::lock_guard<std::mutex> lock(serviceMutex);
            lines.push_back(service.IsModelLoaded()
                                ? L"Model: " + Abbreviate(
                                      std::filesystem::path(activeModelPath)
                                          .filename().wstring()) +
                                      L" (" + service.ProfileSummary() + L")"
                                : L"Model: none (synthetic mask)");
        }
        lines.push_back(settings.OutputFormat() == L"jpg"
                            ? L"Format: JPG (white background)"
                            : L"Format: PNG (transparent)");
        return lines;
    });

    tray.SetOnStart(startWatching);
    tray.SetOnStop(stopWatching);
    tray.SetOnOpenOutput([&] {
        ::ShellExecuteW(nullptr, L"open", settings.OutputFolder().c_str(),
                        nullptr, nullptr, SW_SHOWNORMAL);
    });
    tray.SetOnSelectWatched([&] {
        auto dir = ShowPicker(true, L"Select the folder to watch for new images",
                              settings.WatchedFolder());
        if (!dir) return;
        settings.SetWatchedFolder(*dir);
        // Restart the watcher on the new folder if it was running.
        const bool wasWatching = watcher.IsWatching();
        watcher.StopWatching();
        if (wasWatching) startWatching();
        else tray.ShowBalloon(L"Watched folder set", *dir);
    });
    tray.SetOnSelectOutput([&] {
        auto dir = ShowPicker(true, L"Select the output folder for transparent PNGs",
                              settings.OutputFolder());
        if (!dir) return;
        std::error_code dirEc;
        std::filesystem::create_directories(*dir, dirEc);
        settings.SetOutputFolder(*dir);
        tray.ShowBalloon(L"Output folder set", *dir);
    });
    tray.SetOnSelectModel([&] {
        auto file = ShowPicker(false, L"Select an ONNX background-removal model "
                                      L"(MODNet, U2-Net, IS-Net, BiRefNet, RMBG, …)");
        if (!file) return;
        settings.SetModelPath(*file);
        if (loadModel()) {
            tray.ShowBalloon(L"Model loaded",
                             std::filesystem::path(*file).filename().wstring());
        } else {
            tray.ShowBalloon(L"Model failed to load — using synthetic mask",
                             std::filesystem::path(*file).filename().wstring(),
                             /*isError=*/true);
        }
    });
    tray.SetOnGetModels([&] {
        // Opens the models guide: download links + license checklist for
        // compatible matting models (MODNet etc.).
        ::ShellExecuteW(nullptr, L"open", kModelsHelpUrl, nullptr, nullptr, SW_SHOWNORMAL);
    });
    tray.SetFormatProvider([&] { return settings.OutputFormat(); });
    tray.SetOnSelectFormat([&](const std::wstring& fmt) {
        settings.SetOutputFormat(fmt);
        tray.ShowBalloon(L"Output format set",
                         fmt == L"jpg" ? L"JPG (white background)"
                                       : L"PNG (transparent)");
    });
    tray.SetOnExit([] { ::PostQuitMessage(0); });

    // Start watching immediately so drop-a-file-in-Pictures "just works".
    startWatching();

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_APP_NOTIFY && msg.hwnd == nullptr) {
            // Drain the queue on the tray (main) thread.
            for (;;) {
                Notification n;
                {
                    std::lock_guard<std::mutex> lock(g_notifyMutex);
                    if (g_notifications.empty()) break;
                    n = std::move(g_notifications.front());
                    g_notifications.pop_front();
                }
                tray.ShowBalloon(n.title, n.message, n.isError);
            }
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    watcher.StopWatching();
    work.Stop();
    tray.Shutdown();
    if (SUCCEEDED(comInit)) ::CoUninitialize();
    ::ReleaseMutex(mutex);
    ::CloseHandle(mutex);
    return 0;
}
