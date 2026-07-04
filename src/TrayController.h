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
    std::function<std::vector<std::wstring>()> m_statusProvider;
};

} // namespace rmbg
