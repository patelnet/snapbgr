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

// A unit of processing work. `outputDir` empty means "use the configured
// output folder"; on-demand context-menu requests set it to the source
// file's own folder so results land next to the original.
struct WorkItem {
    std::wstring path;
    std::wstring outputDir;
};

// Simple single-worker task queue so image processing never blocks the
// watcher or UI threads. Exposes queue depth + current item for the
// status display in the tray menu.
class WorkQueue {
public:
    void Start(std::function<void(const WorkItem&)> handler) {
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

    void Enqueue(WorkItem item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_items.push(std::move(item));
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
            WorkItem item;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_running || !m_items.empty(); });
                if (!m_running && m_items.empty()) return;
                if (m_items.empty()) continue;
                item = std::move(m_items.front());
                m_items.pop();
                m_current = std::filesystem::path(item.path).filename().wstring();
            }
            m_handler(item);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_current.clear();
            }
        }
    }

    std::function<void(const WorkItem&)> m_handler;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<WorkItem> m_items;
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

// Threads to allow for image processing under a given throttle mode.
// 0 = library default (all cores).
int ThreadsForThrottle(const std::wstring& mode) {
    if (mode == L"efficiency") return 1;
    if (mode == L"low") {
        const unsigned hw = std::thread::hardware_concurrency();
        return static_cast<int>(hw > 1 ? hw / 2 : 1);
    }
    return 0;
}

// Applies the process-wide half of the throttle: scheduling priority and
// EcoQoS power throttling (Windows 11 / Windows 10 1709+; the call is a
// harmless no-op on older builds). Thread-pool limits are applied
// separately via BackgroundRemovalService::SetMaxThreads and
// cv::setNumThreads.
void ApplyProcessThrottle(const std::wstring& mode) {
    DWORD priority = NORMAL_PRIORITY_CLASS;
    if (mode == L"low") priority = BELOW_NORMAL_PRIORITY_CLASS;
    else if (mode == L"efficiency") priority = IDLE_PRIORITY_CLASS;
    ::SetPriorityClass(::GetCurrentProcess(), priority);

#ifdef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    // EcoQoS: schedules the process on efficiency cores at reduced clocks.
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = (mode == L"efficiency")
                          ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED
                          : 0; // 0 with the mask set = explicitly off
    ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling,
                            &state, sizeof(state));
#endif
}

const wchar_t* ThrottleLabel(const std::wstring& mode) {
    if (mode == L"efficiency") return L"Efficiency (power saving, single core)";
    if (mode == L"low") return L"Low (background priority, half the cores)";
    return L"Normal (full speed)";
}

// --- Start-on-login via the per-user Run key -------------------------------
// HKCU\...\Run is the single source of truth: the installer's finish-page
// checkbox writes the same value, so the tray checkmark always reflects
// reality regardless of how autostart was enabled.
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"SnapBGR";

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS st = ::RegQueryValueExW(key, kRunValue, nullptr, nullptr,
                                          nullptr, nullptr);
    ::RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

bool SetAutostartEnabled(bool enable) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    LSTATUS st;
    if (enable) {
        // Quote the path: Run entries are parsed as command lines.
        wchar_t exe[MAX_PATH]{};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        st = ::RegSetValueExW(key, kRunValue, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(cmd.c_str()),
                              static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = ::RegDeleteValueW(key, kRunValue);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS; // already off
    }
    ::RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

// Model resolution order — no fixed filename required:
//   1. The explicitly selected model (Select Model…, persisted).
//   2. Any *.onnx in %LOCALAPPDATA%\SnapBGR\models (drop-in; no rename).
//   3. Any *.onnx next to the EXE.
// Candidates are tried alphabetically until one loads. Returns the path
// that loaded, or empty (the service then uses the synthetic mask).
std::wstring LoadBestModel(rmbg::BackgroundRemovalService& service,
                           const rmbg::Settings& settings) {
    if (!settings.ModelPath().empty() && service.LoadModel(settings.ModelPath())) {
        return settings.ModelPath();
    }
    const std::vector<std::filesystem::path> searchDirs = {
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
                return candidate.wstring();
            }
        }
    }
    return {};
}

// --- Explorer context menu ("Remove Background") ---------------------------
// Registered per-user under SystemFileAssociations for each supported image
// extension — no elevation needed, no shell extension DLL. The command
// re-launches the EXE with `--process "<file>"`.
constexpr const wchar_t* kContextExts[] = {L".jpg", L".jpeg", L".png", L".bmp",
                                           L".webp", L".tif", L".tiff"};
constexpr wchar_t kContextVerb[] = L"SnapBGR.RemoveBackground";

std::wstring ContextKeyForExt(const std::wstring& ext) {
    return L"Software\\Classes\\SystemFileAssociations\\" + ext +
           L"\\shell\\" + kContextVerb;
}

void SetRegString(const std::wstring& key, const wchar_t* name,
                  const std::wstring& value) {
    HKEY h = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0,
                          KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        ::RegSetValueExW(h, name, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(value.c_str()),
                         static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(h);
    }
}

// Idempotent; called on every startup so the registered EXE path stays
// current even if the app is moved (portable use).
void RegisterContextMenu() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring exePath = exe;
    for (const auto* ext : kContextExts) {
        const std::wstring key = ContextKeyForExt(ext);
        SetRegString(key, nullptr, L"Remove Background");
        SetRegString(key, L"Icon", exePath); // menu shows the EXE's icon
        SetRegString(key + L"\\command", nullptr,
                     L"\"" + exePath + L"\" --process \"%1\"");
    }
}

void UnregisterContextMenu() {
    for (const auto* ext : kContextExts) {
        ::RegDeleteTreeW(HKEY_CURRENT_USER, ContextKeyForExt(ext).c_str());
    }
}

// Handles `--process <file>` (Explorer context menu). The result is written
// NEXT TO the original file, not to the configured output folder. If the
// tray app is running, the request is forwarded to it (shared queue, status
// display, balloons); otherwise the file is processed here, one-shot.
int HandleOnDemand(const std::wstring& file) {
    if (HWND hwnd = ::FindWindowW(rmbg::TrayController::kWindowClassName, nullptr)) {
        COPYDATASTRUCT cds{};
        cds.dwData = 1;
        cds.lpData = const_cast<wchar_t*>(file.c_str());
        cds.cbData = static_cast<DWORD>((file.size() + 1) * sizeof(wchar_t));
        ::SendMessageW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
        return 0;
    }

    rmbg::Settings settings;
    settings.Load();
    const int threads = ThreadsForThrottle(settings.CpuThrottle());
    ApplyProcessThrottle(settings.CpuThrottle());
    cv::setNumThreads(threads > 0 ? threads : -1);

    rmbg::BackgroundRemovalService service;
    service.SetMaxThreads(threads);
    LoadBestModel(service, settings);

    const std::wstring outDir = std::filesystem::path(file).parent_path().wstring();
    const auto out = service.ProcessImage(file, outDir, settings.OutputFormat());
    if (!out) {
        ::MessageBoxW(nullptr, (L"Could not remove the background from:\n" + file).c_str(),
                      L"SnapBGR", MB_ICONERROR | MB_OK);
        return 1;
    }
    return 0; // success is silent — the result appears next to the original
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Command-line modes (used by the Explorer context menu / uninstaller)
    // are handled before the single-instance check.
    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        std::wstring processFile;
        bool unregister = false;
        for (int i = 1; argv && i < argc; ++i) {
            if (::_wcsicmp(argv[i], L"--process") == 0 && i + 1 < argc) {
                processFile = argv[++i];
            } else if (::_wcsicmp(argv[i], L"--unregister") == 0) {
                unregister = true;
            }
        }
        if (argv) ::LocalFree(argv);
        if (unregister) { // MSI uninstall cleanup
            UnregisterContextMenu();
            return 0;
        }
        if (!processFile.empty()) {
            return HandleOnDemand(processFile);
        }
    }

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

    // Keep the Explorer "Remove Background" context menu registered and
    // pointing at this EXE (per-user, idempotent).
    RegisterContextMenu();

    rmbg::BackgroundRemovalService service;
    // The worker thread runs inference while the main thread may reload the
    // model (Select Model…); serialize access.
    std::mutex serviceMutex;

    // Tracks which model file actually loaded, for the status display.
    std::wstring activeModelPath;

    // Applies the persisted CPU throttle: process priority + EcoQoS, plus
    // thread caps for OpenCV and (via SetMaxThreads) the next ORT session.
    auto applyThrottle = [&](const std::wstring& mode) {
        const int threads = ThreadsForThrottle(mode);
        ApplyProcessThrottle(mode);
        // OpenCV: negative resets to the default (all cores).
        cv::setNumThreads(threads > 0 ? threads : -1);
        std::lock_guard<std::mutex> lock(serviceMutex);
        service.SetMaxThreads(threads);
    };
    applyThrottle(settings.CpuThrottle());

    // See LoadBestModel for the model resolution order. Missing/invalid
    // models fall back to the synthetic mask.
    auto loadModel = [&]() -> bool {
        std::lock_guard<std::mutex> lock(serviceMutex);
        activeModelPath = LoadBestModel(service, settings);
        return !activeModelPath.empty();
    };
    loadModel();

    rmbg::DirectoryWatcher watcher;
    WorkQueue work;

    // Counters for the status display (main thread reads, worker writes).
    std::atomic<int> processedCount{0};
    std::atomic<int> failedCount{0};

    work.Start([&](const WorkItem& item) {
        const std::wstring& path = item.path;
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
            out = service.ProcessImage(path,
                                       item.outputDir.empty() ? settings.OutputFolder()
                                                              : item.outputDir,
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
                work.Enqueue({fullPath, L""});
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
        if (settings.CpuThrottle() != L"normal") {
            lines.push_back(std::wstring(L"CPU: ") +
                            ThrottleLabel(settings.CpuThrottle()));
        }
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
    tray.SetThrottleProvider([&] { return settings.CpuThrottle(); });
    tray.SetOnSelectThrottle([&](const std::wstring& mode) {
        settings.SetCpuThrottle(mode);
        applyThrottle(settings.CpuThrottle());
        // ORT thread pools are fixed at session creation — reload so the
        // new cap takes effect immediately.
        loadModel();
        tray.ShowBalloon(L"CPU usage set", ThrottleLabel(settings.CpuThrottle()));
    });
    tray.SetAutostartProvider([] { return IsAutostartEnabled(); });
    tray.SetOnToggleAutostart([&] {
        const bool enable = !IsAutostartEnabled();
        if (SetAutostartEnabled(enable)) {
            tray.ShowBalloon(L"Start on Login",
                             enable ? L"SnapBGR will start when you sign in."
                                    : L"SnapBGR will no longer start automatically.");
        } else {
            tray.ShowBalloon(L"Start on Login", L"Could not update the startup entry.",
                             /*isError=*/true);
        }
    });
    tray.SetOnExit([] { ::PostQuitMessage(0); });
    // On-demand requests forwarded from the Explorer context menu (via a
    // short-lived second instance). Output goes next to the original file.
    tray.SetOnCopyData([&](const std::wstring& file) {
        if (!IsImageFile(file)) return;
        work.Enqueue({file, std::filesystem::path(file).parent_path().wstring()});
    });

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
