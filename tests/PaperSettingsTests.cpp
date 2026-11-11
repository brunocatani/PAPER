// PAPER.toml contracts: the example file is what PAPER writes, the documented
// defaults are the compiled defaults, values are clamped as documented, and
// PAPER.ini of earlier versions migrates once and is deleted.
#include "PaperSettings.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace
{
    namespace fs = std::filesystem;

    std::string read(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input.good());
        return { std::istreambuf_iterator<char>(input), {} };
    }

    void write(const fs::path& path, std::string_view text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        assert(output.good());
    }

    std::string body(std::string text)
    {
        std::erase(text, '\r');
        const auto start = text.find("\n## === ");
        assert(start != std::string::npos);
        return text.substr(start);
    }

    constexpr std::string_view kExampleHeader =
        "## PAPER_example.toml - every option with its default and help\n"
        "## Reference only: PAPER never reads this file. The first time PAPER starts it writes this same text as\n"
        "## PAPER.toml in Documents\\My Games\\Fallout4VR\\Mods_Config\\PAPER.\n";

    bool sameValues(const paper::PaperConfig& a, const paper::PaperConfig& b)
    {
        return a.logLevel == b.logLevel && a.manualReloadOnly == b.manualReloadOnly &&
               a.handBlendSeconds == b.handBlendSeconds && a.slideOpenOnEmpty == b.slideOpenOnEmpty &&
               a.slideCloseSeconds == b.slideCloseSeconds && a.chamberRound == b.chamberRound &&
               a.boltAppreciation == b.boltAppreciation && a.boltPullDistance == b.boltPullDistance &&
               a.debugDrawNativeAnimation == b.debugDrawNativeAnimation &&
               a.debugDrawNativeAnimationText == b.debugDrawNativeAnimationText &&
               a.debugNativeAnimationAxisLength == b.debugNativeAnimationAxisLength &&
               a.debugNativeAnimationMarkerSize == b.debugNativeAnimationMarkerSize;
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    const fs::path root(PAPER_SETTINGS_TEST_DIR);
    std::error_code ignored;
    fs::remove_all(root, ignored);

    // A first start writes every option at its default; that file is the example.
    const auto fresh = root / "fresh";
    fs::create_directories(fresh);
    auto store = paper::settings::makeStore(fresh);
    store->load(true);
    assert(store->messages().empty());
    const auto written = body(read(fresh / "PAPER.toml"));
    const auto examples = fs::path(argv[argc - 1]);
    if (argc == 3 && std::string_view(argv[1]) == "--write-examples") {
        write(examples / "PAPER_example.toml", std::string(kExampleHeader) + written);
        fs::remove_all(root, ignored);
        return 0;
    }
    assert(body(read(examples / "PAPER_example.toml")) == written);
    for (const auto& setting : store->settings())
        assert(setting.specified && setting.value == setting.defaultValue);
    auto loaded = paper::settings::fromStore(*store);
    assert(sameValues(loaded, paper::PaperConfig{}));
    assert(fs::path(loaded.activePath) == fresh / "PAPER.toml");

    // Values are clamped to their documented ranges.
    write(fresh / "PAPER.toml", "[Reload]\nHandBlendSeconds = 4.0\n[BoltAppreciation]\nBoltPullDistance = 1\n");
    store->load(false);
    assert(store->messages().empty());
    loaded = paper::settings::fromStore(*store);
    assert(loaded.handBlendSeconds == 1.0f && loaded.boltPullDistance == 2.0f);

    // An upgrade from PAPER.ini keeps the options the player changed, drops retired
    // options and deletes the INI.
    const auto upgrade = root / "upgrade";
    fs::create_directories(upgrade);
    write(upgrade / "PAPER.ini",
        "; PAPER created this file from compiled defaults because it was missing.\r\n[Main]\r\nbEnabled = true\r\n"
        "iLogLevel = 1\r\n[Reload]\r\nbManualReloadOnly = true\r\nfHandBlendSeconds = 0.15\r\nbChamberRound = true\r\nbBoltHoldOpen = false\r\n"
        "bBoltAppreciation = true\r\nfBoltPullDistance = 9.5\r\n[DevelopmentCapture]\r\nsMaximumMode = User\r\n"
        "[WeaponMotionCache]\r\nsAccess = ReadOnly\r\niDiskCacheMiB = 512\r\n");
    store = paper::settings::makeStore(upgrade);
    store->load(true);
    assert(!fs::exists(upgrade / "PAPER.ini"));
    loaded = paper::settings::fromStore(*store);
    assert(loaded.logLevel == 1 && loaded.manualReloadOnly && loaded.chamberRound && loaded.boltAppreciation);
    assert(loaded.boltPullDistance == 9.5f && !loaded.slideOpenOnEmpty);
    assert(read(upgrade / "PAPER.toml").find("ChamberRound = true\n") != std::string::npos);

    // An INI holding PAPER 1.2's defaults for options whose default has changed
    // since takes the new defaults.
    const auto oldDefaults = root / "old-defaults";
    fs::create_directories(oldDefaults);
    write(oldDefaults / "PAPER.ini", "[Reload]\r\nbManualReloadOnly = false\r\nbChamberRound = false\r\n");
    store = paper::settings::makeStore(oldDefaults);
    store->load(true);
    loaded = paper::settings::fromStore(*store);
    assert(loaded.manualReloadOnly && loaded.chamberRound);

    fs::remove_all(root, ignored);
    return 0;
}
