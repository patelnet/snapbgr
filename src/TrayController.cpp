// TrayController.cpp — see TrayController.h.
#include "pch.h"
#include "TrayController.h"

namespace rmbg {

namespace {
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT ID_MENU_START = 1001;
constexpr UINT ID_MENU_STOP = 1002;
constexpr UINT ID_MENU_OPEN_OUTPUT = 1003;
constexpr UINT ID_MENU_EXIT = 1004;
constexpr UINT ID_MENU_SELECT_WATCHED = 1005;
constexpr UINT ID_MENU_SELECT_OUTPUT = 1006;
constexpr UINT ID_MENU_SELECT_MODEL = 1007;
constexpr UINT ID_MENU_GET_MODELS = 1008;
constexpr UINT ID_MENU_FORMAT_PNG = 1009;
constexpr UINT ID_MENU_FORMAT_JPG = 1010;
constexpr wchar_t kWndClass[] = L"SnapBGRTrayWnd";
} // namespace

TrayController::~TrayController() {
    Shutdown();
}

bool TrayController::Initialize(const std::wstring& tooltip, const std::wstring& iconPath) {
    if (m_hwnd) return true; // already initialized

    // Register a window class for the hidden message window. The instance
    // pointer travels via CREATESTRUCT -> GWLP_USERDATA so WndProc can
    // route messages back to this object.
    WNDCLASSW wc{};
    wc.lpfnWndProc = &TrayController::WndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    ::RegisterClassW(&wc); // ok if already registered

    m_hwnd = ::CreateWindowExW(0, kWndClass, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, wc.hInstance, this);
    if (!m_hwnd) return false;

    // Load the icon from disk if provided, then the EXE's embedded icon
    // resource, then the stock application icon.
    if (!iconPath.empty()) {
        m_icon = static_cast<HICON>(::LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON,
                                                 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }
    if (!m_icon) {
        m_icon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    }
    if (!m_icon) {
        m_icon = ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    m_nid = {};
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = m_icon;
    ::wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);

    m_iconAdded = ::Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    return m_iconAdded;
}

void TrayController::Shutdown() {
    if (m_iconAdded) {
        ::Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_iconAdded = false;
    }
    if (m_hwnd) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_icon) {
        ::DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void TrayController::SetTooltip(const std::wstring& text) {
    if (!m_iconAdded) return;
    ::wcsncpy_s(m_nid.szTip, text.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP;
    ::Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayController::ShowBalloon(const std::wstring& title, const std::wstring& message,
                                 bool isError) {
    if (!m_iconAdded) return;
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = isError ? NIIF_ERROR : NIIF_INFO;
    ::wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    ::wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayController::ShowContextMenu() {
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    // Status header: disabled lines describing current state / queue / model.
    if (m_statusProvider) {
        for (const auto& line : m_statusProvider()) {
            ::AppendMenuW(menu, MF_STRING | MF_GRAYED | MF_DISABLED, 0, line.c_str());
        }
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }

    ::AppendMenuW(menu, MF_STRING, ID_MENU_START, L"Start Watching");
    ::AppendMenuW(menu, MF_STRING, ID_MENU_STOP, L"Stop Watching");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, ID_MENU_SELECT_WATCHED, L"Select Watched Folder…");
    ::AppendMenuW(menu, MF_STRING, ID_MENU_SELECT_OUTPUT, L"Select Output Folder…");
    ::AppendMenuW(menu, MF_STRING, ID_MENU_OPEN_OUTPUT, L"Open Output Folder");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, ID_MENU_SELECT_MODEL, L"Select Model…");
    ::AppendMenuW(menu, MF_STRING, ID_MENU_GET_MODELS, L"Get Compatible Models");
    // Output format submenu with a radio check on the active choice.
    HMENU formatMenu = ::CreatePopupMenu();
    if (formatMenu) {
        const std::wstring fmt = m_formatProvider ? m_formatProvider() : L"png";
        ::AppendMenuW(formatMenu, MF_STRING | (fmt == L"png" ? MF_CHECKED : 0u),
                      ID_MENU_FORMAT_PNG, L"PNG (transparent)");
        ::AppendMenuW(formatMenu, MF_STRING | (fmt == L"jpg" ? MF_CHECKED : 0u),
                      ID_MENU_FORMAT_JPG, L"JPG (white background)");
        ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(formatMenu),
                      L"Output Format");
    }
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, ID_MENU_EXIT, L"Exit");

    POINT pt;
    ::GetCursorPos(&pt);
    // Required foreground quirk: without this the menu won't dismiss when
    // the user clicks elsewhere (documented TrackPopupMenu behavior).
    ::SetForegroundWindow(m_hwnd);
    const UINT cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                      pt.x, pt.y, 0, m_hwnd, nullptr);
    ::DestroyMenu(menu);

    switch (cmd) {
    case ID_MENU_START:          if (m_onStart) m_onStart(); break;
    case ID_MENU_STOP:           if (m_onStop) m_onStop(); break;
    case ID_MENU_OPEN_OUTPUT:    if (m_onOpenOutput) m_onOpenOutput(); break;
    case ID_MENU_SELECT_WATCHED: if (m_onSelectWatched) m_onSelectWatched(); break;
    case ID_MENU_SELECT_OUTPUT:  if (m_onSelectOutput) m_onSelectOutput(); break;
    case ID_MENU_SELECT_MODEL:   if (m_onSelectModel) m_onSelectModel(); break;
    case ID_MENU_GET_MODELS:     if (m_onGetModels) m_onGetModels(); break;
    case ID_MENU_FORMAT_PNG:     if (m_onSelectFormat) m_onSelectFormat(L"png"); break;
    case ID_MENU_FORMAT_JPG:     if (m_onSelectFormat) m_onSelectFormat(L"jpg"); break;
    case ID_MENU_EXIT:           if (m_onExit) m_onExit(); break;
    default: break;
    }
}

LRESULT CALLBACK TrayController::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* self = reinterpret_cast<TrayController*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && msg == WM_TRAYICON) {
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            self->ShowContextMenu();
            return 0;
        default:
            break;
        }
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace rmbg
