// HomePage.xaml.h — home page: folders, start/stop, status, log, drag-drop.
#pragma once

#include "HomePage.xaml.g.h"

namespace winrt::SnapBGR::implementation {

struct HomePage : HomePageT<HomePage> {
    HomePage();

    void StartButton_Click(Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&);
    void StopButton_Click(Windows::Foundation::IInspectable const&,
                          Microsoft::UI::Xaml::RoutedEventArgs const&);

    // Drag-and-drop: dropped image files are processed immediately.
    void Page_DragOver(Windows::Foundation::IInspectable const&,
                       Microsoft::UI::Xaml::DragEventArgs const& e);
    Windows::Foundation::IAsyncAction Page_Drop(
        Windows::Foundation::IInspectable const&,
        Microsoft::UI::Xaml::DragEventArgs const& e);

private:
    void AppendLog(winrt::hstring const& message);
};

} // namespace winrt::SnapBGR::implementation

namespace winrt::SnapBGR::factory_implementation {
struct HomePage : HomePageT<HomePage, implementation::HomePage> {};
} // namespace winrt::SnapBGR::factory_implementation
