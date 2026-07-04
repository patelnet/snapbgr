// MainWindow.xaml.cpp — see MainWindow.xaml.h.
#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "AppState.h"
#include "HomePage.xaml.h"
#include "SettingsPage.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::SnapBGR::implementation {

MainWindow::MainWindow() {
    InitializeComponent();
    Title(L"SnapBGR");

    // Attach this window's dispatcher so background threads can marshal
    // status/log updates onto the UI thread via TryEnqueue.
    rmbg::AppState::Instance().SetDispatcherQueue(DispatcherQueue());

    // Default to the Home page.
    NavView().SelectedItem(HomeItem());
    ContentFrame().Navigate(xaml_typename<SnapBGR::HomePage>());
}

void MainWindow::NavView_SelectionChanged(
    NavigationView const&, NavigationViewSelectionChangedEventArgs const& args) {
    auto item = args.SelectedItem().try_as<NavigationViewItem>();
    if (!item) return;

    const hstring tag = unbox_value_or<hstring>(item.Tag(), L"");
    if (tag == L"home") {
        ContentFrame().Navigate(xaml_typename<SnapBGR::HomePage>());
    } else if (tag == L"settings") {
        ContentFrame().Navigate(xaml_typename<SnapBGR::SettingsPage>());
    }
}

} // namespace winrt::SnapBGR::implementation
