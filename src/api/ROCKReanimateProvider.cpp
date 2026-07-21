#define ROCK_REANIMATE_API_EXPORTS
#include "api/ROCKReanimateProvider.h"

#include "animation/NativeAnimationAuthority.h"
#include "api/ApiTransform.h"
#include "ReanimateLog.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace rock_reanimate::provider
{
    namespace
    {
        using namespace api;

        struct ConsumerSlot
        {
            std::uint64_t ownerToken{ 0 };
            std::uint32_t capabilities{ 0 };
            std::uint32_t authorityFlags{ 0 };
            std::uint32_t remainingFrames{ 0 };
            std::uint64_t authorityUpdatedFrame{ 0 };
            bool persistentAuthority{ false };
            char modName[64]{};
        };

        struct CallbackSlot
        {
            std::uint64_t token{ 0 };
            std::uint64_t ownerToken{ 0 };
            ReanimateEventCallbackV1 callback{ nullptr };
            void* userData{ nullptr };
        };

        constexpr std::uint32_t kAllCapabilities =
            static_cast<std::uint32_t>(ReanimateConsumerCapabilityV1::All);
        constexpr std::uint32_t kAllAuthorityFlags =
            static_cast<std::uint32_t>(
                ReanimateNativeAnimationAuthorityFlagV1::ReloadPose);

        std::array<ConsumerSlot, ROCK_REANIMATE_MAX_CONSUMERS_V1> s_consumers{};
        std::array<CallbackSlot, ROCK_REANIMATE_MAX_CALLBACKS_V1> s_callbacks{};
        std::atomic<std::uint64_t> s_nextToken{ 1 };
        std::atomic<bool> s_ready{ false };
        DWORD s_ownerThread{ 0 };
        std::uint64_t s_currentFrame{ 0 };
        std::uint32_t s_providerGeneration{ 0 };

        ReanimateRuntimeStateV1 s_runtimeState{};
        ReanimateConfigStateV1 s_configState{};
        std::atomic<std::uint64_t> s_runtimeSequence{ 0 };
        std::atomic<std::uint64_t> s_configSequence{ 0 };

        [[nodiscard]] bool onOwnerThread()
        {
            return s_ownerThread != 0 && s_ownerThread == GetCurrentThreadId();
        }

        [[nodiscard]] std::uint64_t nextToken()
        {
            auto token = s_nextToken.fetch_add(1, std::memory_order_acq_rel);
            if (token == 0) {
                token = s_nextToken.fetch_add(1, std::memory_order_acq_rel);
            }
            return token;
        }

        [[nodiscard]] ConsumerSlot* findConsumer(const std::uint64_t ownerToken)
        {
            if (ownerToken == 0) {
                return nullptr;
            }
            for (auto& consumer : s_consumers) {
                if (consumer.ownerToken == ownerToken) {
                    return std::addressof(consumer);
                }
            }
            return nullptr;
        }

        [[nodiscard]] const ConsumerSlot* findConsumerConst(
            const std::uint64_t ownerToken)
        {
            return findConsumer(ownerToken);
        }

        [[nodiscard]] bool hasCapability(
            const ConsumerSlot& consumer,
            const ReanimateConsumerCapabilityV1 capability)
        {
            return (consumer.capabilities &
                       static_cast<std::uint32_t>(capability)) != 0;
        }

        void removeCallbacksForOwner(const std::uint64_t ownerToken)
        {
            for (auto& callback : s_callbacks) {
                if (callback.ownerToken == ownerToken) {
                    callback = {};
                }
            }
        }

        void removeConsumer(const std::uint64_t ownerToken)
        {
            removeCallbacksForOwner(ownerToken);
            if (auto* consumer = findConsumer(ownerToken)) {
                *consumer = {};
            }
        }

        template <class State>
        void publishSeqlocked(
            State& destination,
            std::atomic<std::uint64_t>& sequence,
            const State& source)
        {
            sequence.fetch_add(1, std::memory_order_acq_rel);
            destination = source;
            sequence.fetch_add(1, std::memory_order_release);
        }

        template <class State>
        [[nodiscard]] bool copySeqlocked(
            const State& source,
            const std::atomic<std::uint64_t>& sequence,
            State& destination)
        {
            for (int attempt = 0; attempt < 3; ++attempt) {
                const auto before = sequence.load(std::memory_order_acquire);
                if ((before & 1u) != 0) {
                    continue;
                }
                destination = source;
                std::atomic_thread_fence(std::memory_order_acquire);
                const auto after = sequence.load(std::memory_order_relaxed);
                if (before == after) {
                    return true;
                }
            }
            destination = {};
            return false;
        }

        std::uint32_t ROCK_REANIMATE_CALL getVersion()
        {
            return ROCK_REANIMATE_API_VERSION;
        }

        std::uint32_t ROCK_REANIMATE_CALL getModVersion()
        {
            return ROCK_REANIMATE_MOD_VERSION;
        }

        bool ROCK_REANIMATE_CALL apiIsReady()
        {
            return isReady();
        }

        bool ROCK_REANIMATE_CALL getRuntimeStateV1(
            const std::uint64_t ownerToken,
            ReanimateRuntimeStateV1* outState)
        {
            if (!outState ||
                outState->size < sizeof(ReanimateRuntimeStateV1) ||
                outState->version != ROCK_REANIMATE_API_VERSION ||
                !onOwnerThread()) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::RuntimeState)) {
                return false;
            }
            ReanimateRuntimeStateV1 state{};
            if (!copySeqlocked(s_runtimeState, s_runtimeSequence, state)) {
                *outState = {};
                return false;
            }
            *outState = state;
            return true;
        }

        bool ROCK_REANIMATE_CALL getConfigStateV1(
            const std::uint64_t ownerToken,
            ReanimateConfigStateV1* outState)
        {
            if (!outState ||
                outState->size < sizeof(ReanimateConfigStateV1) ||
                outState->version != ROCK_REANIMATE_API_VERSION ||
                !onOwnerThread()) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::RuntimeState)) {
                return false;
            }
            ReanimateConfigStateV1 state{};
            if (!copySeqlocked(s_configState, s_configSequence, state)) {
                *outState = {};
                return false;
            }
            *outState = state;
            return true;
        }

        std::uint32_t ROCK_REANIMATE_CALL getCapturedTransformCountV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::CapturedTransforms)) {
                return 0;
            }
            ReanimateRuntimeStateV1 state{};
            return copySeqlocked(s_runtimeState, s_runtimeSequence, state) ?
                state.capturedTransformCount :
                0;
        }

        std::uint32_t ROCK_REANIMATE_CALL copyCapturedTransformsV1(
            const std::uint64_t ownerToken,
            ReanimateCapturedTransformV1* outTransforms,
            const std::uint32_t maxTransforms)
        {
            if (!outTransforms || maxTransforms == 0 || !onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::CapturedTransforms)) {
                return 0;
            }

            std::array<
                native_animation_authority::CapturedTransform,
                native_animation_authority::kMaxCapturedTransforms> source{};
            const auto requested = std::min(
                maxTransforms,
                static_cast<std::uint32_t>(source.size()));
            const auto copied =
                native_animation_authority::copyCapturedTransforms(
                    source.data(),
                    requested);
            for (std::uint32_t index = 0; index < copied; ++index) {
                auto& destination = outTransforms[index];
                destination = {};
                std::memcpy(
                    destination.name,
                    source[index].name,
                    sizeof(destination.name));
                api_transform::fromNi(
                    source[index].local,
                    destination.local);
                destination.authorityFlags = source[index].flags;
                destination.sourceIndex = source[index].sourceIndex;
                destination.destinationIndex =
                    source[index].destinationIndex;
            }
            return copied;
        }

        bool ROCK_REANIMATE_CALL getNativeHandPoseV1(
            const std::uint64_t ownerToken,
            const ReanimateHandV1 hand,
            ReanimateNativeHandPoseV1* outPose)
        {
            if (!outPose ||
                outPose->size < sizeof(ReanimateNativeHandPoseV1) ||
                outPose->version != ROCK_REANIMATE_API_VERSION ||
                !onOwnerThread() ||
                (hand != ReanimateHandV1::Right &&
                    hand != ReanimateHandV1::Left)) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::NativeHandPose)) {
                return false;
            }

            native_animation_authority::NativeHandPose source{};
            if (!native_animation_authority::queryNativeHandPose(
                    hand == ReanimateHandV1::Left,
                    source)) {
                *outPose = {};
                return false;
            }

            ReanimateNativeHandPoseV1 result{};
            result.hand = hand;
            result.fingerLocalTransformMask = source.fingerLocalMask;
            api_transform::fromNi(source.handInWeapon, result.handInWeapon);
            for (std::size_t index = 0;
                 index < source.fingerLocals.size();
                 ++index) {
                api_transform::fromNi(
                    source.fingerLocals[index],
                    result.fingerLocalTransforms[index]);
            }
            result.captureSequence = source.captureSequence;
            *outPose = result;
            return true;
        }

        ReanimateResultV1 ROCK_REANIMATE_CALL registerConsumerV1(
            const ReanimateConsumerRegistrationV1* registration,
            ReanimateConsumerHandleV1* outHandle)
        {
            if (!registration || !outHandle) {
                return ReanimateResultV1::InvalidArgument;
            }
            if (registration->size <
                    sizeof(ReanimateConsumerRegistrationV1) ||
                outHandle->size < sizeof(ReanimateConsumerHandleV1)) {
                return ReanimateResultV1::InvalidSize;
            }
            *outHandle = {};
            if (registration->version != ROCK_REANIMATE_API_VERSION) {
                return ReanimateResultV1::UnsupportedVersion;
            }
            if (!isReady()) {
                return ReanimateResultV1::NotReady;
            }
            if (!onOwnerThread()) {
                return ReanimateResultV1::WrongThread;
            }

            const auto* modNameEnd = static_cast<const char*>(std::memchr(
                registration->modName,
                '\0',
                sizeof(registration->modName)));
            if (!modNameEnd || modNameEnd == registration->modName) {
                return ReanimateResultV1::InvalidArgument;
            }
            for (const auto& consumer : s_consumers) {
                if (consumer.ownerToken != 0 &&
                    std::strcmp(consumer.modName, registration->modName) == 0) {
                    return ReanimateResultV1::OwnerConflict;
                }
            }

            for (auto& consumer : s_consumers) {
                if (consumer.ownerToken != 0) {
                    continue;
                }
                consumer.ownerToken = nextToken();
                consumer.capabilities =
                    registration->requestedCapabilities & kAllCapabilities;
                std::memcpy(
                    consumer.modName,
                    registration->modName,
                    sizeof(consumer.modName));
                consumer.modName[sizeof(consumer.modName) - 1] = '\0';

                ReanimateConsumerHandleV1 handle{};
                handle.ownerToken = consumer.ownerToken;
                handle.grantedCapabilities = consumer.capabilities;
                handle.providerGeneration = s_providerGeneration;
                *outHandle = handle;
                REANIMATE_LOG_INFO(
                    Api,
                    "Consumer '{}' registered owner={:016X} capabilities=0x{:08X}",
                    consumer.modName,
                    consumer.ownerToken,
                    consumer.capabilities);
                return ReanimateResultV1::Ok;
            }
            return ReanimateResultV1::CapacityReached;
        }

        ReanimateResultV1 ROCK_REANIMATE_CALL unregisterConsumerV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return ReanimateResultV1::WrongThread;
            }
            if (!findConsumer(ownerToken)) {
                return ReanimateResultV1::UnknownOwner;
            }
            removeConsumer(ownerToken);
            return ReanimateResultV1::Ok;
        }

        std::uint32_t ROCK_REANIMATE_CALL getGrantedCapabilitiesV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            return consumer ? consumer->capabilities : 0;
        }

        ReanimateResultV1 ROCK_REANIMATE_CALL setAnimationAuthorityV1(
            const std::uint64_t ownerToken,
            const ReanimateAuthorityRequestV1* request)
        {
            if (!request ||
                request->size < sizeof(ReanimateAuthorityRequestV1)) {
                return ReanimateResultV1::InvalidArgument;
            }
            if (request->version != ROCK_REANIMATE_API_VERSION) {
                return ReanimateResultV1::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return ReanimateResultV1::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return ReanimateResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::AnimationAuthority)) {
                return ReanimateResultV1::PermissionDenied;
            }
            if ((request->flags & ~kAllAuthorityFlags) != 0 ||
                request->leaseFrames >
                    ROCK_REANIMATE_MAX_AUTHORITY_LEASE_FRAMES_V1) {
                return ReanimateResultV1::InvalidArgument;
            }

            consumer->authorityFlags = request->flags;
            consumer->remainingFrames = request->leaseFrames;
            consumer->persistentAuthority = request->leaseFrames == 0;
            consumer->authorityUpdatedFrame = s_currentFrame;
            return ReanimateResultV1::Ok;
        }

        ReanimateResultV1 ROCK_REANIMATE_CALL clearAnimationAuthorityV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return ReanimateResultV1::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return ReanimateResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::AnimationAuthority)) {
                return ReanimateResultV1::PermissionDenied;
            }
            consumer->authorityFlags = 0;
            consumer->remainingFrames = 0;
            consumer->persistentAuthority = false;
            return ReanimateResultV1::Ok;
        }

        ReanimateResultV1 ROCK_REANIMATE_CALL registerEventCallbackV1(
            const std::uint64_t ownerToken,
            const ReanimateEventCallbackV1 callback,
            void* userData,
            std::uint64_t* outCallbackToken)
        {
            if (!callback || !outCallbackToken) {
                return ReanimateResultV1::InvalidArgument;
            }
            *outCallbackToken = 0;
            if (!onOwnerThread()) {
                return ReanimateResultV1::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return ReanimateResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ReanimateConsumerCapabilityV1::FrameCallbacks)) {
                return ReanimateResultV1::PermissionDenied;
            }

            for (auto& slot : s_callbacks) {
                if (slot.token != 0) {
                    continue;
                }
                slot.token = nextToken();
                slot.ownerToken = ownerToken;
                slot.callback = callback;
                slot.userData = userData;
                *outCallbackToken = slot.token;
                return ReanimateResultV1::Ok;
            }
            return ReanimateResultV1::CapacityReached;
        }

        ReanimateResultV1 ROCK_REANIMATE_CALL unregisterEventCallbackV1(
            const std::uint64_t ownerToken,
            const std::uint64_t callbackToken)
        {
            if (!onOwnerThread()) {
                return ReanimateResultV1::WrongThread;
            }
            if (!findConsumer(ownerToken)) {
                return ReanimateResultV1::UnknownOwner;
            }
            for (auto& slot : s_callbacks) {
                if (slot.token == callbackToken &&
                    slot.ownerToken == ownerToken) {
                    slot = {};
                    return ReanimateResultV1::Ok;
                }
            }
            return ReanimateResultV1::NotFound;
        }

        const ReanimateProviderApiV1 s_api{
            &getVersion,
            &getModVersion,
            &apiIsReady,
            &getRuntimeStateV1,
            &getConfigStateV1,
            &getCapturedTransformCountV1,
            &copyCapturedTransformsV1,
            &getNativeHandPoseV1,
            &registerConsumerV1,
            &unregisterConsumerV1,
            &getGrantedCapabilitiesV1,
            &setAnimationAuthorityV1,
            &clearAnimationAuthorityV1,
            &registerEventCallbackV1,
            &unregisterEventCallbackV1,
        };
    }

    void initialize()
    {
        s_ownerThread = GetCurrentThreadId();
        s_consumers = {};
        s_callbacks = {};
        s_currentFrame = 0;
        if (++s_providerGeneration == 0) {
            s_providerGeneration = 1;
        }
        s_ready.store(true, std::memory_order_release);
    }

    void shutdown()
    {
        s_ready.store(false, std::memory_order_release);
        s_consumers = {};
        s_callbacks = {};
        s_currentFrame = 0;
        s_ownerThread = 0;
    }

    void beginFrame(const std::uint64_t frameIndex)
    {
        if (onOwnerThread()) {
            s_currentFrame = frameIndex;
        }
    }

    void completeFrame()
    {
        if (!onOwnerThread()) {
            return;
        }
        for (auto& consumer : s_consumers) {
            if (consumer.ownerToken == 0 ||
                consumer.authorityFlags == 0 ||
                consumer.persistentAuthority ||
                consumer.authorityUpdatedFrame == s_currentFrame) {
                continue;
            }
            if (consumer.remainingFrames > 0) {
                --consumer.remainingFrames;
            }
            if (consumer.remainingFrames == 0) {
                consumer.authorityFlags = 0;
            }
        }
    }

    void resetRuntime()
    {
        if (!onOwnerThread()) {
            return;
        }
        for (auto& consumer : s_consumers) {
            consumer.authorityFlags = 0;
            consumer.remainingFrames = 0;
            consumer.persistentAuthority = false;
        }
        ReanimateRuntimeStateV1 state{};
        state.reanimateProviderGeneration = s_providerGeneration;
        publishRuntime(state);
        dispatchEvent(ReanimateEventKindV1::RuntimeReset);
    }

    void publishConfig(const ReanimateConfigStateV1& state)
    {
        publishSeqlocked(s_configState, s_configSequence, state);
    }

    void publishRuntime(const ReanimateRuntimeStateV1& state)
    {
        publishSeqlocked(s_runtimeState, s_runtimeSequence, state);
    }

    void dispatchEvent(const ReanimateEventKindV1 kind)
    {
        if (!onOwnerThread()) {
            return;
        }
        ReanimateRuntimeStateV1 runtime{};
        if (!copySeqlocked(s_runtimeState, s_runtimeSequence, runtime)) {
            return;
        }
        ReanimateEventV1 eventData{};
        eventData.kind = kind;
        eventData.runtime = runtime;

        for (auto& slot : s_callbacks) {
            if (slot.token == 0 || !slot.callback) {
                continue;
            }
            const auto ownerToken = slot.ownerToken;
            const auto callback = slot.callback;
            void* const userData = slot.userData;
#if defined(_MSC_VER)
            __try {
                callback(&eventData, userData);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                REANIMATE_LOG_ERROR(
                    Api,
                    "Consumer callback faulted; unregistering owner={:016X}",
                    ownerToken);
                removeConsumer(ownerToken);
            }
#else
            callback(&eventData, userData);
#endif
        }
    }

    std::uint32_t currentConsumerAuthorityFlags()
    {
        if (!onOwnerThread()) {
            return 0;
        }
        std::uint32_t flags = 0;
        for (const auto& consumer : s_consumers) {
            flags |= consumer.authorityFlags;
        }
        return flags & kAllAuthorityFlags;
    }

    std::uint32_t generation()
    {
        return s_providerGeneration;
    }

    bool isReady()
    {
        return s_ready.load(std::memory_order_acquire);
    }

    const ReanimateProviderApiV1* apiTable()
    {
        return std::addressof(s_api);
    }
}

extern "C" ROCK_REANIMATE_API
    const rock_reanimate::api::ReanimateProviderApiV1* ROCK_REANIMATE_CALL
    ROCKREANIMATEAPI_GetProviderApi(const std::uint32_t requestedVersion)
{
    if (requestedVersion > rock_reanimate::api::ROCK_REANIMATE_API_VERSION) {
        return nullptr;
    }
    return rock_reanimate::provider::apiTable();
}
