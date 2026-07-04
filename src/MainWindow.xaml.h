// MainWindow.xaml.h — main window shell (NavigationView + Frame).
#pragma once

#include "MainWindow.xaml.g.h"

namespace winrt::SnapBGR::implementation {

struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();

    void NavView_SelectionChanged(
        Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
};

} // namespace winrt::SnapBGR::implementation

namespace winrt::SnapBGR::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
} // namespace winrt::SnapBGR::factory_implementation
