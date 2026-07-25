#include "PaperConfigFile.h"
#include "PaperDefaultIni.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    [[nodiscard]] std::string readAll(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(stream.is_open(), "Could not open test file for reading");
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    void writeAll(
        const std::filesystem::path& path,
        const std::string_view contents)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        require(stream.is_open(), "Could not open test file for writing");
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        require(stream.good(), "Could not write test file");
    }

    class TestDirectory
    {
    public:
        TestDirectory()
        {
            const auto nonce = std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count();
            root = std::filesystem::temp_directory_path() /
                ("PAPERConfigFileTests-" + std::to_string(nonce));
            std::error_code error;
            const bool created = std::filesystem::create_directory(root, error);
            if (!created || error) {
                throw std::runtime_error(
                    "Could not create isolated config-file test directory");
            }
        }

        ~TestDirectory()
        {
            std::error_code error;
            (void)std::filesystem::remove(root / "config" / "PAPER.ini", error);
            error.clear();
            (void)std::filesystem::remove(root / "config", error);
            error.clear();
            (void)std::filesystem::remove(root / "blocked-parent", error);
            error.clear();
            (void)std::filesystem::remove(root, error);
        }

        std::filesystem::path root;
    };
}

int main()
{
    try {
        TestDirectory testDirectory;
        const auto iniPath = testDirectory.root / "config" / "PAPER.ini";

        const auto created = paper::config_file::ensureFileExists(
            iniPath,
            paper::config_defaults::kIni);
        require(
            created.status == paper::config_file::EnsureStatus::Created,
            "A missing PAPER INI must be created");
        require(!created.error, "Successful creation must not return an error");
        require(
            readAll(iniPath) == paper::config_defaults::kIni,
            "The created PAPER INI must exactly match the configured default");

        constexpr std::string_view customContents =
            "[Main]\r\nbEnabled = false\r\n";
        writeAll(iniPath, customContents);
        const auto existing = paper::config_file::ensureFileExists(
            iniPath,
            paper::config_defaults::kIni);
        require(
            existing.status == paper::config_file::EnsureStatus::Existing,
            "An existing PAPER INI must be preserved");
        require(!existing.error, "An existing file must not return an error");
        require(
            readAll(iniPath) == customContents,
            "First-run creation must never overwrite existing user tuning");

        const auto blockedParent = testDirectory.root / "blocked-parent";
        writeAll(blockedParent, "not a directory");
        const auto failed = paper::config_file::ensureFileExists(
            blockedParent / "PAPER.ini",
            paper::config_defaults::kIni);
        require(
            failed.status == paper::config_file::EnsureStatus::Failed,
            "A path-creation failure must be reported");
        require(
            static_cast<bool>(failed.error),
            "A path-creation failure must carry its system error");
        require(
            readAll(blockedParent) == "not a directory",
            "Failure handling must not damage the blocking user file");
    } catch (const std::exception& error) {
        std::cerr << "PAPERConfigFileTests failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
