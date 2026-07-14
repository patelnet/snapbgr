// TrayController.h — Win32 system tray icon with a right-click menu.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

namespace rmbg {

// Owns a NOTIFYICONDATA tray icon plus a hidden message-only window that
// receives its callbacks. Menu commands are forwarded to std::function
// callbacks set by AppState/MainWindow.
//
// All methods must be called from the thread that created the controller
// (the UI thread); Shell_NotifyIcon is thread-affine.
class TrayController {
public:
    // Window class name of the hidden message window. Secondary instances
    // use it with FindWindow to locate the running app and forward
    // on-demand processing requests via WM_COPYDATA.
    static constexpr const wchar_t* kWindowClassName = L"SnapBGRTrayWnd";

    TrayController() = default;
    ~TrayController();

    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;

    // Creates the hidden window and adds the tray icon. `iconPath` may be
    // empty — the default application icon is used as a fallback.
    bool Initialize(const std::wstring& tooltip, const std::wstring& iconPath = {});

    // Removes the icon and destroys the hidden window.
    void Shutdown();

    // Updates the hover tooltip (truncated to the shell's 127-char limit).
    void SetTooltip(const std::wstring& text);

    // Shows a balloon notification (used for processed-file and error toasts).
    void ShowBalloon(const std::wstring& title, const std::wstring& message,
                     bool isError = false);

    // Menu command callbacks.
    void SetOnStart(std::function<void()> cb) { m_onStart = std::move(cb); }
    void SetOnStop(std::function<void()> cb) { m_onStop = std::move(cb); }
    void SetOnOpenOutput(std::function<void()> cb) { m_onOpenOutput = std::move(cb); }
    void SetOnExit(std::function<void()> cb) { m_onExit = std::move(cb); }
    void SetOnSelectWatched(std::function<void()> cb) { m_onSelectWatched = std::move(cb); }
    void SetOnSelectOutput(std::function<void()> cb) { m_onSelectOutput = std::move(cb); }
    void SetOnSelectModel(std::function<void()> cb) { m_onSelectModel = std::move(cb); }
    void SetOnGetModels(std::function<void()> cb) { m_onGetModels = std::move(cb); }
    // Called with L"png" or L"jpg" when the user picks an output format.
    void SetOnSelectFormat(std::function<void(const std::wstring&)> cb) {
        m_onSelectFormat = std::move(cb);
    }
    // Returns the current format (L"png"/L"jpg") so the menu can show a
    // radio check next to the active choice.
    void SetFormatProvider(std::function<std::wstring()> cb) {
        m_formatProvider = std::move(cb);
    }
    // Called with L"normal", L"low" or L"efficiency" when the user picks a
    // CPU usage mode.
    void SetOnSelectThrottle(std::function<void(const std::wstring&)> cb) {
        m_onSelectThrottle = std::move(cb);
    }
    // Returns the current throttle mode so the menu can check the active choice.
    void SetThrottleProvider(std::function<std::wstring()> cb) {
        m_throttleProvider = std::move(cb);
    }
    // Called when the user toggles "Start on Login".
    void SetOnToggleAutostart(std::function<void()> cb) {
        m_onToggleAutostart = std::move(cb);
    }
    // Returns true when the app is registered to start at sign-in, so the
    // menu can show a checkmark.
    void SetAutostartProvider(std::function<bool()> cb) {
        m_autostartProvider = std::move(cb);
    }
    // Called with a file path when another instance forwards an on-demand
    // processing request (Explorer context menu) via WM_COPYDATA.
    void SetOnCopyData(std::function<void(const std::wstring&)> cb) {
        m_onCopyData = std::move(cb);
    }

    // Called right before the menu opens; returns status lines rendered as
    // disabled items at the top of the menu (state, queue depth, model, ...).
    void SetStatusProvider(std::function<std::vector<std::wstring>()> cb) {
        m_statusProvider = std::move(cb);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void ShowContextMenu();

    HWND m_hwnd = nullptr;
    HICON m_icon = nullptr;
    NOTIFYICONDATAW m_nid{};
    bool m_iconAdded = false;

    std::function<void()> m_onStart;
    std::function<void()> m_onStop;
    std::function<void()> m_onOpenOutput;
    std::function<void()> m_onExit;
    std::function<void()> m_onSelectWatched;
    std::function<void()> m_onSelectOutput;
    std::function<void()> m_onSelectModel;
    std::function<void()> m_onGetModels;
    std::function<void(const std::wstring&)> m_onSelectFormat;
    std::function<std::wstring()> m_formatProvider;
    std::function<void(const std::wstring&)> m_onSelectThrottle;
    std::function<std::wstring()> m_throttleProvider;
    std::function<void()> m_onToggleAutostart;
    std::function<bool()> m_autostartProvider;
    std::function<void(const std::wstring&)> m_onCopyData;
    std::function<std::vector<std::wstring>()> m_statusProvider;
};

} // namespace rmbg
