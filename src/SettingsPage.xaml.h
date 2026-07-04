// SettingsPage.xaml.h — settings page: watch/output folders + Save.
#pragma once

#include "SettingsPage.xaml.g.h"

namespace winrt::SnapBGR::implementation {

struct SettingsPage : SettingsPageT<SettingsPage> {
    SettingsPage();

    void SaveButton_Click(Windows::Foundation::IInspectable const&,
                          Microsoft::UI::Xaml::RoutedEventArgs const&);
};

} // namespace winrt::SnapBGR::implementation

namespace winrt::SnapBGR::factory_implementation {
struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage> {};
} // namespace winrt::SnapBGR::factory_implementation
