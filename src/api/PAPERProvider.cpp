#include "api/PAPERProvider.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation_evidence/AnimationEvidence.h"
#include "api/ApiTransform.h"
#include "development/DevelopmentCapturePolicy.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservation.h"
#include "reload_stages/ReloadStages.h"
#include "weapon_motion/WeaponMotion.h"
#include "reload_control/PhysicalReloadLease.h"
#include "reload_control/ManualReloadOnly.h"
#include "api/InterfaceNegotiation.h"
#include "support/Fo4VrRuntime.h"

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
            EventCallback callback{ nullptr };
            void* userData{ nullptr };
        };

        constexpr std::uint32_t kAllCapabilities =
            static_cast<std::uint32_t>(ConsumerCapability::All);
        constexpr std::uint32_t kReloadAnimationReadCapabilities =
            static_cast<std::uint32_t>(
                ConsumerCapability::ReloadAnimationEvidence) |
            static_cast<std::uint32_t>(
                ConsumerCapability::ReloadAnimationTelemetry);
        constexpr std::uint32_t kAllAuthorityFlags =
            static_cast<std::uint32_t>(
                NativeAnimationAuthorityFlag::ReloadPose);

        std::array<ConsumerSlot, kMaxConsumers> s_consumers{};
        std::array<CallbackSlot, kMaxCallbacks> s_callbacks{};
        std::atomic<std::uint64_t> s_nextToken{ 1 };
        std::atomic<bool> s_ready{ false };
        DWORD s_ownerThread{ 0 };
        std::uint64_t s_currentFrame{ 0 };
        std::uint32_t s_providerGeneration{ 0 };
        DevelopmentCaptureMode s_captureMode{
            DevelopmentCaptureMode::User
        };
        WeaponMotionCacheAccess s_cacheAccess{
            WeaponMotionCacheAccess::Off
        };
        std::uint32_t s_captureConfigFlags{ 0 };
        std::uint32_t s_captureAllowedScopes{ 0 };
        std::uint32_t s_captureAutoStartScopes{ 0 };
        std::uint32_t s_captureLegacyScopes{ 0 };
        std::uint32_t s_captureActiveScopes{ 0 };
        std::uint64_t s_captureConfigRevision{ 0 };

        RuntimeState s_runtimeState{};
        paper::physical_reload::Lease s_physicalReload;
        const void* s_physicalEquippedData{};
        const void* s_physicalInstance{};
        ConfigState s_configState{};
        std::atomic<std::uint64_t> s_runtimeSequence{ 0 };
        std::atomic<std::uint64_t> s_configSequence{ 0 };

        struct NativePosePipelineSnapshot
        {
            NativePoseFrameState frame{};
            NativeHandSolution rightHand{};
            NativeHandSolution leftHand{
                .hand = Hand::Left,
                .role = NativeHandRole::Support,
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
            const ConsumerCapability capability)
        {
            return (consumer.capabilities &
                       static_cast<std::uint32_t>(capability)) != 0;
        }

        [[nodiscard]] bool hasCaptureConfigFlag(
            const DevelopmentCaptureConfigFlag flag)
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
            if(s_physicalReload.owner==ownerToken)s_physicalReload.clear();
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

        const char* PAPER_CALL getModVersion()
        {
            // Version::NAME views a string literal, so data() is terminated.
            return Version::NAME.data();
        }

        bool PAPER_CALL apiIsReady()
        {
            return isReady();
        }

        bool PAPER_CALL getRuntimeState(
            const std::uint64_t ownerToken,
            RuntimeState* outState)
        {
            if (!outState ||
                outState->size < sizeof(RuntimeState) ||
                outState->version != kRecordVersion ||
                !onOwnerThread()) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ConsumerCapability::RuntimeState)) {
                return false;
            }
            RuntimeState state{};
            if (!copySeqlocked(s_runtimeState, s_runtimeSequence, state)) {
                *outState = {};
                return false;
            }
            *outState = state;
            return true;
        }

        bool PAPER_CALL getConfigState(
            const std::uint64_t ownerToken,
            ConfigState* outState)
        {
            if (!outState ||
                outState->size < sizeof(ConfigState) ||
                outState->version != kRecordVersion ||
                !onOwnerThread()) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ConsumerCapability::RuntimeState)) {
                return false;
            }
            ConfigState state{};
            if (!copySeqlocked(s_configState, s_configSequence, state)) {
                *outState = {};
                return false;
            }
            *outState = state;
            return true;
        }

        std::uint32_t PAPER_CALL getCapturedTransformCount(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ConsumerCapability::CapturedTransforms)) {
                return 0;
            }
            RuntimeState state{};
            return copySeqlocked(s_runtimeState, s_runtimeSequence, state) ?
                state.capturedTransformCount :
                0;
        }

        std::uint32_t PAPER_CALL copyCapturedTransforms(
            const std::uint64_t ownerToken,
            CapturedTransform* outTransforms,
            const std::uint32_t maxTransforms)
        {
            if (!outTransforms || maxTransforms == 0 || !onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ConsumerCapability::CapturedTransforms)) {
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

        bool PAPER_CALL getNativeHandPose(
            const std::uint64_t ownerToken,
            const Hand hand,
            NativeHandPose* outPose)
        {
            if (!outPose ||
                outPose->size < sizeof(NativeHandPose) ||
                outPose->version != kRecordVersion ||
                !onOwnerThread() ||
                (hand != Hand::Right &&
                    hand != Hand::Left)) {
                return false;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer || !hasCapability(
                    *consumer,
                    ConsumerCapability::NativeHandPose)) {
                return false;
            }

            native_animation_authority::NativeHandPose source{};
            if (!native_animation_authority::queryNativeHandPose(
                    hand == Hand::Left,
                    source)) {
                *outPose = {};
                return false;
            }

            NativeHandPose result{};
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

        Status PAPER_CALL registerConsumer(
            const ConsumerRegistration* registration,
            ConsumerHandle* outHandle)
        {
            if (!registration || !outHandle) {
                return Status::InvalidArgument;
            }
            if (registration->size <
                    sizeof(ConsumerRegistration) ||
                outHandle->size < sizeof(ConsumerHandle)) {
                return Status::InvalidSize;
            }
            *outHandle = {};
            if (registration->version != kRecordVersion) {
                return Status::UnsupportedVersion;
            }
            if (!isReady()) {
                return Status::NotReady;
            }
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }

            const auto* modNameEnd = static_cast<const char*>(std::memchr(
                registration->modName,
                '\0',
                sizeof(registration->modName)));
            if (!modNameEnd || modNameEnd == registration->modName) {
                return Status::InvalidArgument;
            }
            for (const auto& consumer : s_consumers) {
                if (consumer.ownerToken != 0 &&
                    std::strcmp(consumer.modName, registration->modName) == 0) {
                    return Status::OwnerConflict;
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

                ConsumerHandle handle{};
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
                return Status::Ok;
            }
            return Status::CapacityReached;
        }

        Status PAPER_CALL unregisterConsumer(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            if (!findConsumer(ownerToken)) {
                return Status::UnknownOwner;
            }
            removeConsumer(ownerToken);
            return Status::Ok;
        }

        std::uint32_t PAPER_CALL getGrantedCapabilities(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return 0;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            return consumer ? consumer->capabilities : 0;
        }

        Status PAPER_CALL setAnimationAuthority(
            const std::uint64_t ownerToken,
            const AuthorityRequest* request)
        {
            if (!request ||
                request->size < sizeof(AuthorityRequest)) {
                return Status::InvalidArgument;
            }
            if (request->version != kRecordVersion) {
                return Status::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ConsumerCapability::AnimationAuthority)) {
                return Status::PermissionDenied;
            }
            if ((request->flags & ~kAllAuthorityFlags) != 0 ||
                request->leaseFrames >
                    kMaxAuthorityLeaseFrames) {
                return Status::InvalidArgument;
            }

            consumer->authorityFlags = request->flags;
            consumer->remainingFrames = request->leaseFrames;
            consumer->persistentAuthority = request->leaseFrames == 0;
            consumer->authorityUpdatedFrame = s_currentFrame;
            return Status::Ok;
        }

        Status PAPER_CALL clearAnimationAuthority(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ConsumerCapability::AnimationAuthority)) {
                return Status::PermissionDenied;
            }
            consumer->authorityFlags = 0;
            consumer->remainingFrames = 0;
            consumer->persistentAuthority = false;
            return Status::Ok;
        }

        Status PAPER_CALL setDevelopmentCapture(
            const std::uint64_t ownerToken,
            const DevelopmentCaptureRequest* request)
        {
            if (!request) {
                return Status::InvalidArgument;
            }
            if (request->size < sizeof(DevelopmentCaptureRequest)) {
                return Status::InvalidSize;
            }
            if (request->version != kRecordVersion) {
                return Status::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ConsumerCapability::DevelopmentCaptureControl)) {
                return Status::PermissionDenied;
            }
            if (request->scopes == 0 ||
                (request->scopes & ~development_capture_policy::kAllScopes) != 0 ||
                request->leaseFrames == 0 ||
                request->leaseFrames >
                    kMaxDevelopmentCaptureLeaseFrames) {
                return Status::InvalidArgument;
            }

            const auto requested =
                development_capture_policy::expandDependencies(request->scopes);
            const auto denied = requested & ~s_captureAllowedScopes;
            if (!hasCaptureConfigFlag(
                    DevelopmentCaptureConfigFlag::
                        AllowApiActivation) ||
                denied != 0) {
                consumer->captureDeniedScopes = hasCaptureConfigFlag(
                        DevelopmentCaptureConfigFlag::
                            AllowApiActivation) ?
                    denied : requested;
                return Status::PermissionDenied;
            }

            consumer->captureScopes = requested;
            consumer->captureDeniedScopes = 0;
            consumer->captureRemainingFrames = request->leaseFrames;
            consumer->captureUpdatedFrame = s_currentFrame;
            return Status::Ok;
        }

        Status PAPER_CALL clearDevelopmentCapture(
            const std::uint64_t ownerToken)
        {
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            auto* consumer = findConsumer(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ConsumerCapability::DevelopmentCaptureControl)) {
                return Status::PermissionDenied;
            }
            consumer->captureScopes = 0;
            consumer->captureDeniedScopes = 0;
            consumer->captureRemainingFrames = 0;
            return Status::Ok;
        }

        Status PAPER_CALL getDevelopmentCaptureState(
            const std::uint64_t ownerToken,
            DevelopmentCaptureState* outState)
        {
            if (!outState) {
                return Status::InvalidArgument;
            }
            if (outState->size < sizeof(DevelopmentCaptureState)) {
                return Status::InvalidSize;
            }
            if (outState->version != kRecordVersion) {
                return Status::UnsupportedVersion;
            }
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ConsumerCapability::DevelopmentCaptureControl)) {
                return Status::PermissionDenied;
            }
            DevelopmentCaptureState state{};
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
                    DevelopmentCaptureStateFlag::AutoStartActive);
            }
            if (hasCaptureConfigFlag(
                    DevelopmentCaptureConfigFlag::
                        AllowApiActivation)) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    DevelopmentCaptureStateFlag::ApiActivationAllowed);
            }
            if (consumer->captureScopes != 0) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    DevelopmentCaptureStateFlag::OwnerLeaseActive);
            }
            if (state.aggregateRequestedScopes != 0) {
                state.stateFlags |= static_cast<std::uint32_t>(
                    DevelopmentCaptureStateFlag::AggregateLeaseActive);
            }

            ReloadAnimationCatalogState animation{};
            if (animation_evidence::getCatalogState(animation) ==
                Status::Ok) {
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
                    ReloadAnimationPreharvestState::Completed) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        DevelopmentCaptureStateFlag::HarvestCompleted);
                } else if (animation.exactPreharvestState ==
                           ReloadAnimationPreharvestState::Failed) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        DevelopmentCaptureStateFlag::HarvestFailed);
                }
            }

            WeaponMotionStoreState store{};
            if (weapon_motion::getStoreState(store) == Status::Ok) {
                state.motionStoreFlags = store.flags;
                state.motionPartCount = store.inMemoryPartCount;
                state.motionStageCount = store.inMemoryStageCount;
                state.persistentRecordCount = store.persistentRecordCount;
                state.pendingWriteCount = store.pendingWriteCount;
                const auto hasStoreFlag = [&store](
                    const WeaponMotionStoreFlag flag) {
                    return (store.flags & static_cast<std::uint32_t>(flag)) != 0;
                };
                if (hasStoreFlag(
                        WeaponMotionStoreFlag::CacheLookupPending)) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        DevelopmentCaptureStateFlag::
                            CacheLookupPending);
                }
                if (hasStoreFlag(
                        WeaponMotionStoreFlag::SessionCacheHit)) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        DevelopmentCaptureStateFlag::SessionCacheHit);
                }
                if (hasStoreFlag(
                        WeaponMotionStoreFlag::PersistentCacheHit)) {
                    state.stateFlags |= static_cast<std::uint32_t>(
                        DevelopmentCaptureStateFlag::
                            PersistentCacheHit);
                }
            }
            *outState = state;
            return Status::Ok;
        }

        Status PAPER_CALL registerEventCallback(
            const std::uint64_t ownerToken,
            const EventCallback callback,
            void* userData,
            std::uint64_t* outCallbackToken)
        {
            if (!callback || !outCallbackToken) {
                return Status::InvalidArgument;
            }
            *outCallbackToken = 0;
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            if (!hasCapability(
                    *consumer,
                    ConsumerCapability::FrameCallbacks)) {
                return Status::PermissionDenied;
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
                return Status::Ok;
            }
            return Status::CapacityReached;
        }

        Status PAPER_CALL unregisterEventCallback(
            const std::uint64_t ownerToken,
            const std::uint64_t callbackToken)
        {
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            if (!findConsumer(ownerToken)) {
                return Status::UnknownOwner;
            }
            for (auto& slot : s_callbacks) {
                if (slot.token == callbackToken &&
                    slot.ownerToken == ownerToken) {
                    slot = {};
                    return Status::Ok;
                }
            }
            return Status::NotFound;
        }

        [[nodiscard]] Status validateReloadConsumerAny(
            const std::uint64_t ownerToken,
            const std::uint32_t capabilityMask)
        {
            if (!isReady()) {
                return Status::NotReady;
            }
            if (!onOwnerThread()) {
                return Status::WrongThread;
            }
            const auto* consumer = findConsumerConst(ownerToken);
            if (!consumer) {
                return Status::UnknownOwner;
            }
            return (consumer->capabilities & capabilityMask) != 0 ?
                Status::Ok :
                Status::PermissionDenied;
        }

        [[nodiscard]] Status validateReloadConsumer(
            const std::uint64_t ownerToken,
            const ConsumerCapability capability)
        {
            return validateReloadConsumerAny(
                ownerToken,
                static_cast<std::uint32_t>(capability));
        }

        template <class State>
        [[nodiscard]] Status validateReloadOutput(
            const std::uint64_t ownerToken,
            const ConsumerCapability capability,
            const State* output)
        {
            if (!output) {
                return Status::InvalidArgument;
            }
            if (output->size < sizeof(State)) {
                return Status::InvalidSize;
            }
            if (output->version != kRecordVersion) {
                return Status::UnsupportedVersion;
            }
            return validateReloadConsumer(ownerToken, capability);
        }

        template <class State>
        [[nodiscard]] Status validateReloadOutputAny(
            const std::uint64_t ownerToken,
            const std::uint32_t capabilityMask,
            const State* output)
        {
            if (!output) {
                return Status::InvalidArgument;
            }
            if (output->size < sizeof(State)) {
                return Status::InvalidSize;
            }
            if (output->version != kRecordVersion) {
                return Status::UnsupportedVersion;
            }
            return validateReloadConsumerAny(ownerToken, capabilityMask);
        }

        Status PAPER_CALL getReloadObservationLimits(
            const std::uint64_t ownerToken,
            ReloadObservationLimits* outLimits)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::ReloadObservations,
                outLimits);
            return validation == Status::Ok ?
                reload_observation::getLimits(*outLimits) :
                validation;
        }

        Status PAPER_CALL getReloadCatalogState(
            const std::uint64_t ownerToken,
            ReloadCatalogState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::ReloadObservations,
                outState);
            return validation == Status::Ok ?
                reload_observation::getCatalogState(*outState) :
                validation;
        }

        Status PAPER_CALL copyReloadCatalogNodes(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstNode,
            ReloadNodeCatalogEntry* outNodes,
            const std::uint32_t maxNodes,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::ReloadObservations);
            return validation == Status::Ok ?
                reload_observation::copyCatalogNodes(
                    catalogSequence,
                    firstNode,
                    outNodes,
                    maxNodes,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL copyReloadEvidence(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstEvidence,
            ReloadEvidence* outEvidence,
            const std::uint32_t maxEvidence,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::ReloadObservations);
            return validation == Status::Ok ?
                reload_observation::copyEvidence(
                    catalogSequence,
                    firstEvidence,
                    outEvidence,
                    maxEvidence,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL copyReloadEvidencePoints(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t evidenceId,
            const std::uint32_t firstPoint,
            Point3* outPoints,
            const std::uint32_t maxPoints,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::ReloadEvidenceGeometry);
            return validation == Status::Ok ?
                reload_observation::copyEvidencePoints(
                    catalogSequence,
                    evidenceId,
                    firstPoint,
                    outPoints,
                    maxPoints,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL getReloadFrameState(
            const std::uint64_t ownerToken,
            ReloadFrameState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::ReloadObservations,
                outState);
            return validation == Status::Ok ?
                reload_observation::getFrameState(*outState) :
                validation;
        }

        Status PAPER_CALL copyReloadNodeObservations(
            const std::uint64_t ownerToken,
            const std::uint64_t snapshotSequence,
            const std::uint32_t firstObservation,
            ReloadNodeObservation* outObservations,
            const std::uint32_t maxObservations,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::ReloadObservations);
            return validation == Status::Ok ?
                reload_observation::copyNodeObservations(
                    snapshotSequence,
                    firstObservation,
                    outObservations,
                    maxObservations,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL getReloadAnimationLimits(
            const std::uint64_t ownerToken,
            ReloadAnimationLimits* outLimits)
        {
            const auto validation = validateReloadOutputAny(
                ownerToken,
                kReloadAnimationReadCapabilities,
                outLimits);
            return validation == Status::Ok ?
                animation_evidence::getLimits(*outLimits) :
                validation;
        }

        Status PAPER_CALL getReloadAnimationCatalogState(
            const std::uint64_t ownerToken,
            ReloadAnimationCatalogState* outState)
        {
            const auto validation = validateReloadOutputAny(
                ownerToken,
                kReloadAnimationReadCapabilities,
                outState);
            return validation == Status::Ok ?
                animation_evidence::getCatalogState(*outState) :
                validation;
        }

        Status PAPER_CALL getReloadAnimationLiveState(
            const std::uint64_t ownerToken,
            ReloadAnimationLiveState* outState)
        {
            const auto validation = validateReloadOutputAny(
                ownerToken,
                kReloadAnimationReadCapabilities,
                outState);
            return validation == Status::Ok ?
                animation_evidence::getLiveState(*outState) :
                validation;
        }

        Status PAPER_CALL copyReloadAnimationClips(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstClip,
            ReloadAnimationClip* outClips,
            const std::uint32_t maxClips,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == Status::Ok ?
                animation_evidence::copyClips(
                    catalogSequence,
                    firstClip,
                    outClips,
                    maxClips,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL copyReloadAnimationTracks(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t firstTrack,
            ReloadAnimationTrack* outTracks,
            const std::uint32_t maxTracks,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == Status::Ok ?
                animation_evidence::copyTracks(
                    catalogSequence,
                    clipId,
                    firstTrack,
                    outTracks,
                    maxTracks,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL copyReloadAnimationSamples(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t trackId,
            const std::uint32_t firstSample,
            ReloadAnimationSample* outSamples,
            const std::uint32_t maxSamples,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == Status::Ok ?
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

        Status PAPER_CALL copyReloadAnimationAnnotations(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t firstAnnotation,
            ReloadAnimationAnnotation* outAnnotations,
            const std::uint32_t maxAnnotations,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == Status::Ok ?
                animation_evidence::copyAnnotations(
                    catalogSequence,
                    clipId,
                    firstAnnotation,
                    outAnnotations,
                    maxAnnotations,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL copyReloadAnimationTriggers(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t clipId,
            const std::uint32_t firstTrigger,
            ReloadAnimationTrigger* outTriggers,
            const std::uint32_t maxTriggers,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == Status::Ok ?
                animation_evidence::copyTriggers(
                    catalogSequence,
                    clipId,
                    firstTrigger,
                    outTriggers,
                    maxTriggers,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL copyReloadAnimationSkeleton(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstBone,
            ReloadAnimationSkeletonBone* outBones,
            const std::uint32_t maxBones,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumerAny(
                ownerToken,
                kReloadAnimationReadCapabilities);
            return validation == Status::Ok ?
                animation_evidence::copySkeleton(
                    catalogSequence,
                    firstBone,
                    outBones,
                    maxBones,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL getReloadStageState(
            const std::uint64_t ownerToken,
            ReloadStageState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::
                    ReloadStageIdentification,
                outState);
            return validation == Status::Ok ?
                reload_stages::getState(*outState) :
                validation;
        }

        Status PAPER_CALL copyReloadStageParts(
            const std::uint64_t ownerToken,
            const std::uint64_t snapshotSequence,
            const std::uint32_t firstPart,
            ReloadStagePart* outParts,
            const std::uint32_t maxParts,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::
                    ReloadStageIdentification);
            return validation == Status::Ok ?
                reload_stages::copyParts(
                    snapshotSequence,
                    firstPart,
                    outParts,
                    maxParts,
                    *outCopied) :
                validation;
        }

        Status PAPER_CALL getNativePoseFrameState(
            const std::uint64_t ownerToken,
            NativePoseFrameState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::NativePosePipeline,
                outState);
            if (validation != Status::Ok) {
                return validation;
            }
            NativePosePipelineSnapshot snapshot{};
            if (!copySeqlocked(
                    s_nativePosePipeline,
                    s_nativePosePipelineSequence,
                    snapshot) ||
                snapshot.frame.snapshotSequence == 0) {
                return Status::NotReady;
            }
            *outState = snapshot.frame;
            return Status::Ok;
        }

        Status PAPER_CALL getNativeHandSolution(
            const std::uint64_t ownerToken,
            const Hand hand,
            NativeHandSolution* outSolution)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::NativePosePipeline,
                outSolution);
            if (validation != Status::Ok) {
                return validation;
            }
            if (hand != Hand::Right && hand != Hand::Left) {
                return Status::InvalidArgument;
            }
            NativePosePipelineSnapshot snapshot{};
            if (!copySeqlocked(
                    s_nativePosePipeline,
                    s_nativePosePipelineSequence,
                    snapshot) ||
                snapshot.frame.snapshotSequence == 0) {
                return Status::NotReady;
            }
            *outSolution = hand == Hand::Left ?
                snapshot.leftHand :
                snapshot.rightHand;
            return Status::Ok;
        }

        Status PAPER_CALL getWeaponMotionLimits(
            const std::uint64_t ownerToken,
            WeaponMotionLimits* outLimits)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog,
                outLimits);
            return validation == Status::Ok ?
                weapon_motion::getLimits(*outLimits) : validation;
        }

        Status PAPER_CALL getWeaponMotionCatalogState(
            const std::uint64_t ownerToken,
            WeaponMotionCatalogState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog,
                outState);
            return validation == Status::Ok ?
                weapon_motion::getCatalogState(*outState) : validation;
        }

        Status PAPER_CALL copyWeaponMotionParts(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstPart,
            WeaponMotionPart* outParts,
            const std::uint32_t maxParts,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog);
            return validation == Status::Ok ?
                weapon_motion::copyParts(
                    catalogSequence,
                    firstPart,
                    outParts,
                    maxParts,
                    *outCopied) : validation;
        }

        Status PAPER_CALL copyWeaponMotionStages(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t firstStage,
            WeaponMotionStage* outStages,
            const std::uint32_t maxStages,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog);
            return validation == Status::Ok ?
                weapon_motion::copyStages(
                    catalogSequence,
                    firstStage,
                    outStages,
                    maxStages,
                    *outCopied) : validation;
        }

        Status PAPER_CALL copyWeaponMotionStageKeys(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t stageId,
            const std::uint32_t firstKey,
            ReloadQsTransform* outKeys,
            const std::uint32_t maxKeys,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog);
            return validation == Status::Ok ?
                weapon_motion::copyStageKeys(
                    catalogSequence,
                    stageId,
                    firstKey,
                    outKeys,
                    maxKeys,
                    *outCopied) : validation;
        }

        Status PAPER_CALL copyWeaponMotionFollowers(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t stageId,
            const std::uint32_t firstFollower,
            WeaponMotionFollower* outFollowers,
            const std::uint32_t maxFollowers,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog);
            return validation == Status::Ok ?
                weapon_motion::copyFollowers(
                    catalogSequence,
                    stageId,
                    firstFollower,
                    outFollowers,
                    maxFollowers,
                    *outCopied) : validation;
        }

        Status PAPER_CALL copyWeaponMotionFollowerKeys(
            const std::uint64_t ownerToken,
            const std::uint64_t catalogSequence,
            const std::uint32_t stageId,
            const std::uint32_t followerIndex,
            const std::uint32_t firstKey,
            ReloadQsTransform* outKeys,
            const std::uint32_t maxKeys,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::WeaponMotionCatalog);
            return validation == Status::Ok ?
                weapon_motion::copyFollowerKeys(
                    catalogSequence,
                    stageId,
                    followerIndex,
                    firstKey,
                    outKeys,
                    maxKeys,
                    *outCopied) : validation;
        }

        Status PAPER_CALL getWeaponMotionLearningState(
            const std::uint64_t ownerToken,
            WeaponMotionLearningState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::WeaponMotionDiagnostics,
                outState);
            return validation == Status::Ok ?
                weapon_motion::getLearningState(*outState) : validation;
        }

        Status PAPER_CALL copyWeaponMotionRecorders(
            const std::uint64_t ownerToken,
            const std::uint64_t snapshotSequence,
            const std::uint32_t firstRecorder,
            WeaponMotionRecorder* outRecorders,
            const std::uint32_t maxRecorders,
            std::uint32_t* outCopied)
        {
            if (!outCopied) {
                return Status::InvalidArgument;
            }
            *outCopied = 0;
            const auto validation = validateReloadConsumer(
                ownerToken,
                ConsumerCapability::WeaponMotionDiagnostics);
            return validation == Status::Ok ?
                weapon_motion::copyRecorders(
                    snapshotSequence,
                    firstRecorder,
                    outRecorders,
                    maxRecorders,
                    *outCopied) : validation;
        }

        Status PAPER_CALL getWeaponManipulationFrameState(
            const std::uint64_t ownerToken,
            WeaponManipulationFrameState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::WeaponManipulationTelemetry,
                outState);
            return validation == Status::Ok ?
                weapon_motion::getManipulationFrameState(*outState) :
                validation;
        }

        Status PAPER_CALL getWeaponManipulationHandState(
            const std::uint64_t ownerToken,
            const Hand hand,
            WeaponManipulationHandState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::WeaponManipulationTelemetry,
                outState);
            return validation == Status::Ok ?
                weapon_motion::getManipulationHandState(hand, *outState) :
                validation;
        }

        Status PAPER_CALL getWeaponMotionStoreState(
            const std::uint64_t ownerToken,
            WeaponMotionStoreState* outState)
        {
            const auto validation = validateReloadOutput(
                ownerToken,
                ConsumerCapability::WeaponMotionDiagnostics,
                outState);
            return validation == Status::Ok ?
                weapon_motion::getStoreState(*outState) : validation;
        }

        const api::provider::Api s_api{
            &getModVersion,
            &apiIsReady,
            &getRuntimeState,
            &getConfigState,
            &getCapturedTransformCount,
            &copyCapturedTransforms,
            &getNativeHandPose,
            &registerConsumer,
            &unregisterConsumer,
            &getGrantedCapabilities,
            &setAnimationAuthority,
            &clearAnimationAuthority,
            &registerEventCallback,
            &unregisterEventCallback,
            &getReloadObservationLimits,
            &getReloadCatalogState,
            &copyReloadCatalogNodes,
            &copyReloadEvidence,
            &copyReloadEvidencePoints,
            &getReloadFrameState,
            &copyReloadNodeObservations,
            &getReloadAnimationLimits,
            &getReloadAnimationCatalogState,
            &getReloadAnimationLiveState,
            &copyReloadAnimationClips,
            &copyReloadAnimationTracks,
            &copyReloadAnimationSamples,
            &copyReloadAnimationAnnotations,
            &copyReloadAnimationTriggers,
            &copyReloadAnimationSkeleton,
            &getReloadStageState,
            &copyReloadStageParts,
            &getNativePoseFrameState,
            &getNativeHandSolution,
            &getWeaponMotionLimits,
            &getWeaponMotionCatalogState,
            &copyWeaponMotionParts,
            &copyWeaponMotionStages,
            &copyWeaponMotionStageKeys,
            &copyWeaponMotionFollowers,
            &copyWeaponMotionFollowerKeys,
            &getWeaponMotionLearningState,
            &copyWeaponMotionRecorders,
            &getWeaponManipulationFrameState,
            &getWeaponManipulationHandState,
            &getWeaponMotionStoreState,
            &setDevelopmentCapture,
            &clearDevelopmentCapture,
            &getDevelopmentCaptureState,
        };
    }

    void initialize()
    {
        s_physicalReload.clear();s_physicalEquippedData=nullptr;s_physicalInstance=nullptr;
        s_ownerThread = GetCurrentThreadId();
        s_consumers = {};
        s_callbacks = {};
        s_currentFrame = 0;
        s_captureMode = DevelopmentCaptureMode::User;
        s_cacheAccess = WeaponMotionCacheAccess::Off;
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
        s_physicalReload.clear();s_physicalEquippedData=nullptr;s_physicalInstance=nullptr;
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
        s_physicalReload.clear();s_physicalEquippedData=nullptr;s_physicalInstance=nullptr;
        for (auto& consumer : s_consumers) {
            consumer.authorityFlags = 0;
            consumer.remainingFrames = 0;
            consumer.persistentAuthority = false;
            consumer.captureScopes = 0;
            consumer.captureDeniedScopes = 0;
            consumer.captureRemainingFrames = 0;
        }
        RuntimeState state{};
        state.paperProviderGeneration = s_providerGeneration;
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
        weapon_motion::reset(weapon_motion::ResetReason::RuntimeReset);
        s_captureLegacyScopes = 0;
        s_captureActiveScopes = 0;
        clearNativePosePipeline();
        publishRuntime(state);
        dispatchEvent(EventKind::RuntimeReset);
    }

    void publishConfig(const ConfigState& state)
    {
        publishSeqlocked(s_configState, s_configSequence, state);
    }

    void configureDevelopmentCapture(
        const DevelopmentCaptureMode mode,
        const WeaponMotionCacheAccess cacheAccess,
        const std::uint32_t configFlags,
        const std::uint64_t configRevision)
    {
        if (!onOwnerThread()) {
            return;
        }
        s_captureMode = development_capture_policy::validMode(mode) ?
            mode : DevelopmentCaptureMode::User;
        s_cacheAccess = development_capture_policy::validCacheAccess(cacheAccess) ?
            cacheAccess : WeaponMotionCacheAccess::Off;
        constexpr auto allConfigFlags =
            static_cast<std::uint32_t>(
                DevelopmentCaptureConfigFlag::AutoStart) |
            static_cast<std::uint32_t>(
                DevelopmentCaptureConfigFlag::AllowApiActivation) |
            static_cast<std::uint32_t>(
                DevelopmentCaptureConfigFlag::HotReloadEnabled);
        s_captureConfigFlags = configFlags & allConfigFlags;
        s_captureConfigRevision = configRevision;
        s_captureAllowedScopes = development_capture_policy::allowedScopes(
            s_captureMode, s_cacheAccess);
        s_captureAutoStartScopes =
            development_capture_policy::autoStartScopes(
                s_captureMode,
                s_cacheAccess,
                hasCaptureConfigFlag(
                    DevelopmentCaptureConfigFlag::AutoStart));
        s_captureLegacyScopes &= s_captureAllowedScopes;
        s_captureActiveScopes &= s_captureAllowedScopes;
        const bool apiAllowed = hasCaptureConfigFlag(
            DevelopmentCaptureConfigFlag::AllowApiActivation);
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
                DevelopmentCaptureConfigFlag::AllowApiActivation) ?
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

    void publishRuntime(const RuntimeState& state)
    {
        publishSeqlocked(s_runtimeState, s_runtimeSequence, state);
    }

    void publishNativePosePipeline(
        const NativePoseFrameState& frame,
        const NativeHandSolution& rightHand,
        const NativeHandSolution& leftHand)
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

    void dispatchEvent(const Event& sourceEvent)
    {
        if (!onOwnerThread()) {
            return;
        }
        RuntimeState runtime{};
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
            if (eventData.kind == EventKind::PresentationComplete &&
                !hasCapability(*consumer, ConsumerCapability::NativePosePipeline)) {
                continue;
            }
            const auto manipulationEvent =
                eventData.kind >=
                    EventKind::WeaponManipulationStarted &&
                eventData.kind <=
                    EventKind::WeaponManipulationEnded;
            if (manipulationEvent && !hasCapability(
                    *consumer,
                    ConsumerCapability::
                        WeaponManipulationTelemetry)) {
                continue;
            }
            if (eventData.kind ==
                    EventKind::WeaponMotionCatalogChanged &&
                !hasCapability(
                    *consumer,
                    ConsumerCapability::WeaponMotionCatalog)) {
                continue;
            }
            if (eventData.kind ==
                    EventKind::WeaponMotionCandidateCompleted &&
                !hasCapability(
                    *consumer,
                    ConsumerCapability::WeaponMotionDiagnostics)) {
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

    void dispatchEvent(const EventKind kind)
    {
        Event eventData{};
        eventData.kind = kind;
        dispatchEvent(eventData);
    }

    bool hasConsumerCapability(const ConsumerCapability capability)
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

    bool physicalReloadActive() noexcept
    {
        if(!onOwnerThread()||!s_physicalReload.owner)return false;
        if(!s_ready.load(std::memory_order_acquire)||s_currentFrame>s_physicalReload.expires||currentConsumerAuthorityFlags()!=0) {
            s_physicalReload.clear();s_physicalEquippedData=nullptr;s_physicalInstance=nullptr;
            return false;
        }
        const auto* item=f4vr::getEquippedItem();
        if(item&&item->item.object&&item->item.object->formID==s_physicalReload.request.weaponFormId&&
            item->data.get()==s_physicalEquippedData&&item->item.instanceData.get()==s_physicalInstance)return true;
        s_physicalReload.clear();s_physicalEquippedData=nullptr;s_physicalInstance=nullptr;
        return false;
    }

    bool physicalManualCycleActive() noexcept
    {
        return physicalReloadActive()&&s_physicalReload.manualCycle();
    }

    namespace
    {
        namespace contract = api::physical_reload;

        // The lease binds the exact native equipped item; no PAPER animation
        // snapshot or weapon-part readiness is required.
        const contract::Api s_physicalReloadApi{
            .acquire=[](api::OwnerToken owner,const contract::Request* request) noexcept -> Status {
                if(!onOwnerThread())return Status::WrongThread;
                if(!request)return Status::InvalidArgument;
                if(request->size!=sizeof(*request))return Status::InvalidSize;
                if(request->version!=kRecordVersion)return Status::UnsupportedVersion;
                if(request->flags&~contract::kKnownFlags)return Status::InvalidArgument;
                const auto* consumer=findConsumerConst(owner);
                if(!consumer)return Status::UnknownOwner;
                if(!hasCapability(*consumer,ConsumerCapability::AnimationAuthority))return Status::PermissionDenied;
                if(!request->weaponFormId||!request->leaseFrames||request->leaseFrames>contract::kMaxLeaseFrames)return Status::InvalidArgument;
                if(!s_ready.load(std::memory_order_acquire))return Status::NotReady;
                if(request->observedFrame>s_currentFrame+1||s_currentFrame>request->observedFrame+1)return Status::StaleSnapshot;
                if(currentConsumerAuthorityFlags()!=0)return Status::OwnerConflict;
                if(physicalReloadActive()&&s_physicalReload.owner!=owner)return Status::OwnerConflict;
                const auto* item=f4vr::getEquippedItem();
                if(!item||!item->data||!item->item.object||item->item.object->formID!=request->weaponFormId)return Status::StaleSnapshot;
                const auto result=s_physicalReload.set(owner,*request,s_currentFrame);
                if(result==Status::Ok){s_physicalEquippedData=item->data.get();s_physicalInstance=item->item.instanceData.get();}
                return result;
            },
            .clear=[](api::OwnerToken owner) noexcept -> Status {
                if(!onOwnerThread())return Status::WrongThread;
                if(!findConsumerConst(owner))return Status::UnknownOwner;
                if(s_physicalReload.owner&&s_physicalReload.owner!=owner&&physicalReloadActive())return Status::OwnerConflict;
                if(s_physicalReload.owner==owner)s_physicalReload.clear();
                return Status::Ok;
            },
            .read=[](api::OwnerToken owner,contract::State* output) noexcept -> Status {
                if(!onOwnerThread())return Status::WrongThread;
                if(!output)return Status::InvalidArgument;
                if(output->size!=sizeof(*output))return Status::InvalidSize;
                if(output->version!=kRecordVersion)return Status::UnsupportedVersion;
                *output={};const auto* consumer=findConsumerConst(owner);
                if(!consumer)return Status::UnknownOwner;
                if(!hasCapability(*consumer,ConsumerCapability::RuntimeState))return Status::PermissionDenied;
                if(physicalReloadActive()) {
                    output->active=1;output->owner=s_physicalReload.owner;output->weaponFormId=s_physicalReload.request.weaponFormId;
                    output->weaponGenerationKey=s_physicalReload.request.weaponGenerationKey;output->expiresAfterFrame=s_physicalReload.expires;
                    output->flags=s_physicalReload.request.flags;
                }
                return Status::Ok;
            }
        };

        const std::array s_interfaces{
            discovery::describe(s_api),
            discovery::describe(s_physicalReloadApi),
        };

        // An outdated consumer usually retries; each refused (interface,
        // major) pair and each retired export is logged once.
        std::array<std::atomic<std::uint32_t>,4> s_refusedMajors{};
        std::atomic<std::uint32_t> s_refusedExports{};

        void logRefusedMajor(const api::InterfaceId id,const std::uint32_t major) noexcept
        {
            const auto index=static_cast<std::uint32_t>(id);
            if(index>=s_refusedMajors.size()||major>=32)return;
            const auto bit=1u<<major;
            if(s_refusedMajors[index].fetch_or(bit,std::memory_order_relaxed)&bit)return;
            try {
                PAPER_LOG_WARN(Api,"Discovery refused interface {} major {}; this PAPER serves only its current majors, so the consumer must be updated",
                    index,major);
            } catch (...) {
            }
        }
    }

    api::Status queryInterface(const api::InterfaceId id,const std::uint32_t major,const std::uint32_t minor,
        const std::uint32_t bytes,const api::InterfaceDescriptorV1** output) noexcept
    {
        const auto status=discovery::query(s_interfaces,id,major,minor,bytes,output);
        if(status==Status::UnsupportedMajor)logRefusedMajor(id,major);
        return status;
    }

    void refuseRetiredExport(const std::uint32_t bit,const char* name) noexcept
    {
        if(s_refusedExports.fetch_or(bit,std::memory_order_relaxed)&bit)return;
        try {
            PAPER_LOG_WARN(Api,"Refused {}: it served PAPER's retired pre-2.0 API, so the calling mod must be updated to PAPERAPI_QueryInterfaceV1",name);
        } catch (...) {
        }
    }

    std::uint32_t generation()
    {
        return s_providerGeneration;
    }

    bool isReady()
    {
        return s_ready.load(std::memory_order_acquire);
    }
}

extern "C" paper::api::Status PAPER_CALL PAPERAPI_QueryInterfaceV1(const paper::api::InterfaceId id,
    const std::uint32_t exactMajor,const std::uint32_t minimumMinor,const std::uint32_t minimumTableBytes,
    const paper::api::InterfaceDescriptorV1** output) noexcept
{
    return paper::provider::queryInterface(id,exactMajor,minimumMinor,minimumTableBytes,output);
}

// Retired pre-2.0 discovery exports. They stay exported only so an outdated
// consumer is refused with a logged reason instead of a missing symbol; each
// returns no table. Their names are never reused.
extern "C" const void* PAPER_CALL PAPERAPI_GetProviderApi(std::uint32_t) noexcept
{
    paper::provider::refuseRetiredExport(1u<<0,"PAPERAPI_GetProviderApi");
    return nullptr;
}

extern "C" const void* PAPER_CALL PAPERAPI_GetProviderDescriptorV1() noexcept
{
    paper::provider::refuseRetiredExport(1u<<1,"PAPERAPI_GetProviderDescriptorV1");
    return nullptr;
}

extern "C" const void* PAPER_CALL PAPERAPI_QueryPhysicalReloadV1(std::uint32_t,std::uint32_t) noexcept
{
    paper::provider::refuseRetiredExport(1u<<2,"PAPERAPI_QueryPhysicalReloadV1");
    return nullptr;
}

extern "C" const void* PAPER_CALL PAPERAPI_QueryPhysicalReloadV2(std::uint32_t,std::uint32_t) noexcept
{
    paper::provider::refuseRetiredExport(1u<<3,"PAPERAPI_QueryPhysicalReloadV2");
    return nullptr;
}

extern "C" const void* PAPER_CALL PAPERAPI_QueryPhysicalReloadV3(std::uint32_t,std::uint32_t) noexcept
{
    paper::provider::refuseRetiredExport(1u<<4,"PAPERAPI_QueryPhysicalReloadV3");
    return nullptr;
}
