#include "weapon_motion/WeaponMotionCache.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    using namespace paper::api;
    using namespace paper::weapon_motion_cache;

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    [[nodiscard]] PaperReloadQsTransformV1 pose(const float x)
    {
        PaperReloadQsTransformV1 value{};
        value.translate[0] = x;
        value.rotate[3] = 1.0f;
        value.scale[0] = 1.0f;
        value.scale[1] = 1.0f;
        value.scale[2] = 1.0f;
        return value;
    }

    [[nodiscard]] StablePartKey partKey(const std::string_view suffix)
    {
        return {
            .sourceName = "WeaponMagazine" + std::string(suffix),
            .nodePath = "Weapon/WeaponMagazine" + std::string(suffix),
            .omodPluginName = "TestWeapon.esp",
            .omodLocalFormId = 0x100,
        };
    }

    [[nodiscard]] CompiledRecord record(const std::uint64_t key)
    {
        CompiledRecord value{
            .loadoutKey = key,
            .animationPaths = { "Meshes/Actors/Test/Reload.hkx" },
        };
        CachedStage stage{
            .part = partKey(""),
            .sourceAnimationPath = value.animationPaths.front(),
            .sourceBoundary = 12,
            .totalArcLengthGameUnits = 4.0f,
            .peakDeltaGameUnits = 4.0f,
        };
        for (std::size_t index = 0; index < stage.keys.size(); ++index) {
            stage.keys[index] = pose(
                4.0f * static_cast<float>(index) /
                static_cast<float>(stage.keys.size() - 1));
        }
        CachedFollower follower{
            .part = partKey("Follower"),
            .flags = static_cast<std::uint32_t>(
                PaperWeaponMotionFollowerFlagV1::Rigid),
        };
        for (std::size_t index = 0; index < follower.keys.size(); ++index) {
            follower.keys[index] = pose(
                2.0f + 4.0f * static_cast<float>(index) /
                    static_cast<float>(follower.keys.size() - 1));
        }
        stage.followers.push_back(std::move(follower));
        value.stages.push_back(std::move(stage));
        return value;
    }

    void writeFile(
        const std::filesystem::path& path,
        const std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        require(stream.is_open(), "test file must open");
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        require(stream.good(), "test file must be written");
    }

    [[nodiscard]] LoadResult waitForResult(Store& store)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        LoadResult result{};
        while (std::chrono::steady_clock::now() < deadline) {
            if (store.tryTakeLoadResult(result)) {
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("cache worker result timed out");
    }

    void waitForWrites(Store& store)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (store.statistics().pendingWriteCount == 0) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("cache worker write timed out");
    }
}

int main()
{
    std::filesystem::path root{};
    try {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root = std::filesystem::temp_directory_path() /
            ("PAPERWeaponMotionCacheTests-" + std::to_string(nonce));
        const auto dataRoot = root / "Data";
        const auto cacheRoot = root / "Cache";
        writeFile(dataRoot / "TestWeapon.esp", "plugin-v1");
        writeFile(
            dataRoot / "Meshes" / "Actors" / "Test" / "Reload.hkx",
            "authored-animation-v1");

        const LoadoutIdentity identity{
            .weaponPluginName = "TestWeapon.esp",
            .weaponLocalFormId = 0x800,
            .parts = { partKey("Follower"), partKey("") },
        };
        const LoadoutIdentity reordered{
            .weaponPluginName = "testweapon.ESP",
            .weaponLocalFormId = 0x800,
            .parts = { partKey(""), partKey("Follower") },
        };
        const auto key = buildLoadoutKey(identity);
        require(key != 0, "stable loadout identity must hash");
        require(
            key == buildLoadoutKey(reordered),
            "loadout hash must ignore part ordering and ASCII case");
        auto changedGraphContext = identity;
        changedGraphContext.weaponKeywordFlags = 1;
        require(
            key != buildLoadoutKey(std::move(changedGraphContext)),
            "weapon graph/classification context must participate in identity");

        auto source = record(key);
        const auto encoded = serialize(source);
        require(!encoded.empty(), "valid compiled record must serialize");
        CompiledRecord decoded{};
        require(
            deserialize(encoded, decoded),
            "valid compiled record must deserialize");
        require(
            decoded.loadoutKey == key && decoded.stages.size() == 1 &&
                decoded.stages[0].followers.size() == 1 &&
                decoded.stages[0].keys.back().translate[0] == 4.0f,
            "round trip must preserve bounded compiled paths");
        auto corrupted = encoded;
        corrupted.back() ^= std::byte{ 0x5A };
        require(
            !deserialize(corrupted, decoded),
            "payload corruption must fail closed");

        const Settings settings{
            .root = cacheRoot,
            .dataRoot = dataRoot,
            .maximumSessionBytes = 8ull * 1024ull * 1024ull,
            .maximumDiskBytes = 32ull * 1024ull * 1024ull,
            .maximumFileBytes = 4ull * 1024ull * 1024ull,
            .maximumEntries = 16,
        };
        {
            Store store;
            store.configure(settings);
            require(
                !std::filesystem::exists(cacheRoot),
                "configuration without demand must not touch user storage");
            store.start();
            require(
                store.requestSave(std::make_unique<CompiledRecord>(source)),
                "valid compiled record must enter the bounded write queue");
            waitForWrites(store);
            require(
                store.requestLoad(key),
                "saved record must enter the bounded lookup queue");
            const auto result = waitForResult(store);
            require(
                result.status == LoadStatus::HitSession && result.record,
                "same-session lookup must use the in-process cache");
        }
        {
            Store store;
            store.configure(settings);
            store.start();
            require(store.requestLoad(key), "persistent lookup must queue");
            const auto result = waitForResult(store);
            require(
                result.status == LoadStatus::HitPersistent && result.record,
                "new store instance must hydrate the persistent cache");
        }

        writeFile(dataRoot / "TestWeapon.esp", "plugin-v2-with-new-size");
        {
            Store store;
            store.configure(settings);
            store.start();
            require(store.requestLoad(key), "invalidated lookup must queue");
            const auto result = waitForResult(store);
            require(
                result.status == LoadStatus::Miss && !result.record,
                "changed mod environment must invalidate persistent data");
        }
    } catch (const std::exception& error) {
        std::cerr << "PAPERWeaponMotionCacheTests failed: " << error.what()
                  << '\n';
        std::error_code cleanupError;
        if (!root.empty()) {
            (void)std::filesystem::remove_all(root, cleanupError);
        }
        return 1;
    }

    std::error_code cleanupError;
    (void)std::filesystem::remove_all(root, cleanupError);
    return 0;
}
