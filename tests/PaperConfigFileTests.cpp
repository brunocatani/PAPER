#include "PaperConfigFile.h"
#include "PaperConfigDefaults.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
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

    [[nodiscard]] auto iniValues(const std::string& contents)
    {
        std::map<std::string, std::string> values;
        std::istringstream stream(contents);
        std::string section;
        std::string line;
        const auto trim = [](const std::string& text) {
            const auto first = text.find_first_not_of(" \t\r");
            return first == std::string::npos ? std::string{} :
                text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
        };
        while (std::getline(stream, line)) {
            line = trim(line);
            if (line.empty() || line.front() == ';') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            const auto equals = line.find('=');
            require(equals != std::string::npos && !section.empty(), "Invalid INI entry");
            require(values.emplace(section + "/" + trim(line.substr(0, equals)),
                        trim(line.substr(equals + 1))).second,
                "Duplicate INI key");
        }
        return values;
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

int main(int argc, char** argv)
{
    try {
        TestDirectory testDirectory;
        const auto iniPath = testDirectory.root / "config" / "PAPER.ini";
        const auto compiledDefaults = paper::config_defaults::makeDefaultIni();

        const auto created = paper::config_file::ensureFileExists(
            iniPath,
            compiledDefaults);
        require(
            created.status == paper::config_file::EnsureStatus::Created,
            "A missing PAPER INI must be created");
        require(!created.error, "Successful creation must not return an error");
        require(
            readAll(iniPath) == compiledDefaults,
            "The created PAPER INI must exactly match the compiled defaults");
        require(argc == 2, "Expected the documentation example path");
        require(iniValues(readAll(iniPath)) == iniValues(readAll(argv[1])),
            "Example key coverage and values must agree with compiled first-run defaults");

        constexpr std::string_view customContents =
            "[Main]\r\nbEnabled = false\r\n";
        writeAll(iniPath, customContents);
        const auto existing = paper::config_file::ensureFileExists(
            iniPath,
            compiledDefaults);
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
            compiledDefaults);
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
