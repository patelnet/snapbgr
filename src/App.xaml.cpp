// App.xaml.cpp — application startup.
//
// Startup order (per master prompt):
//   1. Construct AppState (creates Settings/Watcher/Service/Tray members).
//   2. AppState::Initialize(): load settings.json, try to load
//      models/modnet.onnx (placeholder -> synthetic fallback), init tray.
//   3. Create + activate MainWindow, which attaches the DispatcherQueue
//      and status/log callbacks.
//
// AppState is a documented singleton: the single instance is owned here and
// exposed through rmbg::AppState::Instance() for XAML code-behind.
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::SnapBGR::implementation {

App::App() {
    InitializeComponent();

#if defined(_DEBUG)
    UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
        if (IsDebuggerPresent()) {
            auto message = e.Message();
            __debugbreak();
        }
    });
#endif
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    // Core services first so the window can bind to them immediately.
    m_appState = std::make_unique<rmbg::AppState>();
    m_appState->Initialize();

    m_window = make<MainWindow>();

    // Tray "Exit" must close the XAML window (which ends the app loop).
    m_appState->Tray().SetOnExit([window = m_window] {
        // Tray callbacks arrive on the UI thread (message-only window on
        // the UI thread), so closing directly is safe.
        window.Close();
    });

    m_window.Activate();

    // Optionally auto-start watching if the user enabled it in settings.
    if (m_appState->GetSettings().AutoStart()) {
        m_appState->StartWatching();
    }
}

} // namespace winrt::SnapBGR::implementation
