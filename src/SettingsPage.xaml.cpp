// SettingsPage.xaml.cpp — see SettingsPage.xaml.h.
#include "pch.h"
#include "SettingsPage.xaml.h"
#if __has_include("SettingsPage.g.cpp")
#include "SettingsPage.g.cpp"
#endif

#include "AppState.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::SnapBGR::implementation {

SettingsPage::SettingsPage() {
    InitializeComponent();

    auto& state = rmbg::AppState::Instance();
    WatchFolderBox().Text(hstring{state.WatchedFolder()});
    OutputFolderBox().Text(hstring{state.OutputFolder()});
}

void SettingsPage::SaveButton_Click(Windows::Foundation::IInspectable const&,
                                    RoutedEventArgs const&) {
    auto& state = rmbg::AppState::Instance();
    // Setters persist to %LOCALAPPDATA%\SnapBGR\settings.json
    // immediately (save-on-change policy).
    state.SetWatchedFolder(std::wstring{WatchFolderBox().Text()});
    state.SetOutputFolder(std::wstring{OutputFolderBox().Text()});
    SavedText().Text(L"Saved.");
}

} // namespace winrt::SnapBGR::implementation
