#include "weapon_motion/WeaponMotionCache.h"

#include "weapon_motion/WeaponMotionPolicy.h"

#include <Windows.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace paper::weapon_motion_cache
{
    namespace
    {
        constexpr std::array<char, 8> kMagic{
            'P', 'W', 'M', 'C', 'A', 'C', 'H', 'E'
        };
        constexpr std::size_t kHeaderBytes = 48;
        constexpr std::size_t kMaximumQueuedCommands = 4;
        constexpr std::size_t kMaximumQueuedResults = 2;
        constexpr std::uint32_t kMaximumStringBytes = 1024;
        constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        [[nodiscard]] char asciiLower(const char value)
        {
            return value >= 'A' && value <= 'Z' ?
                static_cast<char>(value + ('a' - 'A')) : value;
        }

        [[nodiscard]] std::string normalizedText(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), asciiLower);
            return value;
        }

        void hashBytes(
            std::uint64_t& hash,
            const void* data,
            const std::size_t size)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= kFnvPrime;
            }
        }

        void hashText(std::uint64_t& hash, const std::string_view value)
        {
            for (const auto character : value) {
                const auto normalized = static_cast<unsigned char>(
                    asciiLower(character == '/' ? '\\' : character));
                hashBytes(hash, std::addressof(normalized), sizeof(normalized));
            }
            constexpr unsigned char terminator = 0;
            hashBytes(hash, std::addressof(terminator), sizeof(terminator));
        }

        template <class Value>
        void hashValue(std::uint64_t& hash, const Value& value)
        {
            hashBytes(hash, std::addressof(value), sizeof(value));
        }

        [[nodiscard]] bool partLess(
            const StablePartKey& left,
            const StablePartKey& right)
        {
            return std::tie(
                       left.omodPluginName,
                       left.omodLocalFormId,
                       left.nodePath,
                       left.sourceName) <
                std::tie(
                       right.omodPluginName,
                       right.omodLocalFormId,
                       right.nodePath,
                       right.sourceName);
        }

        class Writer
        {
        public:
            template <class Value>
            void scalar(const Value value)
            {
                static_assert(std::is_trivially_copyable_v<Value>);
                const auto bytes = std::bit_cast<
                    std::array<std::byte, sizeof(Value)>>(value);
                data_.insert(data_.end(), bytes.begin(), bytes.end());
            }

            void text(const std::string_view value)
            {
                scalar(static_cast<std::uint32_t>(value.size()));
                const auto* first = reinterpret_cast<const std::byte*>(
                    value.data());
                data_.insert(data_.end(), first, first + value.size());
            }

            void transform(const api::PaperReloadQsTransformV1& value)
            {
                for (const auto component : value.translate) {
                    scalar(component);
                }
                for (const auto component : value.rotate) {
                    scalar(component);
                }
                for (const auto component : value.scale) {
                    scalar(component);
                }
            }

            void part(const StablePartKey& value)
            {
                text(value.sourceName);
                text(value.nodePath);
                text(value.omodPluginName);
                scalar(value.omodLocalFormId);
            }

            [[nodiscard]] std::vector<std::byte> take()
            {
                return std::move(data_);
            }

        private:
            std::vector<std::byte> data_{};
        };

        class Reader
        {
        public:
            explicit Reader(const std::span<const std::byte> data) : data_(data) {}

            template <class Value>
            [[nodiscard]] bool scalar(Value& outValue)
            {
                static_assert(std::is_trivially_copyable_v<Value>);
                if (offset_ > data_.size() ||
                    sizeof(Value) > data_.size() - offset_) {
                    return false;
                }
                std::array<std::byte, sizeof(Value)> bytes{};
                std::copy_n(data_.data() + offset_, sizeof(Value), bytes.data());
                outValue = std::bit_cast<Value>(bytes);
                offset_ += sizeof(Value);
                return true;
            }

            [[nodiscard]] bool text(std::string& outValue)
            {
                std::uint32_t size = 0;
                if (!scalar(size) || size > kMaximumStringBytes ||
                    offset_ > data_.size() || size > data_.size() - offset_) {
                    return false;
                }
                outValue.assign(
                    reinterpret_cast<const char*>(data_.data() + offset_), size);
                offset_ += size;
                return true;
            }

            [[nodiscard]] bool transform(
                api::PaperReloadQsTransformV1& outValue)
            {
                for (auto& component : outValue.translate) {
                    if (!scalar(component)) {
                        return false;
                    }
                }
                for (auto& component : outValue.rotate) {
                    if (!scalar(component)) {
                        return false;
                    }
                }
                for (auto& component : outValue.scale) {
                    if (!scalar(component)) {
                        return false;
                    }
                }
                return weapon_motion_policy::finite(outValue);
            }

            [[nodiscard]] bool part(StablePartKey& outValue)
            {
                return text(outValue.sourceName) && text(outValue.nodePath) &&
                    text(outValue.omodPluginName) &&
                    scalar(outValue.omodLocalFormId) &&
                    !outValue.sourceName.empty() && !outValue.nodePath.empty();
            }

            [[nodiscard]] bool consumed() const
            {
                return offset_ == data_.size();
            }

        private:
            std::span<const std::byte> data_{};
            std::size_t offset_{ 0 };
        };

        [[nodiscard]] std::uint64_t hashPayload(
            const std::span<const std::byte> payload)
        {
            auto hash = kFnvOffset;
            hashBytes(hash, payload.data(), payload.size());
            return hash;
        }

        [[nodiscard]] bool validRecord(const CompiledRecord& record)
        {
            if (record.loadoutKey == 0 || record.stages.empty() ||
                record.stages.size() > api::PAPER_MAX_WEAPON_MOTION_STAGES_V1 ||
                record.animationPaths.size() > kMaximumAnimationPaths) {
                return false;
            }
            for (const auto& path : record.animationPaths) {
                if (path.empty() || path.size() > kMaximumStringBytes) {
                    return false;
                }
            }
            const auto validPart = [](const StablePartKey& part) {
                return !part.sourceName.empty() && !part.nodePath.empty() &&
                    part.sourceName.size() <= kMaximumStringBytes &&
                    part.nodePath.size() <= kMaximumStringBytes &&
                    part.omodPluginName.size() <= kMaximumStringBytes;
            };
            const auto validTransform = [](const auto& transform) {
                if (!weapon_motion_policy::finite(transform)) {
                    return false;
                }
                auto quaternionLengthSquared = 0.0f;
                for (const auto component : transform.rotate) {
                    quaternionLengthSquared += component * component;
                }
                return quaternionLengthSquared >= 0.81f &&
                    quaternionLengthSquared <= 1.21f;
            };
            for (const auto& stage : record.stages) {
                if (!validPart(stage.part) ||
                    (stage.kind !=
                            api::PaperWeaponMotionStageKindV1::Primary &&
                        stage.kind !=
                            api::PaperWeaponMotionStageKindV1::Return) ||
                    stage.sourceAnimationPath.size() > kMaximumStringBytes ||
                    stage.followers.size() >
                        api::PAPER_MAX_WEAPON_MOTION_FOLLOWERS_V1 ||
                    !std::isfinite(stage.totalArcLengthGameUnits) ||
                    !std::isfinite(stage.peakDeltaGameUnits) ||
                    stage.totalArcLengthGameUnits <
                        weapon_motion_policy::kMinimumExcursionGameUnits) {
                    return false;
                }
                for (const auto& key : stage.keys) {
                    if (!validTransform(key)) {
                        return false;
                    }
                }
                for (const auto& follower : stage.followers) {
                    if (!validPart(follower.part)) {
                        return false;
                    }
                    for (const auto& key : follower.keys) {
                        if (!validTransform(key)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path recordPath(
            const Settings& settings,
            const std::uint64_t loadoutKey)
        {
            wchar_t name[32]{};
            (void)swprintf_s(name, L"%016llx.pwmc", loadoutKey);
            return settings.root / name;
        }

        [[nodiscard]] std::vector<std::byte> readFile(
            const std::filesystem::path& path,
            const std::uint64_t maximumBytes)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size == 0 || size > maximumBytes ||
                size > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::size_t>::max)())) {
                return {};
            }
            std::ifstream stream(path, std::ios::binary);
            if (!stream.is_open()) {
                return {};
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            return stream.good() || stream.eof() ? bytes :
                std::vector<std::byte>{};
        }

        [[nodiscard]] bool writeFileAtomically(
            const std::filesystem::path& path,
            const std::span<const std::byte> bytes)
        {
            auto temporary = path;
            temporary += L".tmp";
            {
                std::ofstream stream(
                    temporary,
                    std::ios::binary | std::ios::trunc);
                if (!stream.is_open()) {
                    return false;
                }
                stream.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                stream.flush();
                if (!stream.good()) {
                    std::error_code error;
                    (void)std::filesystem::remove(temporary, error);
                    return false;
                }
            }
            if (!MoveFileExW(
                    temporary.c_str(),
                    path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::error_code error;
                (void)std::filesystem::remove(temporary, error);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool environmentExtension(
            const std::filesystem::path& path)
        {
            const auto extension = normalizedText(path.extension().string());
            return extension == ".ba2" || extension == ".esm" ||
                extension == ".esp" || extension == ".esl";
        }

        void hashFileMetadata(
            std::uint64_t& hash,
            const std::filesystem::directory_entry& entry)
        {
            std::error_code error;
            const auto size = entry.file_size(error);
            if (error) {
                hash = 0;
                return;
            }
            const auto writeTime = entry.last_write_time(error);
            if (error) {
                hash = 0;
                return;
            }
            hashText(hash, entry.path().filename().string());
            hashValue(hash, size);
            const auto ticks = writeTime.time_since_epoch().count();
            hashValue(hash, ticks);
        }

        [[nodiscard]] bool safeRelativeAnimationPath(
            const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() ||
                path.has_root_directory()) {
                return false;
            }
            for (const auto& component : path) {
                if (component == "..") {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::uint64_t environmentFingerprint(
            const Settings& settings,
            const CompiledRecord& record)
        {
            std::error_code error;
            if (!std::filesystem::is_directory(settings.dataRoot, error) || error) {
                return 0;
            }
            std::vector<std::filesystem::directory_entry> environmentFiles{};
            for (std::filesystem::directory_iterator iterator(
                     settings.dataRoot, error), end;
                 !error && iterator != end;
                 iterator.increment(error)) {
                if (iterator->is_regular_file(error) && !error &&
                    environmentExtension(iterator->path())) {
                    environmentFiles.push_back(*iterator);
                }
                error.clear();
            }
            if (error) {
                return 0;
            }
            std::sort(
                environmentFiles.begin(),
                environmentFiles.end(),
                [](const auto& left, const auto& right) {
                    return normalizedText(left.path().filename().string()) <
                        normalizedText(right.path().filename().string());
                });
            auto hash = kFnvOffset;
            hashValue(hash, kCompilerSchemaVersion);
            for (const auto& entry : environmentFiles) {
                hashFileMetadata(hash, entry);
                if (hash == 0) {
                    return 0;
                }
            }
            for (const auto& animationPath : record.animationPaths) {
                hashText(hash, animationPath);
                const std::filesystem::path relative(animationPath);
                if (!safeRelativeAnimationPath(relative)) {
                    return 0;
                }
                const auto loosePath = settings.dataRoot / relative;
                if (!std::filesystem::is_regular_file(loosePath, error)) {
                    error.clear();
                    continue;
                }
                std::ifstream stream(loosePath, std::ios::binary);
                if (!stream.is_open()) {
                    return 0;
                }
                std::array<char, 64 * 1024> buffer{};
                while (stream) {
                    stream.read(buffer.data(), buffer.size());
                    const auto count = stream.gcount();
                    if (count > 0) {
                        hashBytes(
                            hash,
                            buffer.data(),
                            static_cast<std::size_t>(count));
                    }
                }
                if (!stream.eof()) {
                    return 0;
                }
            }
            return hash;
        }

        [[nodiscard]] std::unique_ptr<CompiledRecord> cloneRecord(
            const CompiledRecord& record)
        {
            return std::make_unique<CompiledRecord>(record);
        }
    }

    std::uint64_t buildLoadoutKey(LoadoutIdentity identity)
    {
        if (identity.weaponPluginName.empty() ||
            identity.weaponLocalFormId == 0 || identity.parts.empty()) {
            return 0;
        }
        identity.weaponPluginName = normalizedText(
            std::move(identity.weaponPluginName));
        for (auto& part : identity.parts) {
            part.sourceName = normalizedText(std::move(part.sourceName));
            part.nodePath = normalizedText(std::move(part.nodePath));
            part.omodPluginName = normalizedText(
                std::move(part.omodPluginName));
            if (part.sourceName.empty() || part.nodePath.empty()) {
                return 0;
            }
        }
        std::sort(identity.parts.begin(), identity.parts.end(), partLess);
        if (std::adjacent_find(
                identity.parts.begin(), identity.parts.end()) !=
            identity.parts.end()) {
            return 0;
        }
        auto hash = kFnvOffset;
        hashValue(hash, kCompilerSchemaVersion);
        hashText(hash, identity.weaponPluginName);
        hashValue(hash, identity.weaponLocalFormId);
        hashValue(hash, identity.weaponKeywordFlags);
        hashValue(hash, identity.weaponFamilyFlags);
        hashValue(hash, identity.weaponPrimaryFamily);
        for (const auto& part : identity.parts) {
            hashText(hash, part.sourceName);
            hashText(hash, part.nodePath);
            hashText(hash, part.omodPluginName);
            hashValue(hash, part.omodLocalFormId);
        }
        return hash == 0 ? 1 : hash;
    }

    std::vector<std::byte> serialize(const CompiledRecord& record)
    {
        if (!validRecord(record)) {
            return {};
        }
        Writer payloadWriter;
        payloadWriter.scalar(static_cast<std::uint32_t>(
            record.animationPaths.size()));
        for (const auto& path : record.animationPaths) {
            if (path.empty() || path.size() > kMaximumStringBytes) {
                return {};
            }
            payloadWriter.text(path);
        }
        payloadWriter.scalar(static_cast<std::uint32_t>(record.stages.size()));
        for (const auto& stage : record.stages) {
            payloadWriter.part(stage.part);
            payloadWriter.scalar(static_cast<std::uint32_t>(stage.kind));
            payloadWriter.text(stage.sourceAnimationPath);
            payloadWriter.scalar(stage.sourceBoundary);
            payloadWriter.scalar(stage.totalArcLengthGameUnits);
            payloadWriter.scalar(stage.peakDeltaGameUnits);
            for (const auto& key : stage.keys) {
                payloadWriter.transform(key);
            }
            payloadWriter.scalar(static_cast<std::uint32_t>(
                stage.followers.size()));
            for (const auto& follower : stage.followers) {
                payloadWriter.part(follower.part);
                payloadWriter.scalar(follower.flags);
                for (const auto& key : follower.keys) {
                    payloadWriter.transform(key);
                }
            }
        }
        auto payload = payloadWriter.take();
        Writer headerWriter;
        for (const auto character : kMagic) {
            headerWriter.scalar(character);
        }
        headerWriter.scalar(kFormatVersion);
        headerWriter.scalar(kCompilerSchemaVersion);
        headerWriter.scalar(record.loadoutKey);
        headerWriter.scalar(record.environmentFingerprint);
        headerWriter.scalar(static_cast<std::uint64_t>(payload.size()));
        headerWriter.scalar(hashPayload(payload));
        auto bytes = headerWriter.take();
        if (bytes.size() != kHeaderBytes) {
            return {};
        }
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }

    bool deserialize(
        const std::vector<std::byte>& bytes,
        CompiledRecord& outRecord)
    {
        outRecord = {};
        if (bytes.size() < kHeaderBytes) {
            return false;
        }
        Reader header(std::span<const std::byte>{ bytes }.first(kHeaderBytes));
        for (const auto expected : kMagic) {
            char actual = 0;
            if (!header.scalar(actual) || actual != expected) {
                return false;
            }
        }
        std::uint32_t formatVersion = 0;
        std::uint32_t compilerVersion = 0;
        std::uint64_t payloadSize = 0;
        std::uint64_t payloadHash = 0;
        if (!header.scalar(formatVersion) ||
            !header.scalar(compilerVersion) ||
            !header.scalar(outRecord.loadoutKey) ||
            !header.scalar(outRecord.environmentFingerprint) ||
            !header.scalar(payloadSize) || !header.scalar(payloadHash) ||
            !header.consumed() || formatVersion != kFormatVersion ||
            compilerVersion != kCompilerSchemaVersion ||
            payloadSize != bytes.size() - kHeaderBytes) {
            return false;
        }
        const auto payload = std::span<const std::byte>{ bytes }.subspan(
            kHeaderBytes);
        if (hashPayload(payload) != payloadHash) {
            return false;
        }
        Reader reader(payload);
        std::uint32_t animationPathCount = 0;
        if (!reader.scalar(animationPathCount) ||
            animationPathCount > kMaximumAnimationPaths) {
            return false;
        }
        outRecord.animationPaths.resize(animationPathCount);
        for (auto& path : outRecord.animationPaths) {
            if (!reader.text(path) || path.empty()) {
                return false;
            }
        }
        std::uint32_t stageCount = 0;
        if (!reader.scalar(stageCount) || stageCount == 0 ||
            stageCount > api::PAPER_MAX_WEAPON_MOTION_STAGES_V1) {
            return false;
        }
        outRecord.stages.resize(stageCount);
        for (auto& stage : outRecord.stages) {
            std::uint32_t kind = 0;
            if (!reader.part(stage.part) || !reader.scalar(kind) ||
                (kind != static_cast<std::uint32_t>(
                             api::PaperWeaponMotionStageKindV1::Primary) &&
                    kind != static_cast<std::uint32_t>(
                                api::PaperWeaponMotionStageKindV1::Return)) ||
                !reader.text(stage.sourceAnimationPath) ||
                !reader.scalar(stage.sourceBoundary) ||
                !reader.scalar(stage.totalArcLengthGameUnits) ||
                !reader.scalar(stage.peakDeltaGameUnits)) {
                return false;
            }
            stage.kind = static_cast<api::PaperWeaponMotionStageKindV1>(kind);
            for (auto& key : stage.keys) {
                if (!reader.transform(key)) {
                    return false;
                }
            }
            std::uint32_t followerCount = 0;
            if (!reader.scalar(followerCount) ||
                followerCount > api::PAPER_MAX_WEAPON_MOTION_FOLLOWERS_V1) {
                return false;
            }
            stage.followers.resize(followerCount);
            for (auto& follower : stage.followers) {
                if (!reader.part(follower.part) ||
                    !reader.scalar(follower.flags)) {
                    return false;
                }
                for (auto& key : follower.keys) {
                    if (!reader.transform(key)) {
                        return false;
                    }
                }
            }
        }
        if (!reader.consumed() || !validRecord(outRecord)) {
            outRecord = {};
            return false;
        }
        return true;
    }

    std::size_t estimatedBytes(const CompiledRecord& record)
    {
        auto bytes = sizeof(CompiledRecord);
        for (const auto& path : record.animationPaths) {
            bytes += path.size();
        }
        for (const auto& stage : record.stages) {
            bytes += sizeof(CachedStage) + stage.part.sourceName.size() +
                stage.part.nodePath.size() + stage.part.omodPluginName.size() +
                stage.sourceAnimationPath.size();
            for (const auto& follower : stage.followers) {
                bytes += sizeof(CachedFollower) + follower.part.sourceName.size() +
                    follower.part.nodePath.size() +
                    follower.part.omodPluginName.size();
            }
        }
        return bytes;
    }

    Store::~Store()
    {
        shutdown();
    }

    void Store::configure(Settings settings)
    {
        shutdown();
        settings_ = std::move(settings);
        if (!settings_.enabled || settings_.root.empty() ||
            settings_.dataRoot.empty() || settings_.maximumEntries == 0 ||
            settings_.maximumFileBytes < kHeaderBytes) {
            return;
        }
        enabled_.store(true, std::memory_order_release);
    }

    void Store::start()
    {
        if (!enabled() || worker_.joinable()) {
            return;
        }
        worker_ = std::jthread(
            [this](const std::stop_token stopToken) { run(stopToken); });
    }

    void Store::shutdown()
    {
        enabled_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.request_stop();
            wake_.notify_all();
            worker_.join();
        }
        std::scoped_lock lock(mutex_);
        commands_.clear();
        results_.clear();
        session_.clear();
        sessionBytes_ = 0;
        sessionUseSequence_ = 0;
        persistentRecordCount_.store(0, std::memory_order_release);
        pendingWriteCount_.store(0, std::memory_order_release);
        droppedRequestCount_.store(0, std::memory_order_release);
        persistentStorageAvailable_.store(false, std::memory_order_release);
    }

    bool Store::enabled() const
    {
        return enabled_.load(std::memory_order_acquire);
    }

    bool Store::requestLoad(const std::uint64_t loadoutKey)
    {
        if (!enabled() || !worker_.joinable() || loadoutKey == 0) {
            return false;
        }
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || commands_.size() >= kMaximumQueuedCommands) {
            droppedRequestCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        commands_.push_back(Command{
            .kind = CommandKind::Load,
            .loadoutKey = loadoutKey,
        });
        wake_.notify_one();
        return true;
    }

    bool Store::requestSave(std::unique_ptr<CompiledRecord> record)
    {
        if (!enabled() || !worker_.joinable() || !record ||
            !validRecord(*record)) {
            return false;
        }
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || commands_.size() >= kMaximumQueuedCommands) {
            droppedRequestCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        commands_.push_back(Command{
            .kind = CommandKind::Save,
            .loadoutKey = record->loadoutKey,
            .record = std::move(record),
        });
        pendingWriteCount_.fetch_add(1, std::memory_order_relaxed);
        wake_.notify_one();
        return true;
    }

    bool Store::tryTakeLoadResult(LoadResult& outResult)
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || results_.empty()) {
            return false;
        }
        outResult = std::move(results_.front());
        results_.pop_front();
        return true;
    }

    Statistics Store::statistics() const
    {
        return {
            .persistentRecordCount = persistentRecordCount_.load(
                std::memory_order_acquire),
            .pendingWriteCount = pendingWriteCount_.load(
                std::memory_order_acquire),
            .droppedRequestCount = droppedRequestCount_.load(
                std::memory_order_acquire),
            .persistentStorageAvailable = persistentStorageAvailable_.load(
                std::memory_order_acquire),
        };
    }

    void Store::run(const std::stop_token stopToken)
    {
        std::error_code error;
        (void)std::filesystem::create_directories(settings_.root, error);
        const bool cacheRootAvailable = !error &&
            std::filesystem::is_directory(settings_.root, error) && !error;
        error.clear();
        const bool dataRootAvailable =
            std::filesystem::is_directory(settings_.dataRoot, error) && !error;
        persistentStorageAvailable_.store(
            cacheRootAvailable && dataRootAvailable,
            std::memory_order_release);
        refreshPersistentStatisticsAndPrune();
        while (!stopToken.stop_requested()) {
            Command command{};
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, stopToken, [this] { return !commands_.empty(); });
                if (stopToken.stop_requested()) {
                    break;
                }
                command = std::move(commands_.front());
                commands_.pop_front();
            }
            if (command.kind == CommandKind::Load) {
                processLoad(command.loadoutKey);
            } else {
                processSave(std::move(command.record));
                pendingWriteCount_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }

    void Store::processLoad(const std::uint64_t loadoutKey)
    {
        if (auto session = copySessionRecord(loadoutKey)) {
            publishResult({
                .loadoutKey = loadoutKey,
                .status = LoadStatus::HitSession,
                .record = std::move(session),
            });
            return;
        }
        if (!persistentStorageAvailable_.load(std::memory_order_acquire)) {
            publishResult({
                .loadoutKey = loadoutKey,
                .status = LoadStatus::Unavailable,
            });
            return;
        }
        const auto path = recordPath(settings_, loadoutKey);
        const auto bytes = readFile(path, settings_.maximumFileBytes);
        if (bytes.empty()) {
            publishResult({
                .loadoutKey = loadoutKey,
                .status = LoadStatus::Miss,
            });
            return;
        }
        auto record = std::make_unique<CompiledRecord>();
        if (!deserialize(bytes, *record) || record->loadoutKey != loadoutKey) {
            publishResult({
                .loadoutKey = loadoutKey,
                .status = LoadStatus::Invalid,
            });
            return;
        }
        const auto fingerprint = environmentFingerprint(settings_, *record);
        if (fingerprint == 0 || fingerprint != record->environmentFingerprint) {
            publishResult({
                .loadoutKey = loadoutKey,
                .status = LoadStatus::Miss,
            });
            return;
        }
        auto resultRecord = cloneRecord(*record);
        addSessionRecord(std::move(record));
        std::error_code touchError;
        std::filesystem::last_write_time(
            path,
            std::filesystem::file_time_type::clock::now(),
            touchError);
        publishResult({
            .loadoutKey = loadoutKey,
            .status = LoadStatus::HitPersistent,
            .record = std::move(resultRecord),
        });
    }

    void Store::processSave(std::unique_ptr<CompiledRecord> record)
    {
        if (!record) {
            return;
        }
        record->environmentFingerprint = environmentFingerprint(
            settings_, *record);
        auto sessionRecord = cloneRecord(*record);
        if (persistentStorageAvailable_.load(std::memory_order_acquire) &&
            record->environmentFingerprint == 0) {
            persistentStorageAvailable_.store(false, std::memory_order_release);
        } else if (record->environmentFingerprint != 0 &&
                   persistentStorageAvailable_.load(std::memory_order_acquire)) {
            const auto bytes = serialize(*record);
            if (!bytes.empty() && bytes.size() <= settings_.maximumFileBytes &&
                writeFileAtomically(
                    recordPath(settings_, record->loadoutKey), bytes)) {
                refreshPersistentStatisticsAndPrune();
            } else {
                droppedRequestCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        addSessionRecord(std::move(sessionRecord));
    }

    void Store::publishResult(LoadResult result)
    {
        std::scoped_lock lock(mutex_);
        if (results_.size() >= kMaximumQueuedResults) {
            results_.pop_front();
            droppedRequestCount_.fetch_add(1, std::memory_order_relaxed);
        }
        results_.push_back(std::move(result));
    }

    void Store::addSessionRecord(std::unique_ptr<CompiledRecord> record)
    {
        if (!record || settings_.maximumSessionBytes == 0) {
            return;
        }
        const auto bytes = estimatedBytes(*record);
        if (bytes > settings_.maximumSessionBytes) {
            return;
        }
        for (auto& entry : session_) {
            if (entry.record->loadoutKey != record->loadoutKey) {
                continue;
            }
            sessionBytes_ -= entry.bytes;
            entry.record = std::move(record);
            entry.bytes = bytes;
            entry.lastUse = ++sessionUseSequence_;
            sessionBytes_ += bytes;
            pruneSession();
            return;
        }
        sessionBytes_ += bytes;
        session_.push_back({
            .record = std::move(record),
            .lastUse = ++sessionUseSequence_,
            .bytes = bytes,
        });
        pruneSession();
    }

    std::unique_ptr<CompiledRecord> Store::copySessionRecord(
        const std::uint64_t loadoutKey)
    {
        for (auto& entry : session_) {
            if (entry.record->loadoutKey == loadoutKey) {
                entry.lastUse = ++sessionUseSequence_;
                return cloneRecord(*entry.record);
            }
        }
        return {};
    }

    void Store::pruneSession()
    {
        while (sessionBytes_ > settings_.maximumSessionBytes &&
               !session_.empty()) {
            const auto oldest = std::min_element(
                session_.begin(),
                session_.end(),
                [](const SessionEntry& left, const SessionEntry& right) {
                    return left.lastUse < right.lastUse;
                });
            sessionBytes_ -= oldest->bytes;
            session_.erase(oldest);
        }
    }

    void Store::refreshPersistentStatisticsAndPrune()
    {
        if (!persistentStorageAvailable_.load(std::memory_order_acquire)) {
            persistentRecordCount_.store(0, std::memory_order_release);
            return;
        }
        struct DiskEntry
        {
            std::filesystem::path path{};
            std::uint64_t bytes{ 0 };
            std::filesystem::file_time_type writeTime{};
        };
        std::vector<DiskEntry> entries{};
        std::uint64_t totalBytes = 0;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(
                 settings_.root, error), end;
             !error && iterator != end;
             iterator.increment(error)) {
            if (!iterator->is_regular_file(error) || error ||
                normalizedText(iterator->path().extension().string()) !=
                    ".pwmc") {
                error.clear();
                continue;
            }
            const auto bytes = iterator->file_size(error);
            if (error) {
                error.clear();
                continue;
            }
            const auto writeTime = iterator->last_write_time(error);
            if (error) {
                error.clear();
                continue;
            }
            entries.push_back({ iterator->path(), bytes, writeTime });
            totalBytes += bytes;
        }
        if (error) {
            persistentStorageAvailable_.store(false, std::memory_order_release);
            persistentRecordCount_.store(0, std::memory_order_release);
            return;
        }
        std::sort(
            entries.begin(),
            entries.end(),
            [](const DiskEntry& left, const DiskEntry& right) {
                return left.writeTime < right.writeTime;
            });
        while (!entries.empty() &&
               (entries.size() > settings_.maximumEntries ||
                   totalBytes > settings_.maximumDiskBytes)) {
            const auto removedBytes = entries.front().bytes;
            if (!std::filesystem::remove(entries.front().path, error) || error) {
                persistentStorageAvailable_.store(
                    false, std::memory_order_release);
                persistentRecordCount_.store(
                    static_cast<std::uint32_t>(entries.size()),
                    std::memory_order_release);
                return;
            }
            totalBytes -= removedBytes;
            entries.erase(entries.begin());
        }
        persistentRecordCount_.store(
            static_cast<std::uint32_t>(entries.size()),
            std::memory_order_release);
    }
}
