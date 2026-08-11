#define PAPER_API_EXPORTS
#include "api/PAPERProvider.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation_evidence/AnimationEvidence.h"
#include "api/ApiTransform.h"
#include "development/DevelopmentCapturePolicy.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservation.h"
#include "reload_stages/ReloadStages.h"
#include "weapon_motion/WeaponMotion.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>

namespace paper::provider
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
            std::uint32_t captureScopes{ 0 };
            std::uint32_t captureDeniedScopes{ 0 };
            std::uint32_t captureRemainingFrames{ 0 };
            std::uint64_t captureUpdatedFrame{ 0 };
            char modName[64]{};
        };

        struct CallbackSlot
        {
            std::uint64_t token{ 0 };
            std::uint64_t ownerToken{ 0 };
            PaperEventCallbackV1 callback{ nullptr };
            void* userData{ nullptr };
        };

        constexpr std::uint32_t kAllCapabilities =
            static_cast<std::uint32_t>(PaperConsumerCapabilityV1::All);
        constexpr std::uint32_t kReloadAnimationReadCapabilities =
            static_cast<std::uint32_t>(
                PaperConsumerCapabilityV1::ReloadAnimationEvidence) |
            static_cast<std::uint32_t>(
                PaperConsumerCapabilityV1::ReloadAnimationTelemetry);
        constexpr std::uint32_t kAllAuthorityFlags =
            static_cast<std::uint32_t>(
                PaperNativeAnimationAuthorityFlagV1::ReloadPose);

        std::array<ConsumerSlot, PAPER_MAX_CONSUMERS_V1> s_consumers{};
        std::array<CallbackSlot, PAPER_MAX_CALLBACKS_V1> s_callbacks{};
        std::atomic<std::uint64_t> s_nextToken{ 1 };
        std::atomic<bool> s_ready{ false };
        DWORD s_ownerThread{ 0 };
        std::uint64_t s_currentFrame{ 0 };
        std::uint32_t s_providerGeneration{ 0 };
        PaperDevelopmentCaptureModeV1 s_captureMode{
            PaperDevelopmentCaptureModeV1::User
        };
        PaperWeaponMotionCacheAccessV1 s_cacheAccess{
            PaperWeaponMotionCacheAccessV1::Off
        };
        std::uint32_t s_captureConfigFlags{ 0 };
        std::uint32_t s_captureAllowedScopes{ 0 };
        std::uint32_t s_captureAutoStartScopes{ 0 };
        std::uint32_t s_captureLegacyScopes{ 0 };
        std::uint32_t s_captureActiveScopes{ 0 };
        std::uint64_t s_captureConfigRevision{ 0 };

        PaperRuntimeStateV1 s_runtimeState{};
        PaperConfigStateV1 s_configState{};
        std::atomic<std::uint64_t> s_runtimeSequence{ 0 };
        std::atomic<std::uint64_t> s_configSequence{ 0 };

        struct NativePosePipelineSnapshot
        {
            PaperNativePoseFrameStateV1 frame{};
            PaperNativeHandSolutionV1 rightHand{};
            PaperNativeHandSolutionV1 leftHand{
                .hand = PaperHandV1::Left,
                .role = PaperNativeHandRoleV1::Support,
            };
        };

        NativePosePipelineSnapshot s_nativePosePipeline{};
        std::atomic<std::uint64_t> s_nativePosePipelineSequence{ 0 };
        std::uint64_t s_nativePosePublicationSequence{ 0 };

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
            const PaperConsumerCapabilityV1 capability)
        {
            return (consumer.capabilities &
                       static_cast<std::uint32_t>(capability)) != 0;
        }

        [[nodiscard]] bool hasCaptureConfigFlag(
            const PaperDevelopmentCaptureConfigFlagV1 flag)
        {
            return (s_captureConfigFlags &
                       static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] std::uint32_t aggregateCaptureScopes()
        {
            std::uint32_t scopes = 0;
            for (const auto& consumer : s_consumers) {
                if (consumer.ownerToken != 0) {
                    scopes |= consumer.captureScopes;
                }
            }
            return scopes & development_capture_policy::kAllScopes;
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

        std::uint32_t PAPER_CALL getVersion()
        {
            return PAPER_API_VERSION;
        }

        std::uint32_t PAPER_CALL getModVersion()
        {
            return PAPER_MOD_VERSION;
        }

        bool PAPER_CALL apiIsReady()
        {
            return isReady();
        }

        bool PAPER_CALL getRuntimeStateV1(
            const std::uint64_t ownerToken,
            PaperRuntimeStateV1* outState)
        {
            if (!outState ||
                outState->size < sizeof(PaperRuntimeStateV1) ||
                outState->version != PAPER_API_VERSION ||
                !onOwnerThread()) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::RuntimeState)) {
                return false;
            }
            PaperRuntimeStateV1 state{};
            if (!copySeqlocked(s_runtimeState, s_runtimeSequence, state)) {
                *outState = {};
                return false;
            }
            *outState = state;
            return true;
        }

        bool PAPER_CALL getConfigStateV1(
            const std::uint64_t ownerToken,
            PaperConfigStateV1* outState)
        {
            if (!outState ||
                outState->size < sizeof(PaperConfigStateV1) ||
                outState->version != PAPER_API_VERSION ||
                !onOwnerThread()) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::RuntimeState)) {
                return false;
            }
            PaperConfigStateV1 state{};
            if (!copySeqlocked(s_configState, s_configSequence, state)) {
                *outState = {};
                return false;
            }
            *outState = state;
            return true;
        }

        std::uint32_t PAPER_CALL getCapturedTransformCountV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::CapturedTransforms)) {
                return 0;
            }
            PaperRuntimeStateV1 state{};
            return copySeqlocked(s_runtimeState, s_runtimeSequence, state) ?
                state.capturedTransformCount :
                0;
        }

        std::uint32_t PAPER_CALL copyCapturedTransformsV1(
            const std::uint64_t ownerToken,
            PaperCapturedTransformV1* outTransforms,
            const std::uint32_t maxTransforms)
        {
            if (!outTransforms || maxTransforms == 0 || !onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::CapturedTransforms)) {
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

        bool PAPER_CALL getNativeHandPoseV1(
            const std::uint64_t ownerToken,
            const PaperHandV1 hand,
            PaperNativeHandPoseV1* outPose)
        {
            if (!outPose ||
                outPose->size < sizeof(PaperNativeHandPoseV1) ||
                outPose->version != PAPER_API_VERSION ||
                !onOwnerThread() ||
                (hand != PaperHandV1::Right &&
                    hand != PaperHandV1::Left)) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::NativeHandPose)) {
                return false;
            }

            native_animation_authority::NativeHandPose source{};
            if (!native_animation_authority::queryNativeHandPose(
                    hand == PaperHandV1::Left,
                    source)) {
                *outPose = {};
                return false;
            }

            PaperNativeHandPoseV1 result{};
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

        PaperResultV1 PAPER_CALL registerConsumerV1(
            const PaperConsumerRegistrationV1* registration,
            PaperConsumerHandleV1* outHandle)
        {
            if (!registration || !outHandle) {
                return PaperResultV1::InvalidArgument;
            }
            if (registration->size <
                    sizeof(PaperConsumerRegistrationV1) ||
                outHandle->size < sizeof(PaperConsumerHandleV1)) {
                return PaperResultV1::InvalidSize;
            }
            *outHandle = {};
            if (registration->version != PAPER_API_VERSION) {
                return PaperResultV1::UnsupportedVersion;
            }
            if (!isReady()) {
                return PaperResultV1::NotReady;
            }
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }

            const auto* modNameEnd = static_cast<const char*>(std::memchr(
                registration->modName,
                '\0',
                sizeof(registration->modName)));
            if (!modNameEnd || modNameEnd == registration->modName) {
                return PaperResultV1::InvalidArgument;
            }
            for (const auto& consumer : s_consumers) {
                if (consumer.ownerToken != 0 &&
                    std::strcmp(consumer.modName, registration->modName) == 0) {
                    return PaperResultV1::OwnerConflict;
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

                PaperConsumerHandleV1 handle{};
                handle.ownerToken = consumer.ownerToken;
                handle.grantedCapabilities = consumer.capabilities;
                handle.providerGeneration = s_providerGeneration;
                *outHandle = handle;
                PAPER_LOG_INFO(
                    Api,
                    "Consumer '{}' registered owner={:016X} capabilities=0x{:08X}",
                    consumer.modName,
                    consumer.ownerToken,
                    consumer.capabilities);
                return PaperResultV1::Ok;
            }
            return PaperResultV1::CapacityReached;
        }

        PaperResultV1 PAPER_CALL unregisterConsumerV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            if (!findConsumer(ownerToken)) {
                return PaperResultV1::UnknownOwner;
            }
            removeConsumer(ownerToken);
            return PaperResultV1::Ok;
        }

        std::uint32_t PAPER_CALL getGrantedCapabilitiesV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            return consumer ? consumer->capabilities : 0;
        }

        PaperResultV1 PAPER_CALL setAnimationAuthorityV1(
            const std::uint64_t ownerToken,
            const PaperAuthorityRequestV1* request)
        {
            if (!request ||
                request->size < sizeof(PaperAuthorityRequestV1)) {
                return PaperResultV1::InvalidArgument;
            }
            if (request->version != PAPER_API_VERSION) {
                return PaperResultV1::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::AnimationAuthority)) {
                return PaperResultV1::PermissionDenied;
            }
            if ((request->flags & ~kAllAuthorityFlags) != 0 ||
                request->leaseFrames >
                    PAPER_MAX_AUTHORITY_LEASE_FRAMES_V1) {
                return PaperResultV1::InvalidArgument;
            }

            consumer->authorityFlags = request->flags;
            consumer->remainingFrames = request->leaseFrames;
            consumer->persistentAuthority = request->leaseFrames == 0;
            consumer->authorityUpdatedFrame = s_currentFrame;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL clearAnimationAuthorityV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::AnimationAuthority)) {
                return PaperResultV1::PermissionDenied;
            }
            consumer->authorityFlags = 0;
            consumer->remainingFrames = 0;
            consumer->persistentAuthority = false;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL setDevelopmentCaptureV1(
            const std::uint64_t ownerToken,
            const PaperDevelopmentCaptureRequestV1* request)
        {
            if (!request) {
                return PaperResultV1::InvalidArgument;
            }
            if (request->size < sizeof(PaperDevelopmentCaptureRequestV1)) {
                return PaperResultV1::InvalidSize;
            }
            if (request->version != PAPER_API_VERSION) {
                return PaperResultV1::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::DevelopmentCaptureControl)) {
                return PaperResultV1::PermissionDenied;
            }
            if (request->scopes == 0 ||
                (request->scopes & ~development_capture_policy::kAllScopes) != 0 ||
                request->leaseFrames == 0 ||
                request->leaseFrames >
                    PAPER_MAX_DEVELOPMENT_CAPTURE_LEASE_FRAMES_V1) {
                return PaperResultV1::InvalidArgument;
            }

            const auto requested =
                development_capture_policy::expandDependencies(request->scopes);
            const auto denied = requested & ~s_captureAllowedScopes;
            if (!hasCaptureConfigFlag(
                    PaperDevelopmentCaptureConfigFlagV1::
                        AllowApiActivation) ||
                denied != 0) {
                consumer->captureDeniedScopes = hasCaptureConfigFlag(
                        PaperDevelopmentCaptureConfigFlagV1::
                            AllowApiActivation) ?
                    denied : requested;
                return PaperResultV1::PermissionDenied;
            }

            consumer->captureScopes = requested;
            consumer->captureDeniedScopes = 0;
            consumer->captureRemainingFrames = request->leaseFrames;
            consumer->captureUpdatedFrame = s_currentFrame;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL clearDevelopmentCaptureV1(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::DevelopmentCaptureControl)) {
                return PaperResultV1::PermissionDenied;
            }
            consumer->captureScopes = 0;
            consumer->captureDeniedScopes = 0;
            consumer->captureRemainingFrames = 0;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL getDevelopmentCaptureStateV1(
            const std::uint64_t ownerToken,
            PaperDevelopmentCaptureStateV1* outState)
        {
            if (!outState) {
                return PaperResultV1::InvalidArgument;
            }
            if (outState->size < sizeof(PaperDevelopmentCaptureStateV1)) {
                return PaperResultV1::InvalidSize;
            }
            if (outState->version != PAPER_API_VERSION) {
                return PaperResultV1::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::DevelopmentCaptureControl)) {
                return PaperResultV1::PermissionDenied;
            }
            PaperDevelopmentCaptureStateV1 state{};
            state.mode = s_captureMode;
            state.cacheAccess = s_cacheAccess;
            state.allowedScopes = s_captureAllowedScopes;
            state.autoStartScopes = s_captureAutoStartScopes;
            state.ownerRequestedScopes = consumer->captureScopes;
            state.aggregateRequestedScopes = aggregateCaptureScopes();
            state.activeScopes = s_captureActiveScopes;
            state.deniedScopes = consumer->captureDeniedScopes;
            state.remainingLeaseFrames = consumer->captureRemainingFrames;
            state.configRevision = s_captureConfigRevision;
            if (s_captureAutoStartScopes != 0) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    PaperDevelopmentCaptureStateFlagV1::AutoStartActive);
            }
            if (hasCaptureConfigFlag(
                    PaperDevelopmentCaptureConfigFlagV1::
                        AllowApiActivation)) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    PaperDevelopmentCaptureStateFlagV1::ApiActivationAllowed);
            }
            if (consumer->captureScopes != 0) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    PaperDevelopmentCaptureStateFlagV1::OwnerLeaseActive);
            }
            if (state.aggregateRequestedScopes != 0) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    PaperDevelopmentCaptureStateFlagV1::AggregateLeaseActive);
            }

            PaperReloadAnimationCatalogStateV1 animation{};
            if (animation_evidence::getCatalogState(animation) ==
                PaperResultV1::Ok) {
                state.weaponFormId = animation.weaponFormId;
                state.weaponGenerationKey = animation.weaponGenerationKey;
                state.storedSampleBytes = animation.storedSampleBytes;
                state.sampleStorageBudgetBytes =
                    animation.sampleStorageBudgetBytes;
                state.exactPreharvestState = animation.exactPreharvestState;
                state.animationStatusFlags = animation.statusFlags;
                state.animationClipCount = animation.clipCount;
                state.exactAnimationClipCount = animation.exactClipCount;
                state.exactClipsSampled = animation.exactClipsSampled;
                state.exactClipsRejected = animation.exactClipsRejected;
                if (animation.exactPreharvestState ==
                    PaperReloadAnimationPreharvestStateV1::Completed) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        PaperDevelopmentCaptureStateFlagV1::HarvestCompleted);
                } else if (animation.exactPreharvestState ==
                           PaperReloadAnimationPreharvestStateV1::Failed) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        PaperDevelopmentCaptureStateFlagV1::HarvestFailed);
                }
            }

            PaperWeaponMotionStoreStateV1 store{};
            if (weapon_motion::getStoreState(store) == PaperResultV1::Ok) {
                state.motionStoreFlags = store.flags;
                state.motionPartCount = store.inMemoryPartCount;
                state.motionStageCount = store.inMemoryStageCount;
                state.persistentRecordCount = store.persistentRecordCount;
                state.pendingWriteCount = store.pendingWriteCount;
                const auto hasStoreFlag = [&store](
                    const PaperWeaponMotionStoreFlagV1 flag) {
                    return (store.flags & static_cast<std::uint32_t>(flag)) != 0;
                };
                if (hasStoreFlag(
                        PaperWeaponMotionStoreFlagV1::CacheLookupPending)) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        PaperDevelopmentCaptureStateFlagV1::
                            CacheLookupPending);
                }
                if (hasStoreFlag(
                        PaperWeaponMotionStoreFlagV1::SessionCacheHit)) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        PaperDevelopmentCaptureStateFlagV1::SessionCacheHit);
                }
                if (hasStoreFlag(
                        PaperWeaponMotionStoreFlagV1::PersistentCacheHit)) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        PaperDevelopmentCaptureStateFlagV1::
                            PersistentCacheHit);
                }
            }
            *outState = state;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL registerEventCallbackV1(
            const std::uint64_t ownerToken,
            const PaperEventCallbackV1 callback,
            void* userData,
            std::uint64_t* outCallbackToken)
        {
            if (!callback || !outCallbackToken) {
                return PaperResultV1::InvalidArgument;
            }
            *outCallbackToken = 0;
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::FrameCallbacks)) {
                return PaperResultV1::PermissionDenied;
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
                return PaperResultV1::Ok;
            }
            return PaperResultV1::CapacityReached;
        }

        PaperResultV1 PAPER_CALL unregisterEventCallbackV1(
            const std::uint64_t ownerToken,
            const std::uint64_t callbackToken)
        {
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            if (!findConsumer(ownerToken)) {
                return PaperResultV1::UnknownOwner;
            }
            for (auto& slot : s_callbacks) {
                if (slot.token == callbackToken &&
                    slot.ownerToken == ownerToken) {
                    slot = {};
                    return PaperResultV1::Ok;
                }
            }
            return PaperResultV1::NotFound;
        }

        [[nodiscard]] PaperResultV1 validateReloadConsumerAny(
            const std::uint64_t ownerToken,
            const std::uint32_t capabilityMask)
        {
            if (!isReady()) {
                return PaperResultV1::NotReady;
            }
            if (!onOwnerThread()) {
                return PaperResultV1::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return PaperResultV1::UnknownOwner;
            }
            return (consumer->capabilities & capabilityMask) != 0 ?
                PaperResultV1::Ok :
                PaperResultV1::PermissionDenied;
        }

        [[nodiscard]] PaperResultV1 validateReloadConsumer(
            const std::uint64_t ownerToken,
            const PaperConsumerCapabilityV1 capability)
        {
            return validateReloadConsumerAny(
                ownerToken,
                static_cast<std::uint32_t>(capability));
        }

        template <class State>
        [[nodiscard]] PaperResultV1 validateReloadOutput(
            const std::uint64_t ownerToken,
            const PaperConsumerCapabilityV1 capability,
            const State* output)
        {
            if (!output) {
                return PaperResultV1::InvalidArgument;
            }
            if (output->size < sizeof(State)) {
                return PaperResultV1::InvalidSize;
            }
            if (output->version != PAPER_API_VERSION) {
                return PaperResultV1::UnsupportedVersion;
            }
            return validateReloadConsumer(ownerToken, capability);
        }

        template <class State>
        [[nodiscard]] PaperResultV1 validateReloadOutputAny(
            const std::uint64_t ownerToken,
            const std::uint32_t capabilityMask,
            const State* output)
        {
            if (!output) {
                return PaperResultV1::InvalidArgument;
            }
            if (output->size < sizeof(State)) {
                return PaperResultV1::InvalidSize;
            }
            if (output->version != PAPER_API_VERSION) {
                return PaperResultV1::UnsupportedVersion;
            }
            return validateReloadConsumerAny(ownerToken, capabilityMask);
        }

        PaperResultV1 PAPER_CALL getReloadObservationLimitsV1(
            const std::uint64_t ownerToken,
            PaperReloadObservationLimitsV1* outLimits)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadObservations,
                outLimits);
            return validation == PaperResultV1::Ok ?
                reload_observation::getLimits(*outLimits) :
                validation;
        }

        PaperResultV1 PAPER_CALL getReloadCatalogStateV1(
            const std::uint64_t ownerToken,
            PaperReloadCatalogStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadObservations,
                outState);
            return validation == PaperResultV1::Ok ?
                reload_observation::getCatalogState(*outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadCatalogNodesV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstNode,
            PaperReloadNodeCatalogEntryV1* outNodes,
            const std::uint32_t maxNodes,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadObservations);
            return validation == PaperResultV1::Ok ?
                reload_observation::copyCatalogNodes(
                    catalogSequence,
                    firstNode,
                    outNodes,
                    maxNodes,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadEvidenceV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstEvidence,
            PaperReloadEvidenceV1* outEvidence,
            const std::uint32_t maxEvidence,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadObservations);
            return validation == PaperResultV1::Ok ?
                reload_observation::copyEvidence(
                    catalogSequence,
                    firstEvidence,
                    outEvidence,
                    maxEvidence,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadEvidencePointsV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t evidenceId,
            const std::uint32_t firstPoint,
            PaperPoint3V1* outPoints,
            const std::uint32_t maxPoints,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadEvidenceGeometry);
            return validation == PaperResultV1::Ok ?
                reload_observation::copyEvidencePoints(
                    catalogSequence,
                    evidenceId,
                    firstPoint,
                    outPoints,
                    maxPoints,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL getReloadFrameStateV1(
            const std::uint64_t ownerToken,
            PaperReloadFrameStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadObservations,
                outState);
            return validation == PaperResultV1::Ok ?
                reload_observation::getFrameState(*outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadNodeObservationsV1(
            const std::uint64_t ownerToken,
            const std::uint64_t snapshotSequence,
            const std::uint32_t firstObservation,
            PaperReloadNodeObservationV1* outObservations,
            const std::uint32_t maxObservations,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::ReloadObservations);
            return validation == PaperResultV1::Ok ?
                reload_observation::copyNodeObservations(
                    snapshotSequence,
                    firstObservation,
                    outObservations,
                    maxObservations,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL getReloadAnimationLimitsV1(
            const std::uint64_t ownerToken,
            PaperReloadAnimationLimitsV1* outLimits)
        {
            const auto validation = validateReloadOutputAny(
                ownerToken,
                kReloadAnimationReadCapabilities,
                outLimits);
            return validation == PaperResultV1::Ok ?
                animation_evidence::getLimits(*outLimits) :
                validation;
        }

        PaperResultV1 PAPER_CALL getReloadAnimationCatalogStateV1(
            const std::uint64_t ownerToken,
            PaperReloadAnimationCatalogStateV1* outState)
        {
            const auto validation = validateReloadOutputAny(
                ownerToken,
                kReloadAnimationReadCapabilities,
                outState);
            return validation == PaperResultV1::Ok ?
                animation_evidence::getCatalogState(*outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL getReloadAnimationLiveStateV1(
            const std::uint64_t ownerToken,
            PaperReloadAnimationLiveStateV1* outState)
        {
            const auto validation = validateReloadOutputAny(
                ownerToken,
                kReloadAnimationReadCapabilities,
                outState);
            return validation == PaperResultV1::Ok ?
                animation_evidence::getLiveState(*outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadAnimationClipsV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstClip,
            PaperReloadAnimationClipV1* outClips,
            const std::uint32_t maxClips,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == PaperResultV1::Ok ?
                animation_evidence::copyClips(
                    catalogSequence,
                    firstClip,
                    outClips,
                    maxClips,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadAnimationTracksV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t firstTrack,
            PaperReloadAnimationTrackV1* outTracks,
            const std::uint32_t maxTracks,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == PaperResultV1::Ok ?
                animation_evidence::copyTracks(
                    catalogSequence,
                    clipId,
                    firstTrack,
                    outTracks,
                    maxTracks,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadAnimationSamplesV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t trackId,
            const std::uint32_t firstSample,
            PaperReloadAnimationSampleV1* outSamples,
            const std::uint32_t maxSamples,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == PaperResultV1::Ok ?
                animation_evidence::copySamples(
                    catalogSequence,
                    clipId,
                    trackId,
                    firstSample,
                    outSamples,
                    maxSamples,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadAnimationAnnotationsV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t firstAnnotation,
            PaperReloadAnimationAnnotationV1* outAnnotations,
            const std::uint32_t maxAnnotations,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == PaperResultV1::Ok ?
                animation_evidence::copyAnnotations(
                    catalogSequence,
                    clipId,
                    firstAnnotation,
                    outAnnotations,
                    maxAnnotations,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadAnimationTriggersV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t firstTrigger,
            PaperReloadAnimationTriggerV1* outTriggers,
            const std::uint32_t maxTriggers,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == PaperResultV1::Ok ?
                animation_evidence::copyTriggers(
                    catalogSequence,
                    clipId,
                    firstTrigger,
                    outTriggers,
                    maxTriggers,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadAnimationSkeletonV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstBone,
            PaperReloadAnimationSkeletonBoneV1* outBones,
            const std::uint32_t maxBones,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == PaperResultV1::Ok ?
                animation_evidence::copySkeleton(
                    catalogSequence,
                    firstBone,
                    outBones,
                    maxBones,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL getReloadStageStateV1(
            const std::uint64_t ownerToken,
            PaperReloadStageStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::
                    ReloadStageIdentification,
                outState);
            return validation == PaperResultV1::Ok ?
                reload_stages::getState(*outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL copyReloadStagePartsV1(
            const std::uint64_t ownerToken,
            const std::uint64_t snapshotSequence,
            const std::uint32_t firstPart,
            PaperReloadStagePartV1* outParts,
            const std::uint32_t maxParts,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::
                    ReloadStageIdentification);
            return validation == PaperResultV1::Ok ?
                reload_stages::copyParts(
                    snapshotSequence,
                    firstPart,
                    outParts,
                    maxParts,
                    *outCopied) :
                validation;
        }

        PaperResultV1 PAPER_CALL getNativePoseFrameStateV1(
            const std::uint64_t ownerToken,
            PaperNativePoseFrameStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::NativePosePipeline,
                outState);
            if (validation != PaperResultV1::Ok) {
                return validation;
            }
            NativePosePipelineSnapshot snapshot{};
            if (!copySeqlocked(
                    s_nativePosePipeline,
                    s_nativePosePipelineSequence,
                    snapshot) ||
                snapshot.frame.snapshotSequence == 0) {
                return PaperResultV1::NotReady;
            }
            *outState = snapshot.frame;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL getNativeHandSolutionV1(
            const std::uint64_t ownerToken,
            const PaperHandV1 hand,
            PaperNativeHandSolutionV1* outSolution)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::NativePosePipeline,
                outSolution);
            if (validation != PaperResultV1::Ok) {
                return validation;
            }
            if (hand != PaperHandV1::Right && hand != PaperHandV1::Left) {
                return PaperResultV1::InvalidArgument;
            }
            NativePosePipelineSnapshot snapshot{};
            if (!copySeqlocked(
                    s_nativePosePipeline,
                    s_nativePosePipelineSequence,
                    snapshot) ||
                snapshot.frame.snapshotSequence == 0) {
                return PaperResultV1::NotReady;
            }
            *outSolution = hand == PaperHandV1::Left ?
                snapshot.leftHand :
                snapshot.rightHand;
            return PaperResultV1::Ok;
        }

        PaperResultV1 PAPER_CALL getWeaponMotionLimitsV1(
            const std::uint64_t ownerToken,
            PaperWeaponMotionLimitsV1* outLimits)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog,
                outLimits);
            return validation == PaperResultV1::Ok ?
                weapon_motion::getLimits(*outLimits) : validation;
        }

        PaperResultV1 PAPER_CALL getWeaponMotionCatalogStateV1(
            const std::uint64_t ownerToken,
            PaperWeaponMotionCatalogStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog,
                outState);
            return validation == PaperResultV1::Ok ?
                weapon_motion::getCatalogState(*outState) : validation;
        }

        PaperResultV1 PAPER_CALL copyWeaponMotionPartsV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstPart,
            PaperWeaponMotionPartV1* outParts,
            const std::uint32_t maxParts,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog);
            return validation == PaperResultV1::Ok ?
                weapon_motion::copyParts(
                    catalogSequence,
                    firstPart,
                    outParts,
                    maxParts,
                    *outCopied) : validation;
        }

        PaperResultV1 PAPER_CALL copyWeaponMotionStagesV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstStage,
            PaperWeaponMotionStageV1* outStages,
            const std::uint32_t maxStages,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog);
            return validation == PaperResultV1::Ok ?
                weapon_motion::copyStages(
                    catalogSequence,
                    firstStage,
                    outStages,
                    maxStages,
                    *outCopied) : validation;
        }

        PaperResultV1 PAPER_CALL copyWeaponMotionStageKeysV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t stageId,
            const std::uint32_t firstKey,
            PaperReloadQsTransformV1* outKeys,
            const std::uint32_t maxKeys,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog);
            return validation == PaperResultV1::Ok ?
                weapon_motion::copyStageKeys(
                    catalogSequence,
                    stageId,
                    firstKey,
                    outKeys,
                    maxKeys,
                    *outCopied) : validation;
        }

        PaperResultV1 PAPER_CALL copyWeaponMotionFollowersV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t stageId,
            const std::uint32_t firstFollower,
            PaperWeaponMotionFollowerV1* outFollowers,
            const std::uint32_t maxFollowers,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog);
            return validation == PaperResultV1::Ok ?
                weapon_motion::copyFollowers(
                    catalogSequence,
                    stageId,
                    firstFollower,
                    outFollowers,
                    maxFollowers,
                    *outCopied) : validation;
        }

        PaperResultV1 PAPER_CALL copyWeaponMotionFollowerKeysV1(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t stageId,
            const std::uint32_t followerIndex,
            const std::uint32_t firstKey,
            PaperReloadQsTransformV1* outKeys,
            const std::uint32_t maxKeys,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionCatalog);
            return validation == PaperResultV1::Ok ?
                weapon_motion::copyFollowerKeys(
                    catalogSequence,
                    stageId,
                    followerIndex,
                    firstKey,
                    outKeys,
                    maxKeys,
                    *outCopied) : validation;
        }

        PaperResultV1 PAPER_CALL getWeaponMotionLearningStateV1(
            const std::uint64_t ownerToken,
            PaperWeaponMotionLearningStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionDiagnostics,
                outState);
            return validation == PaperResultV1::Ok ?
                weapon_motion::getLearningState(*outState) : validation;
        }

        PaperResultV1 PAPER_CALL copyWeaponMotionRecordersV1(
            const std::uint64_t ownerToken,
            const std::uint64_t snapshotSequence,
            const std::uint32_t firstRecorder,
            PaperWeaponMotionRecorderV1* outRecorders,
            const std::uint32_t maxRecorders,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return PaperResultV1::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionDiagnostics);
            return validation == PaperResultV1::Ok ?
                weapon_motion::copyRecorders(
                    snapshotSequence,
                    firstRecorder,
                    outRecorders,
                    maxRecorders,
                    *outCopied) : validation;
        }

        PaperResultV1 PAPER_CALL getWeaponManipulationFrameStateV1(
            const std::uint64_t ownerToken,
            PaperWeaponManipulationFrameStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponManipulationTelemetry,
                outState);
            return validation == PaperResultV1::Ok ?
                weapon_motion::getManipulationFrameState(*outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL getWeaponManipulationHandStateV1(
            const std::uint64_t ownerToken,
            const PaperHandV1 hand,
            PaperWeaponManipulationHandStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponManipulationTelemetry,
                outState);
            return validation == PaperResultV1::Ok ?
                weapon_motion::getManipulationHandState(hand, *outState) :
                validation;
        }

        PaperResultV1 PAPER_CALL getWeaponMotionStoreStateV1(
            const std::uint64_t ownerToken,
            PaperWeaponMotionStoreStateV1* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                PaperConsumerCapabilityV1::WeaponMotionDiagnostics,
                outState);
            return validation == PaperResultV1::Ok ?
                weapon_motion::getStoreState(*outState) : validation;
        }

        const PaperProviderApiV1 s_api{
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
            &getReloadObservationLimitsV1,
            &getReloadCatalogStateV1,
            &copyReloadCatalogNodesV1,
            &copyReloadEvidenceV1,
            &copyReloadEvidencePointsV1,
            &getReloadFrameStateV1,
            &copyReloadNodeObservationsV1,
            &getReloadAnimationLimitsV1,
            &getReloadAnimationCatalogStateV1,
            &getReloadAnimationLiveStateV1,
            &copyReloadAnimationClipsV1,
            &copyReloadAnimationTracksV1,
            &copyReloadAnimationSamplesV1,
            &copyReloadAnimationAnnotationsV1,
            &copyReloadAnimationTriggersV1,
            &copyReloadAnimationSkeletonV1,
            &getReloadStageStateV1,
            &copyReloadStagePartsV1,
            &getNativePoseFrameStateV1,
            &getNativeHandSolutionV1,
            &getWeaponMotionLimitsV1,
            &getWeaponMotionCatalogStateV1,
            &copyWeaponMotionPartsV1,
            &copyWeaponMotionStagesV1,
            &copyWeaponMotionStageKeysV1,
            &copyWeaponMotionFollowersV1,
            &copyWeaponMotionFollowerKeysV1,
            &getWeaponMotionLearningStateV1,
            &copyWeaponMotionRecordersV1,
            &getWeaponManipulationFrameStateV1,
            &getWeaponManipulationHandStateV1,
            &getWeaponMotionStoreStateV1,
            &setDevelopmentCaptureV1,
            &clearDevelopmentCaptureV1,
            &getDevelopmentCaptureStateV1,
        };

        const PaperProviderDescriptorV1 s_descriptor{
            .api = std::addressof(s_api),
        };
    }

    void initialize()
    {
        s_ownerThread = GetCurrentThreadId();
        s_consumers = {};
        s_callbacks = {};
        s_currentFrame = 0;
        s_captureMode = PaperDevelopmentCaptureModeV1::User;
        s_cacheAccess = PaperWeaponMotionCacheAccessV1::Off;
        s_captureConfigFlags = 0;
        s_captureAllowedScopes = 0;
        s_captureAutoStartScopes = 0;
        s_captureLegacyScopes = 0;
        s_captureActiveScopes = 0;
        s_captureConfigRevision = 0;
        s_nativePosePipeline = {};
        s_nativePosePipelineSequence.store(0, std::memory_order_release);
        s_nativePosePublicationSequence = 0;
        if (++s_providerGeneration == 0) {
            s_providerGeneration = 1;
        }
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
        weapon_motion::reset(weapon_motion::ResetReason::RuntimeReset);
        s_ready.store(true, std::memory_order_release);
    }

    void shutdown()
    {
        s_ready.store(false, std::memory_order_release);
        s_consumers = {};
        s_callbacks = {};
        s_currentFrame = 0;
        s_captureAllowedScopes = 0;
        s_captureAutoStartScopes = 0;
        s_captureLegacyScopes = 0;
        s_captureActiveScopes = 0;
        s_nativePosePipeline = {};
        s_nativePosePipelineSequence.store(0, std::memory_order_release);
        s_nativePosePublicationSequence = 0;
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
        weapon_motion::reset(weapon_motion::ResetReason::ProviderShutdown);
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
            if (consumer.ownerToken == 0) {
                continue;
            }
            if (consumer.authorityFlags != 0 &&
                !consumer.persistentAuthority &&
                consumer.authorityUpdatedFrame != s_currentFrame) {
                if (consumer.remainingFrames > 0) {
                    --consumer.remainingFrames;
                }
                if (consumer.remainingFrames == 0) {
                    consumer.authorityFlags = 0;
                }
            }
            if (consumer.captureScopes != 0 &&
                consumer.captureUpdatedFrame != s_currentFrame) {
                if (consumer.captureRemainingFrames > 0) {
                    --consumer.captureRemainingFrames;
                }
                if (consumer.captureRemainingFrames == 0) {
                    consumer.captureScopes = 0;
                }
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
            consumer.captureScopes = 0;
            consumer.captureDeniedScopes = 0;
            consumer.captureRemainingFrames = 0;
        }
        PaperRuntimeStateV1 state{};
        state.paperProviderGeneration = s_providerGeneration;
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
        weapon_motion::reset(weapon_motion::ResetReason::RuntimeReset);
        s_captureLegacyScopes = 0;
        s_captureActiveScopes = 0;
        clearNativePosePipeline();
        publishRuntime(state);
        dispatchEvent(PaperEventKindV1::RuntimeReset);
    }

    void publishConfig(const PaperConfigStateV1& state)
    {
        publishSeqlocked(s_configState, s_configSequence, state);
    }

    void configureDevelopmentCapture(
        const PaperDevelopmentCaptureModeV1 mode,
        const PaperWeaponMotionCacheAccessV1 cacheAccess,
        const std::uint32_t configFlags,
        const std::uint64_t configRevision)
    {
        if (!onOwnerThread()) {
            return;
        }
        s_captureMode = development_capture_policy::validMode(mode) ?
            mode : PaperDevelopmentCaptureModeV1::User;
        s_cacheAccess = development_capture_policy::validCacheAccess(cacheAccess) ?
            cacheAccess : PaperWeaponMotionCacheAccessV1::Off;
        constexpr auto allConfigFlags =
            static_cast<std::uint32_t>(
                PaperDevelopmentCaptureConfigFlagV1::AutoStart) |
            static_cast<std::uint32_t>(
                PaperDevelopmentCaptureConfigFlagV1::AllowApiActivation) |
            static_cast<std::uint32_t>(
                PaperDevelopmentCaptureConfigFlagV1::HotReloadEnabled);
        s_captureConfigFlags = configFlags & allConfigFlags;
        s_captureConfigRevision = configRevision;
        s_captureAllowedScopes = development_capture_policy::allowedScopes(
            s_captureMode, s_cacheAccess);
        s_captureAutoStartScopes =
            development_capture_policy::autoStartScopes(
                s_captureMode,
                s_cacheAccess,
                hasCaptureConfigFlag(
                    PaperDevelopmentCaptureConfigFlagV1::AutoStart));
        s_captureLegacyScopes &= s_captureAllowedScopes;
        s_captureActiveScopes &= s_captureAllowedScopes;
        const bool apiAllowed = hasCaptureConfigFlag(
            PaperDevelopmentCaptureConfigFlagV1::AllowApiActivation);
        for (auto& consumer : s_consumers) {
            if (!apiAllowed ||
                (consumer.captureScopes & ~s_captureAllowedScopes) != 0) {
                consumer.captureScopes = 0;
                consumer.captureRemainingFrames = 0;
            }
        }
    }

    std::uint32_t refreshDevelopmentCaptureDemand(
        const std::uint32_t legacyRequestedScopes)
    {
        if (!onOwnerThread()) {
            return 0;
        }
        s_captureLegacyScopes = hasCaptureConfigFlag(
                PaperDevelopmentCaptureConfigFlagV1::AllowApiActivation) ?
            development_capture_policy::expandDependencies(
                legacyRequestedScopes) &
                s_captureAllowedScopes :
            0;
        return (s_captureAutoStartScopes | s_captureLegacyScopes |
                   aggregateCaptureScopes()) &
            s_captureAllowedScopes;
    }

    void publishDevelopmentCaptureScopes(const std::uint32_t activeScopes)
    {
        if (onOwnerThread()) {
            s_captureActiveScopes =
                activeScopes & s_captureAllowedScopes;
        }
    }

    std::uint32_t allowedDevelopmentCaptureScopes()
    {
        return onOwnerThread() ? s_captureAllowedScopes : 0;
    }

    void publishRuntime(const PaperRuntimeStateV1& state)
    {
        publishSeqlocked(s_runtimeState, s_runtimeSequence, state);
    }

    void publishNativePosePipeline(
        const PaperNativePoseFrameStateV1& frame,
        const PaperNativeHandSolutionV1& rightHand,
        const PaperNativeHandSolutionV1& leftHand)
    {
        if (!onOwnerThread()) {
            return;
        }
        if (++s_nativePosePublicationSequence == 0) {
            ++s_nativePosePublicationSequence;
        }
        NativePosePipelineSnapshot snapshot{
            .frame = frame,
            .rightHand = rightHand,
            .leftHand = leftHand,
        };
        snapshot.frame.snapshotSequence = s_nativePosePublicationSequence;
        snapshot.rightHand.snapshotSequence = s_nativePosePublicationSequence;
        snapshot.leftHand.snapshotSequence = s_nativePosePublicationSequence;
        publishSeqlocked(
            s_nativePosePipeline,
            s_nativePosePipelineSequence,
            snapshot);
    }

    void clearNativePosePipeline()
    {
        if (!onOwnerThread()) {
            return;
        }
        publishSeqlocked(
            s_nativePosePipeline,
            s_nativePosePipelineSequence,
            NativePosePipelineSnapshot{});
    }

    void dispatchEvent(const PaperEventV1& sourceEvent)
    {
        if (!onOwnerThread()) {
            return;
        }
        PaperRuntimeStateV1 runtime{};
        if (!copySeqlocked(s_runtimeState, s_runtimeSequence, runtime)) {
            return;
        }
        auto eventData = sourceEvent;
        eventData.runtime = runtime;

        for (auto& slot : s_callbacks) {
            if (slot.token == 0 || !slot.callback) {
                continue;
            }
            const auto ownerToken = slot.ownerToken;
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                continue;
            }
            const auto manipulationEvent =
                eventData.kind >=
                    PaperEventKindV1::WeaponManipulationStarted &&
                eventData.kind <=
                    PaperEventKindV1::WeaponManipulationEnded;
            if (manipulationEvent && !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::
                        WeaponManipulationTelemetry)) {
                continue;
            }
            if (eventData.kind ==
                    PaperEventKindV1::WeaponMotionCatalogChanged &&
                !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::WeaponMotionCatalog)) {
                continue;
            }
            if (eventData.kind ==
                    PaperEventKindV1::WeaponMotionCandidateCompleted &&
                !hasCapability(
                    *consumer,
                    PaperConsumerCapabilityV1::WeaponMotionDiagnostics)) {
                continue;
            }
            const auto callback = slot.callback;
            void* const userData = slot.userData;
#if defined(_MSC_VER)
            __try {
                callback(&eventData, userData);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                PAPER_LOG_ERROR(
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

    void dispatchEvent(const PaperEventKindV1 kind)
    {
        PaperEventV1 eventData{};
        eventData.kind = kind;
        dispatchEvent(eventData);
    }

    bool hasConsumerCapability(const PaperConsumerCapabilityV1 capability)
    {
        if (!onOwnerThread()) {
            return false;
        }
        const auto capabilityMask =
            static_cast<std::uint32_t>(capability) & kAllCapabilities;
        if (capabilityMask == 0) {
            return false;
        }
        for (const auto& consumer : s_consumers) {
            if (consumer.ownerToken != 0 &&
                (consumer.capabilities & capabilityMask) != 0) {
                return true;
            }
        }
        return false;
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

    const PaperProviderApiV1* apiTable()
    {
        return std::addressof(s_api);
    }

    const PaperProviderDescriptorV1* apiDescriptor()
    {
        return std::addressof(s_descriptor);
    }
}

extern "C" PAPER_API
    const paper::api::PaperProviderApiV1* PAPER_CALL
    PAPERAPI_GetProviderApi(const std::uint32_t requestedVersion)
{
    if (requestedVersion > paper::api::PAPER_API_VERSION) {
        return nullptr;
    }
    return paper::provider::apiTable();
}

extern "C" PAPER_API
    const paper::api::PaperProviderDescriptorV1* PAPER_CALL
    PAPERAPI_GetProviderDescriptorV1()
{
    return paper::provider::apiDescriptor();
}
