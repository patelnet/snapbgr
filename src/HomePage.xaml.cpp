// HomePage.xaml.cpp — see HomePage.xaml.h.
#include "pch.h"
#include "HomePage.xaml.h"
#if __has_include("HomePage.g.cpp")
#include "HomePage.g.cpp"
#endif

#include "AppState.h"

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Windows::ApplicationModel::DataTransfer;

namespace winrt::SnapBGR::implementation {

HomePage::HomePage() {
    InitializeComponent();

    auto& state = rmbg::AppState::Instance();

    // Populate folder boxes from persisted settings.
    WatchFolderBox().Text(hstring{state.WatchedFolder()});
    OutputFolderBox().Text(hstring{state.OutputFolder()});

    // AppState marshals these onto the UI thread via DispatcherQueue, so we
    // can touch controls directly. Capture get_weak() so callbacks are safe
    // after the page is torn down (navigation away).
    state.SetStatusChangedCallback([weak = get_weak()](const std::wstring& status) {
        if (auto self = weak.get()) {
            self->StatusText().Text(hstring{status});
        }
    });
    state.SetLogCallback([weak = get_weak()](const std::wstring& message) {
        if (auto self = weak.get()) {
            self->AppendLog(hstring{message});
        }
    });
}

void HomePage::AppendLog(hstring const& message) {
    LogList().Items().InsertAt(0, box_value(message)); // newest on top
    // Cap the log so the ListView never grows unbounded.
    while (LogList().Items().Size() > 500) {
        LogList().Items().RemoveAtEnd();
    }
}

void HomePage::StartButton_Click(Windows::Foundation::IInspectable const&,
                                 RoutedEventArgs const&) {
    auto& state = rmbg::AppState::Instance();
    // Persist any edits made directly on the home page before starting.
    state.SetWatchedFolder(std::wstring{WatchFolderBox().Text()});
    state.SetOutputFolder(std::wstring{OutputFolderBox().Text()});
    state.StartWatching();
}

void HomePage::StopButton_Click(Windows::Foundation::IInspectable const&,
                                RoutedEventArgs const&) {
    rmbg::AppState::Instance().StopWatching();
}

void HomePage::Page_DragOver(Windows::Foundation::IInspectable const&,
                             DragEventArgs const& e) {
    // Accept only file drops.
    if (e.DataView().Contains(StandardDataFormats::StorageItems())) {
        e.AcceptedOperation(DataPackageOperation::Copy);
    }
}

Windows::Foundation::IAsyncAction HomePage::Page_Drop(
    Windows::Foundation::IInspectable const&, DragEventArgs const& e) {
    if (!e.DataView().Contains(StandardDataFormats::StorageItems())) {
        co_return;
    }

    auto items = co_await e.DataView().GetStorageItemsAsync();
    for (auto const& item : items) {
        if (auto file = item.try_as<Windows::Storage::StorageFile>()) {
            // Processing runs on a background thread inside AppState.
            rmbg::AppState::Instance().ProcessFileAsync(std::wstring{file.Path()});
        }
    }
}

} // namespace winrt::SnapBGR::implementation
