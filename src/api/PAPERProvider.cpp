#define PAPER_API_EXPORTS
#include "api/PAPERProvider.h"

#include "animation/NativeAnimationAuthority.h"
#include "animation_evidence/AnimationEvidence.h"
#include "api/ApiTransform.h"
#include "PaperLog.h"
#include "reload_observation/ReloadObservation.h"
#include "reload_stages/ReloadStages.h"

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

        PaperRuntimeStateV1 s_runtimeState{};
        PaperConfigStateV1 s_configState{};
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
            const PaperConsumerCapabilityV1 capability)
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
        if (++s_providerGeneration == 0) {
            s_providerGeneration = 1;
        }
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
        s_ready.store(true, std::memory_order_release);
    }

    void shutdown()
    {
        s_ready.store(false, std::memory_order_release);
        s_consumers = {};
        s_callbacks = {};
        s_currentFrame = 0;
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
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
        PaperRuntimeStateV1 state{};
        state.paperProviderGeneration = s_providerGeneration;
        reload_observation::reset();
        animation_evidence::reset();
        reload_stages::reset();
        publishRuntime(state);
        dispatchEvent(PaperEventKindV1::RuntimeReset);
    }

    void publishConfig(const PaperConfigStateV1& state)
    {
        publishSeqlocked(s_configState, s_configSequence, state);
    }

    void publishRuntime(const PaperRuntimeStateV1& state)
    {
        publishSeqlocked(s_runtimeState, s_runtimeSequence, state);
    }

    void dispatchEvent(const PaperEventKindV1 kind)
    {
        if (!onOwnerThread()) {
            return;
        }
        PaperRuntimeStateV1 runtime{};
        if (!copySeqlocked(s_runtimeState, s_runtimeSequence, runtime)) {
            return;
        }
        PaperEventV1 eventData{};
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
