#pragma once

#include "api/PAPERApi.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace paper::weapon_motion_cache
{
    inline constexpr std::uint32_t kFormatVersion = 1;
    inline constexpr std::uint32_t kCompilerSchemaVersion = 2;
    inline constexpr std::uint32_t kMaximumAnimationPaths =
        api::PAPER_MAX_RELOAD_ANIMATION_CLIPS_V1;

    struct StablePartKey
    {
        std::string sourceName{};
        std::string nodePath{};
        std::string omodPluginName{};
        std::uint32_t omodLocalFormId{ 0 };

        [[nodiscard]] bool operator==(const StablePartKey&) const = default;
    };

    struct CachedFollower
    {
        StablePartKey part{};
        std::uint32_t flags{ 0 };
        std::array<
            api::PaperReloadQsTransformV1,
            api::PAPER_WEAPON_MOTION_KEY_COUNT_V1> keys{};
    };

    struct CachedStage
    {
        StablePartKey part{};
        api::PaperWeaponMotionStageKindV1 kind{
            api::PaperWeaponMotionStageKindV1::Primary
        };
        std::string sourceAnimationPath{};
        std::uint32_t sourceBoundary{ 0 };
        float totalArcLengthGameUnits{ 0.0f };
        float peakDeltaGameUnits{ 0.0f };
        std::array<
            api::PaperReloadQsTransformV1,
            api::PAPER_WEAPON_MOTION_KEY_COUNT_V1> keys{};
        std::vector<CachedFollower> followers{};
    };

    struct CompiledRecord
    {
        std::uint64_t loadoutKey{ 0 };
        std::uint64_t environmentFingerprint{ 0 };
        std::vector<std::string> animationPaths{};
        std::vector<CachedStage> stages{};
    };

    struct LoadoutIdentity
    {
        std::string weaponPluginName{};
        std::uint32_t weaponLocalFormId{ 0 };
        std::uint64_t weaponKeywordFlags{ 0 };
        std::uint64_t weaponFamilyFlags{ 0 };
        std::uint32_t weaponPrimaryFamily{ 0 };
        std::vector<StablePartKey> parts{};
    };

    [[nodiscard]] std::uint64_t buildLoadoutKey(LoadoutIdentity identity);
    [[nodiscard]] std::vector<std::byte> serialize(const CompiledRecord& record);
    [[nodiscard]] bool deserialize(
        const std::vector<std::byte>& bytes,
        CompiledRecord& outRecord);
    [[nodiscard]] std::size_t estimatedBytes(const CompiledRecord& record);

    struct Settings
    {
        bool enabled{ true };
        std::filesystem::path root{};
        std::filesystem::path dataRoot{};
        std::uint64_t maximumSessionBytes{ 64ull * 1024ull * 1024ull };
        std::uint64_t maximumDiskBytes{ 256ull * 1024ull * 1024ull };
        std::uint64_t maximumFileBytes{ 16ull * 1024ull * 1024ull };
        std::uint32_t maximumEntries{ 256 };

        [[nodiscard]] bool operator==(const Settings&) const = default;
    };

    enum class LoadStatus : std::uint32_t
    {
        HitSession = 0,
        HitPersistent = 1,
        Miss = 2,
        Invalid = 3,
        Unavailable = 4,
    };

    struct LoadResult
    {
        std::uint64_t loadoutKey{ 0 };
        LoadStatus status{ LoadStatus::Unavailable };
        std::unique_ptr<CompiledRecord> record{};
    };

    struct Statistics
    {
        std::uint32_t persistentRecordCount{ 0 };
        std::uint32_t pendingWriteCount{ 0 };
        std::uint64_t droppedRequestCount{ 0 };
        bool persistentStorageAvailable{ false };
    };

    class Store
    {
    public:
        Store() = default;
        ~Store();

        Store(const Store&) = delete;
        Store& operator=(const Store&) = delete;

        void configure(Settings settings);
        void start();
        void shutdown();
        [[nodiscard]] bool enabled() const;
        [[nodiscard]] bool requestLoad(std::uint64_t loadoutKey);
        [[nodiscard]] bool requestSave(std::unique_ptr<CompiledRecord> record);
        [[nodiscard]] bool tryTakeLoadResult(LoadResult& outResult);
        [[nodiscard]] Statistics statistics() const;

    private:
        enum class CommandKind : std::uint32_t
        {
            Load,
            Save,
        };

        struct Command
        {
            CommandKind kind{ CommandKind::Load };
            std::uint64_t loadoutKey{ 0 };
            std::unique_ptr<CompiledRecord> record{};
        };

        struct SessionEntry
        {
            std::unique_ptr<CompiledRecord> record{};
            std::uint64_t lastUse{ 0 };
            std::size_t bytes{ 0 };
        };

        void run(std::stop_token stopToken);
        void processLoad(std::uint64_t loadoutKey);
        void processSave(std::unique_ptr<CompiledRecord> record);
        void publishResult(LoadResult result);
        void addSessionRecord(std::unique_ptr<CompiledRecord> record);
        [[nodiscard]] std::unique_ptr<CompiledRecord> copySessionRecord(
            std::uint64_t loadoutKey);
        void pruneSession();
        void refreshPersistentStatisticsAndPrune();

        mutable std::mutex mutex_{};
        std::condition_variable_any wake_{};
        std::deque<Command> commands_{};
        std::deque<LoadResult> results_{};
        std::vector<SessionEntry> session_{};
        Settings settings_{};
        std::jthread worker_{};
        std::uint64_t sessionUseSequence_{ 0 };
        std::size_t sessionBytes_{ 0 };
        std::atomic<std::uint32_t> persistentRecordCount_{ 0 };
        std::atomic<std::uint32_t> pendingWriteCount_{ 0 };
        std::atomic<std::uint64_t> droppedRequestCount_{ 0 };
        std::atomic_bool persistentStorageAvailable_{ false };
        std::atomic_bool enabled_{ false };
    };
}
