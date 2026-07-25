#include "PaperConfigFile.h"

#include <Windows.h>

#include <algorithm>
#include <limits>

namespace paper::config_file
{
    namespace
    {
        [[nodiscard]] EnsureResult failed(const DWORD error)
        {
            return EnsureResult{
                .status = EnsureStatus::Failed,
                .error = std::error_code(
                    static_cast<int>(error),
                    std::system_category()),
            };
        }

        [[nodiscard]] EnsureResult discardCreatedFile(
            const HANDLE file,
            const std::filesystem::path& path,
            const DWORD error)
        {
            (void)CloseHandle(file);
            (void)DeleteFileW(path.c_str());
            return failed(error);
        }
    }

    EnsureResult ensureFileExists(
        const std::filesystem::path& path,
        const std::string_view defaultContents)
    {
        if (path.empty()) {
            return EnsureResult{
                .status = EnsureStatus::Failed,
                .error = std::make_error_code(std::errc::invalid_argument),
            };
        }

        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code directoryError;
            (void)std::filesystem::create_directories(parent, directoryError);
            if (directoryError) {
                return EnsureResult{
                    .status = EnsureStatus::Failed,
                    .error = directoryError,
                };
            }
        }

        const HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
                return EnsureResult{
                    .status = EnsureStatus::Existing,
                };
            }
            return failed(error);
        }

        std::size_t offset = 0;
        while (offset < defaultContents.size()) {
            const auto remaining = defaultContents.size() - offset;
            const auto chunkSize = static_cast<DWORD>(std::min<std::size_t>(
                remaining,
                std::numeric_limits<DWORD>::max()));
            DWORD bytesWritten = 0;
            const BOOL writeSucceeded = WriteFile(
                file,
                defaultContents.data() + offset,
                chunkSize,
                &bytesWritten,
                nullptr);
            if (!writeSucceeded || bytesWritten == 0) {
                const DWORD error = writeSucceeded ?
                    ERROR_WRITE_FAULT :
                    GetLastError();
                return discardCreatedFile(file, path, error);
            }
            offset += bytesWritten;
        }

        if (!FlushFileBuffers(file)) {
            return discardCreatedFile(file, path, GetLastError());
        }
        if (!CloseHandle(file)) {
            const DWORD error = GetLastError();
            (void)DeleteFileW(path.c_str());
            return failed(error);
        }

        return EnsureResult{
            .status = EnsureStatus::Created,
        };
    }
}
