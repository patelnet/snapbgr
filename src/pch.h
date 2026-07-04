// pch.h — precompiled header for SnapBGR.
// Shared Windows / STL / library includes. The WinUI 3 app project also
// pulls in C++/WinRT projection headers guarded below so the same pch can
// be used by both the core library and the packaged app.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <onnxruntime_cxx_api.h>

// C++/WinRT + WinUI 3 headers are only available when building the packaged
// app inside Visual Studio with the Windows App SDK NuGet packages restored.
#ifdef BACKGROUNDREMOVER_WINUI
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#endif
