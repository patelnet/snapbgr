// DirectoryWatcher.cpp — see DirectoryWatcher.h.
#include "pch.h"
#include "DirectoryWatcher.h"

#include <filesystem>

namespace rmbg {

DirectoryWatcher::~DirectoryWatcher() {
    StopWatching();
}

bool DirectoryWatcher::StartWatching(const std::wstring& folder, FileAddedCallback onFileAdded) {
    StopWatching();

    // FILE_FLAG_OVERLAPPED lets us cancel a pending ReadDirectoryChangesW
    // via an event, so StopWatching() never blocks indefinitely.
    HANDLE dir = ::CreateFileW(
        folder.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
        return false;
    }

    m_stopEvent = ::CreateEventW(nullptr, TRUE /*manual reset*/, FALSE, nullptr);
    if (!m_stopEvent) {
        ::CloseHandle(dir);
        return false;
    }

    m_directoryHandle = dir;
    m_folder = folder;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callback = std::move(onFileAdded);
    }
    m_running = true;
    m_thread = std::thread(&DirectoryWatcher::WatchLoop, this);
    return true;
}

void DirectoryWatcher::StopWatching() {
    if (!m_running.exchange(false)) {
        // Not running, but a previous Start may have partially initialized.
        if (m_thread.joinable()) m_thread.join();
    } else {
        if (m_stopEvent) ::SetEvent(m_stopEvent);
        if (m_thread.joinable()) m_thread.join();
    }

    if (m_directoryHandle != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_directoryHandle);
        m_directoryHandle = INVALID_HANDLE_VALUE;
    }
    if (m_stopEvent) {
        ::CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callback = nullptr;
    }
}

void DirectoryWatcher::WatchLoop() {
    // FILE_NOTIFY_INFORMATION records are variable-length; 64 KiB handles
    // bursts of many files at once.
    alignas(DWORD) std::vector<std::byte> buffer(64 * 1024);

    OVERLAPPED overlapped{};
    HANDLE readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readEvent) return;
    overlapped.hEvent = readEvent;

    while (m_running.load()) {
        DWORD bytesReturned = 0;
        ::ResetEvent(readEvent);

        BOOL ok = ::ReadDirectoryChangesW(
            m_directoryHandle,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            FALSE, // do not watch subtree
            FILE_NOTIFY_CHANGE_FILE_NAME,
            nullptr,
            &overlapped,
            nullptr);
        if (!ok) break; // handle closed or invalid — exit thread

        // Wait for either data or a stop request.
        HANDLE handles[2] = {readEvent, m_stopEvent};
        const DWORD wait = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            // Stop requested (or wait failed) — cancel the pending read.
            ::CancelIoEx(m_directoryHandle, &overlapped);
            ::GetOverlappedResult(m_directoryHandle, &overlapped, &bytesReturned, TRUE);
            break;
        }
        if (!::GetOverlappedResult(m_directoryHandle, &overlapped, &bytesReturned, FALSE)) {
            break;
        }
        if (bytesReturned == 0) continue; // buffer overflow — changes lost; keep going

        // Walk the packed FILE_NOTIFY_INFORMATION list.
        size_t offset = 0;
        for (;;) {
            auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                // FileName is NOT null-terminated; length is in bytes.
                std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                std::wstring fullPath = (std::filesystem::path(m_folder) / name).wstring();

                FileAddedCallback cb;
                {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    cb = m_callback;
                }
                if (cb) {
                    try {
                        cb(fullPath);
                    } catch (...) {
                        // Callbacks must never take down the watcher thread.
                    }
                }
            }
            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
        }
    }

    ::CloseHandle(readEvent);
}

} // namespace rmbg
