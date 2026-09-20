#include "animation/NativeAnimationAuthority.h"

#include "api/RockApiClient.h"
#include "api/ApiTransform.h"
#include "animation/NativeAnimationAuthorityPolicy.h"
#include "animation_evidence/ClipTelemetry.h"
#include "compat/TacticalReloadBridge.h"
#include "native/NativeOffsets.h"
#include "PaperLog.h"
#include "reload_control/ManualReloadOnly.h"
#include "support/TransformMath.h"
#include "api/RockVisualAuthorityBridge.h"

#include "support/Fo4VrRuntime.h"

#include "RE/Bethesda/Actor.h"
#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Bethesda/TESBoundObjects.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <memory>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>

namespace paper::native_animation_authority
{
    namespace
    {
        using BoneTree = f4vr::BSFlattenedBoneTree;
        using BoneTransform = BoneTree::BoneTransforms;
        using WeaponFireHandlerFn = bool (*)(void* handler, RE::Actor* actor, RE::BSFixedString* eventData);
        using ReloadStateChangeHandlerFn = bool (*)(void* handler, RE::Actor* actor, RE::BSFixedString* stateToken);
        using ReloadStateTokenFn = RE::BSFixedString* (*)();

        constexpr std::size_t kMaxBindings = 192;
        constexpr int kMaxFlattenedTransforms = 768;
        constexpr std::uint32_t kLocalReloadTestLeaseFrames = 600;
        constexpr float kManualCycleFallbackWatchdogSeconds = 6.0f;
        constexpr float kManualCycleWatchdogPaddingSeconds = 0.75f;
        constexpr float kManualCycleMinimumWatchdogSeconds = 1.0f;
        constexpr float kManualCycleMaximumWatchdogSeconds = 12.0f;
        constexpr int kManualCycleVisualAuthorityPriority = 110;
        constexpr const char* kManualCycleVisualAuthorityTag =
            "PAPER_NativeManualCycle";
        constexpr std::uint32_t kImplementedFlags = native_animation_authority_policy::kReloadPose;
        constexpr std::array<std::string_view, 15> kManualCyclePrimaryFingerBoneNames{
            "RArm_Finger11", "RArm_Finger12", "RArm_Finger13",
            "RArm_Finger21", "RArm_Finger22", "RArm_Finger23",
            "RArm_Finger31", "RArm_Finger32", "RArm_Finger33",
            "RArm_Finger41", "RArm_Finger42", "RArm_Finger43",
            "RArm_Finger51", "RArm_Finger52", "RArm_Finger53",
        };
        constexpr std::array<std::string_view, 15> kManualCycleSupportFingerBoneNames{
            "LArm_Finger11", "LArm_Finger12", "LArm_Finger13",
            "LArm_Finger21", "LArm_Finger22", "LArm_Finger23",
            "LArm_Finger31", "LArm_Finger32", "LArm_Finger33",
            "LArm_Finger41", "LArm_Finger42", "LArm_Finger43",
            "LArm_Finger51", "LArm_Finger52", "LArm_Finger53",
        };

        struct Binding
        {
            int sourceIndex{ -1 };
            int destinationIndex{ -1 };
            std::uint32_t flags{ 0 };
            RE::NiTransform capturedLocal{};
            bool captured{ false };
        };

        struct BindingCache
        {
            BoneTree* sourceTree{ nullptr };
            BoneTree* destinationTree{ nullptr };
            BoneTransform* sourceTransforms{ nullptr };
            BoneTransform* destinationTransforms{ nullptr };
            int sourceCount{ 0 };
            int destinationCount{ 0 };
            int sourceWeaponIndex{ -1 };
            int destinationWeaponIndex{ -1 };
            std::array<Binding, kMaxBindings> bindings{};
            std::size_t bindingCount{ 0 };
        };

        struct NativeHandBoneCache
        {
            BoneTree* tree{ nullptr };
            BoneTransform* transforms{ nullptr };
            int transformCount{ 0 };
            int primaryHandIndex{ -1 };
            int supportHandIndex{ -1 };
            int weaponIndex{ -1 };
            std::array<int, kManualCyclePrimaryFingerBoneNames.size()> primaryFingerIndices{};
            std::array<int, kManualCycleSupportFingerBoneNames.size()> supportFingerIndices{};
        };

        struct NativeHandPoseCapture
        {
            RE::NiTransform weaponModel{};
            RE::NiTransform primaryHandInWeapon{};
            RE::NiTransform supportHandInWeapon{};
            frik_visual_authority::FingerLocalTransformOverride primaryFingerLocals{};
            frik_visual_authority::FingerLocalTransformOverride supportFingerLocals{};
            bool primaryHandValid{ false };
            bool supportHandValid{ false };
        };

        struct ManualCycleVisualPublication
        {
            bool worldPublished{ false };
            bool fingerPosePublished{ false };
        };

        struct ManualCycleHandRebase
        {
            RE::NiTransform nativeBaselineHandInWeapon{};
            RE::NiTransform liveBaselineHandInWeapon{};
            RE::NiTransform resolvedHandInWeapon{};
            RE::NiTransform resolvedHandWorld{};
            native_animation_authority_policy::WeaponFixedHandTargetMode
                targetMode{
                    native_animation_authority_policy::
                        WeaponFixedHandTargetMode::LiveGripDelta
                };
            float motionTranslationGameUnits{ 0.0f };
            float motionRotationDegrees{ 0.0f };
            bool captured{ false };
            bool baselineUnavailableLogged{ false };
            bool motionQualified{ false };
            bool resolvedHandInWeaponValid{ false };
            bool resolvedHandWorldValid{ false };
        };

        struct ResolvedManualCycleRockGripBaselines
        {
            std::uintptr_t weaponNode{ 0 };
            RE::NiTransform rightHandInWeapon{};
            RE::NiTransform leftHandInWeapon{};
            std::uint64_t weaponGenerationKey{ 0 };
            std::uint32_t weaponFormId{ 0 };
            bool rightValid{ false };
            bool authoredLeftActive{ false };
        };

        struct LatchedManualCycleAuthoredSupportGrip
        {
            RE::NiTransform handInWeapon{};
            native_animation_authority_policy::
                ManualCycleAuthoredSupportGripLatchState state{};
        };

        enum class ManualCycleHandVisualResult : std::uint8_t
        {
            Failed,
            Suppressed,
            Published,
        };

        struct ControllerAimFrame
        {
            // Non-owning scene references. They are valid only while the
            // matching flattened tree/cache and authority session remain
            // active; resetHybridPoseState clears them at every lease edge.
            RE::NiNode* weaponNode{ nullptr };
            RE::NiNode* controlParent{ nullptr };
            RE::NiTransform weaponInControlParent{};
            RE::NiTransform controlWeaponWorld{};
            RE::NiTransform nativeBaselineWeaponWorld{};
            RE::NiTransform desiredWeaponWorld{};
            std::array<ManualCycleHandRebase, 2> manualCycleHandRebases{};
            bool controlBindingCaptured{ false };
            bool controlCaptured{ false };
            bool nativeBaselineCaptured{ false };
            bool desiredCaptured{ false };
            bool destinationAlignmentLogged{ false };
            bool manualCycleIkLogged{ false };
        };

        BindingCache s_cache{};
        NativeHandBoneCache s_nativeHandBoneCache{};
        NativeHandPoseCapture s_nativeHandPoseCapture{};
        std::array<ManualCycleVisualPublication, 2> s_manualCycleVisualPublications{};
        // Current provider observation and the effective baselines are copied
        // on the game thread. The effective left baseline may retain only the
        // authored support grip latched at a manual cycle's entry.
        ResolvedManualCycleRockGripBaselines
            s_latestManualCycleRockGripBaselines{};
        ResolvedManualCycleRockGripBaselines
            s_manualCycleRockGripBaselines{};
        LatchedManualCycleAuthoredSupportGrip
            s_manualCycleAuthoredSupportGripLatch{};
        ControllerAimFrame s_sourceAimFrame{};
        // Paired final hand/weapon samples. Never combine an older wrist world
        // with the current weapon world when acquiring a cycle baseline.
        ResolvedManualCycleRockGripBaselines s_presentedGripBaselines{};
        // Investigation owner: PAPER hand participation. Remove after stationary
        // support and real pump/bolt trajectories are distinguished and qualified.
        // Debug logging must be enabled at GameLoaded.
        // The game thread owns this session; the bounded worker receives only
        // formatted values and shares PAPER's thread-safe sink. Destruction
        // releases the logger before draining/joining its pool.
        struct CycleTrace
        {
            std::shared_ptr<spdlog::details::thread_pool> pool;
            std::shared_ptr<spdlog::async_logger> log;
            NativeHandPoseCapture nativePose{};
            std::uint64_t graphSequence{ 0 };
            std::uint64_t fireSequence{ 0 };
            std::uint64_t reloadStartSequence{ 0 };
            std::uint64_t reloadEndSequence{ 0 };
            std::uint64_t weaponGenerationKey{ 0 };
            std::uint32_t weaponFormId{ 0 };
            bool active{ false };
            std::array<bool, 2> published{};
            std::array<bool, 2> qualified{};
            bool failureReported{ false };
            std::atomic<bool> failed{ false };
            ~CycleTrace()
            {
                log.reset();
                pool.reset();
            }
        };
        std::unique_ptr<CycleTrace> s_cycleTrace;
        WeaponFireHandlerFn s_originalWeaponFire{ nullptr };
        ReloadStateChangeHandlerFn s_originalReloadStateChange{ nullptr };
        std::atomic<bool> s_hookInstalled{ false };
        std::atomic<bool> s_reloadStateHookInstalled{ false };
        std::atomic<bool> s_weaponFireHookInstalled{ false };
        std::atomic<bool> s_weaponFireHookInstallFailed{ false };
        std::atomic<bool> s_hookInstallFailed{ false };
        std::atomic<bool> s_runtimeEnabled{ false };
        std::atomic<bool> s_localManualCycleTestEnabled{ false };
        std::atomic<bool> s_localReloadTestEnabled{ false };
        std::atomic<bool> s_manualCycleHandAnimationEligible{ false };
        std::atomic<bool> s_localManualCycleTestLeaseActive{ false };
        std::atomic<bool> s_captureValid{ false };
        std::atomic<bool> s_threadMismatch{ false };
        std::atomic<bool> s_captureFault{ false };
        std::atomic<DWORD> s_ownerThreadId{ 0 };
        std::atomic<std::uint32_t> s_localReloadTestLeaseFrames{ 0 };
        std::atomic<std::uint64_t> s_localReloadTestRequestSequence{ 0 };
        std::atomic<std::uint64_t> s_localManualCycleTestRequestSequence{ 0 };
        std::atomic<std::uint32_t> s_localManualCycleRequestedWatchdogMilliseconds{ 0 };
        std::atomic<std::uint64_t> s_localManualCycleReloadStartSequenceAtArm{ 0 };
        std::atomic<std::uint64_t> s_localManualCycleReloadEndSequenceAtArm{ 0 };
        std::atomic<std::uint64_t> s_playerReloadStartSequence{ 0 };
        std::atomic<std::uint64_t> s_playerReloadEndSequence{ 0 };
        std::atomic<std::uint64_t> s_playerWeaponFireSequence{ 0 };
        std::atomic<std::uint64_t>
            s_playerWeaponFireActivityOrderAtEvent{ 0 };
        std::atomic<bool> s_playerReloadEventActive{ false };
        std::atomic<std::uint32_t> s_capturedFlags{ 0 };
        std::atomic<std::uint32_t> s_capturedTransformCount{ 0 };
        std::atomic<std::uint64_t> s_captureSequence{ 0 };


        std::uint64_t s_frameCaptureSequence{ 0 };
        std::uint64_t s_lastCompletedCaptureSequence{ 0 };
        std::uint64_t s_seenLocalReloadTestRequestSequence{ 0 };
        std::uint64_t s_seenLocalManualCycleTestRequestSequence{ 0 };
        std::uint32_t s_frameCaptureFlags{ 0 };
        std::uint32_t s_lastLoggedEffectiveFlags{ 0 };
        native_animation_authority_policy::LocalReloadLeaseState s_localReloadLeaseState{};
        native_animation_authority_policy::LocalManualCycleLeaseState s_localManualCycleLeaseState{};
        bool s_frameCaptureReady{ false };
        bool s_frameCapturePrepared{ false };
        bool s_frameWeaponFixedHandsExpected{ false };
        bool s_framePartialReloadExpected{ false };
        bool s_frameWeaponFixedHandsApplied{ false };
        bool s_frameWeaponFixedHandsCleanupPending{ false };
        bool s_beforeRockApplicationAttempted{ false };
        bool s_beforeRockApplicationSucceeded{ false };
        bool s_afterRockApplicationAttempted{ false };
        bool s_afterRockApplicationSucceeded{ false };

        [[nodiscard]] bool validTree(const BoneTree* tree)
        {
            return tree && tree->transforms && tree->numTransforms > 0 && tree->numTransforms <= kMaxFlattenedTransforms;
        }

        [[nodiscard]] std::string_view transformName(const BoneTransform& transform)
        {
            const char* name = transform.name.c_str();
            return name ? std::string_view(name) : std::string_view{};
        }

        [[nodiscard]] bool finiteTransform(const RE::NiTransform& transform)
        {
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    if (!std::isfinite(transform.rotate.entry[row][column])) {
                        return false;
                    }
                }
            }
            return std::isfinite(transform.translate.x) &&
                   std::isfinite(transform.translate.y) &&
                   std::isfinite(transform.translate.z) &&
                   std::isfinite(transform.scale) &&
                   std::abs(transform.scale) > 0.000001f;
        }

        [[nodiscard]] native_animation_authority_policy::ManualCycleHandMotionSample
            measureManualCycleHandMotion(
                const RE::NiTransform& baselineHandInWeapon,
                const RE::NiTransform& currentHandInWeapon)
        {
            const RE::NiTransform delta = transform_math::composeTransforms(
                transform_math::invertTransform(baselineHandInWeapon),
                currentHandInWeapon);
            if (!finiteTransform(delta)) {
                return {};
            }

            const float translationGameUnits = std::sqrt(
                delta.translate.x * delta.translate.x +
                delta.translate.y * delta.translate.y +
                delta.translate.z * delta.translate.z);
            const float trace =
                delta.rotate.entry[0][0] +
                delta.rotate.entry[1][1] +
                delta.rotate.entry[2][2];
            const float rawCosine = (trace - 1.0f) * 0.5f;
            const float cosine = rawCosine < -1.0f ?
                -1.0f :
                (rawCosine > 1.0f ? 1.0f : rawCosine);
            constexpr float kRadiansToDegrees = 57.29577951308232f;
            const float rotationDegrees =
                std::acos(cosine) * kRadiansToDegrees;
            if (!std::isfinite(translationGameUnits) ||
                !std::isfinite(rotationDegrees)) {
                return {};
            }
            return {
                .translationGameUnits = translationGameUnits,
                .rotationDegrees = rotationDegrees,
            };
        }

        [[nodiscard]] constexpr std::size_t manualCycleHandIndex(
            const frik_visual_authority::Hand hand)
        {
            return hand == frik_visual_authority::Hand::Left ? 1u : 0u;
        }

        void setEffectiveManualCycleRockGripBaselines(
            const ResolvedManualCycleRockGripBaselines& resolved)
        {
            const auto& previous = s_manualCycleRockGripBaselines;
            const bool sameWeapon = previous.weaponNode == resolved.weaponNode &&
                previous.weaponFormId == resolved.weaponFormId;
            const bool initialCollisionReady = sameWeapon && previous.weaponNode != 0 &&
                previous.weaponGenerationKey == 0 && resolved.weaponGenerationKey != 0;
            const bool weaponGenerationChanged = !sameWeapon ||
                (previous.weaponGenerationKey != resolved.weaponGenerationKey && !initialCollisionReady);
            const bool authoredLeftStateChanged =
                s_manualCycleRockGripBaselines.authoredLeftActive !=
                resolved.authoredLeftActive;
            if (weaponGenerationChanged) {
                s_sourceAimFrame.manualCycleHandRebases = {};
            } else if (authoredLeftStateChanged) {
                s_sourceAimFrame.manualCycleHandRebases[
                    manualCycleHandIndex(
                        frik_visual_authority::Hand::Left)] = {};
            }
            s_manualCycleRockGripBaselines = resolved;
            if ((weaponGenerationChanged || authoredLeftStateChanged) &&
                resolved.authoredLeftActive) {
                PAPER_LOG_INFO(
                    Animation,
                    "Native fire-cycle authored support baseline available weapon={:016X}",
                    resolved.weaponGenerationKey);
            }
        }

        void refreshEffectiveManualCycleRockGripBaselines(
            const bool manualCycleLeaseActive)
        {
            auto effective = s_latestManualCycleRockGripBaselines;
            if (manualCycleLeaseActive) {
                effective.authoredLeftActive = false;
                const auto& latch =
                    s_manualCycleAuthoredSupportGripLatch;
                if (latch.state.active &&
                    latch.state.weaponGenerationKey ==
                        effective.weaponGenerationKey) {
                    effective.leftHandInWeapon =
                        latch.handInWeapon;
                    effective.authoredLeftActive = true;
                }
            }
            setEffectiveManualCycleRockGripBaselines(effective);
        }

        void clearManualCycleRockGripState()
        {
            s_latestManualCycleRockGripBaselines = {};
            s_manualCycleAuthoredSupportGripLatch = {};
            setEffectiveManualCycleRockGripBaselines({});
        }

        [[nodiscard]] bool clearManualCycleVisualForHand(
            const frik_visual_authority::Hand hand)
        {
            auto& publication =
                s_manualCycleVisualPublications[manualCycleHandIndex(hand)];
            if (!publication.worldPublished && !publication.fingerPosePublished) {
                return true;
            }

            const bool skeletonReady =
                frik_visual_authority::isSkeletonReadyHint();
            const bool fingerPoseCleared = !publication.fingerPosePublished ||
                frik_visual_authority::clearHandPose(
                    kManualCycleVisualAuthorityTag,
                    hand);
            const bool worldCleared = !publication.worldPublished ||
                frik_visual_authority::clearExternalHandWorldTransform(
                    kManualCycleVisualAuthorityTag,
                    hand);

            if (fingerPoseCleared || !skeletonReady) {
                publication.fingerPosePublished = false;
            }
            if (worldCleared || !skeletonReady) {
                publication.worldPublished = false;
            }
            return !publication.worldPublished &&
                   !publication.fingerPosePublished;
        }

        void clearManualCycleVisualAuthority()
        {
            (void)clearManualCycleVisualForHand(
                frik_visual_authority::Hand::Right);
            (void)clearManualCycleVisualForHand(
                frik_visual_authority::Hand::Left);
        }

        [[nodiscard]] bool manualCycleVisualAuthorityPublished()
        {
            for (const auto& publication : s_manualCycleVisualPublications) {
                if (publication.worldPublished ||
                    publication.fingerPosePublished) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool claimOrValidateThread()
        {
            const DWORD currentThread = GetCurrentThreadId();
            DWORD expected = 0;
            if (s_ownerThreadId.compare_exchange_strong(expected, currentThread, std::memory_order_acq_rel)) {
                return true;
            }
            if (expected == currentThread) {
                return true;
            }
            s_threadMismatch.store(true, std::memory_order_release);
            s_captureValid.store(false, std::memory_order_release);
            return false;
        }

        [[nodiscard]] bool localReloadLeaseActive()
        {
            return s_localReloadTestLeaseFrames.load(
                       std::memory_order_acquire) > 0;
        }

        [[nodiscard]] std::uint32_t localRequestedFlags()
        {
            if (!s_runtimeEnabled.load(std::memory_order_acquire)) {
                return 0;
            }

            std::uint32_t localFlags =
                native_animation_authority_policy::resolveLocalReloadAuthorityFlags(
                    localReloadLeaseActive());
            if (s_localManualCycleTestLeaseActive.load(std::memory_order_acquire) &&
                s_manualCycleHandAnimationEligible.load(std::memory_order_acquire)) {
                localFlags |= native_animation_authority_policy::kManualCyclePose;
            }
            return localFlags & kImplementedFlags;
        }

        [[nodiscard]] std::uint32_t effectiveRequestedFlags()
        {
            if (!s_runtimeEnabled.load(std::memory_order_acquire)) {
                return 0;
            }

            rock::api::animation::NativeAnimationAuthorityStateV1 state{};
            const std::uint32_t providerFlags =
                rockApiClient().queryNativeAnimationAuthorityState(state) ?
                state.activeFlags :
                0;
            return (providerFlags | localRequestedFlags()) & kImplementedFlags;
        }

        [[nodiscard]] int findDestinationIndex(const BoneTree& destination, std::string_view sourceName)
        {
            for (int index = 0; index < destination.numTransforms; ++index) {
                if (native_animation_authority_policy::equalsIgnoreCase(
                        transformName(destination.transforms[index]), sourceName)) {
                    return index;
                }
            }
            return -1;
        }

        void resetHybridPoseState()
        {
            s_sourceAimFrame = {};
            s_presentedGripBaselines = {};
        }

        [[nodiscard]] bool cacheMatches(BoneTree* source, BoneTree* destination)
        {
            return s_cache.sourceTree == source &&
                   s_cache.destinationTree == destination &&
                   s_cache.sourceTransforms == source->transforms &&
                   s_cache.destinationTransforms == destination->transforms &&
                   s_cache.sourceCount == source->numTransforms &&
                   s_cache.destinationCount == destination->numTransforms &&
                   s_cache.sourceWeaponIndex >= 0 &&
                   s_cache.bindingCount > 0;
        }

        [[nodiscard]] bool rebuildCache(BoneTree* source, BoneTree* destination)
        {
            resetHybridPoseState();
            s_cache = {};
            if (!validTree(source) || !validTree(destination)) {
                return false;
            }

            s_cache.sourceTree = source;
            s_cache.destinationTree = destination;
            s_cache.sourceTransforms = source->transforms;
            s_cache.destinationTransforms = destination->transforms;
            s_cache.sourceCount = source->numTransforms;
            s_cache.destinationCount = destination->numTransforms;

            for (int sourceIndex = 0; sourceIndex < source->numTransforms; ++sourceIndex) {
                const auto name = transformName(source->transforms[sourceIndex]);
                const auto flags = native_animation_authority_policy::classifyBone(name);
                if (flags == 0) {
                    continue;
                }
                if (s_cache.bindingCount >= s_cache.bindings.size()) {
                    s_cache = {};
                    return false;
                }

                auto& binding = s_cache.bindings[s_cache.bindingCount++];
                binding.sourceIndex = sourceIndex;
                binding.destinationIndex = findDestinationIndex(*destination, name);
                binding.flags = flags;
                if (native_animation_authority_policy::equalsIgnoreCase(name, "Weapon")) {
                    s_cache.sourceWeaponIndex = binding.sourceIndex;
                    s_cache.destinationWeaponIndex = binding.destinationIndex;
                }
            }
            return s_cache.bindingCount > 0 &&
                   s_cache.sourceWeaponIndex >= 0 &&
                   s_cache.destinationWeaponIndex >= 0;
        }

        [[nodiscard]] const RE::NiTransform& authoritativeLocal(const BoneTransform& transform)
        {
            return transform.refNode ? transform.refNode->local : transform.local;
        }

        struct NativeLogicalModelTransform
        {
            RE::NiTransform model{};
            int rootIndex{ -1 };
        };

        [[nodiscard]] bool composeNativeLogicalModelTransform(
            const BoneTree& tree,
            const int leafIndex,
            NativeLogicalModelTransform& out)
        {
            out = {};
            if (leafIndex < 0 || leafIndex >= tree.numTransforms) {
                return false;
            }

            // BSFlattenedBoneTree_UpdateBoneArray at FO4VR 0x141C214B0
            // proves that `world` is presentation cache: when refNode exists,
            // the engine copies refNode->world into it. It is therefore the
            // wrong source for the controller-independent authored pose.
            // Walk parPos and compose only graph-local transforms instead.
            std::array<int, kMaxFlattenedTransforms> chain{};
            std::size_t chainLength = 0;
            int currentIndex = leafIndex;
            while (currentIndex >= 0) {
                if (currentIndex >= tree.numTransforms ||
                    chainLength >= static_cast<std::size_t>(tree.numTransforms) ||
                    chainLength >= chain.size()) {
                    return false;
                }
                chain[chainLength++] = currentIndex;
                const int parentIndex = tree.transforms[currentIndex].parPos;
                if (parentIndex < 0) {
                    break;
                }
                currentIndex = parentIndex;
            }
            if (chainLength == 0 ||
                tree.transforms[chain[chainLength - 1]].parPos >= 0) {
                return false;
            }

            RE::NiTransform model =
                transform_math::identityTransform<RE::NiTransform>();
            for (std::size_t chainPosition = chainLength;
                 chainPosition > 0;
                 --chainPosition) {
                const int transformIndex = chain[chainPosition - 1];
                const RE::NiTransform& local =
                    authoritativeLocal(tree.transforms[transformIndex]);
                if (!finiteTransform(local)) {
                    return false;
                }
                model = transform_math::composeTransforms(model, local);
                if (!finiteTransform(model)) {
                    return false;
                }
            }

            out.model = model;
            out.rootIndex = chain[chainLength - 1];
            return true;
        }

        void invalidateCapture()
        {
            s_captureValid.store(false, std::memory_order_release);
            s_capturedFlags.store(0, std::memory_order_release);
            s_capturedTransformCount.store(0, std::memory_order_release);
            s_nativeHandPoseCapture = {};
        }


        [[nodiscard]] bool nativeHandCacheMatches(const BoneTree& source)
        {
            return s_nativeHandBoneCache.tree == &source &&
                   s_nativeHandBoneCache.transforms == source.transforms &&
                   s_nativeHandBoneCache.transformCount == source.numTransforms &&
                   s_nativeHandBoneCache.primaryHandIndex >= 0 &&
                   s_nativeHandBoneCache.weaponIndex >= 0;
        }

        [[nodiscard]] bool rebuildNativeHandBoneCache(BoneTree& source)
        {
            s_nativeHandBoneCache = {};
            s_nativeHandBoneCache.primaryFingerIndices.fill(-1);
            s_nativeHandBoneCache.supportFingerIndices.fill(-1);
            if (!validTree(&source)) {
                return false;
            }

            s_nativeHandBoneCache.tree = &source;
            s_nativeHandBoneCache.transforms = source.transforms;
            s_nativeHandBoneCache.transformCount = source.numTransforms;
            for (int index = 0; index < source.numTransforms; ++index) {
                const auto name = transformName(source.transforms[index]);
                if (native_animation_authority_policy::equalsIgnoreCase(name, "RArm_Hand")) {
                    s_nativeHandBoneCache.primaryHandIndex = index;
                } else if (native_animation_authority_policy::equalsIgnoreCase(name, "LArm_Hand")) {
                    s_nativeHandBoneCache.supportHandIndex = index;
                } else if (native_animation_authority_policy::equalsIgnoreCase(name, "Weapon")) {
                    s_nativeHandBoneCache.weaponIndex = index;
                }
                for (std::size_t fingerIndex = 0;
                     fingerIndex < kManualCyclePrimaryFingerBoneNames.size();
                     ++fingerIndex) {
                    if (native_animation_authority_policy::equalsIgnoreCase(
                            name,
                            kManualCyclePrimaryFingerBoneNames[fingerIndex])) {
                        s_nativeHandBoneCache.primaryFingerIndices[fingerIndex] = index;
                        break;
                    }
                }
                for (std::size_t fingerIndex = 0;
                     fingerIndex < kManualCycleSupportFingerBoneNames.size();
                     ++fingerIndex) {
                    if (native_animation_authority_policy::equalsIgnoreCase(
                            name,
                            kManualCycleSupportFingerBoneNames[fingerIndex])) {
                        s_nativeHandBoneCache.supportFingerIndices[fingerIndex] = index;
                        break;
                    }
                }
            }
            return s_nativeHandBoneCache.primaryHandIndex >= 0 &&
                   s_nativeHandBoneCache.weaponIndex >= 0;
        }

        void captureNativeFingerLocals(
            const BoneTree& source,
            const std::array<int, kManualCyclePrimaryFingerBoneNames.size()>& indices,
            frik_visual_authority::FingerLocalTransformOverride& out)
        {
            out = {};
            for (std::size_t fingerIndex = 0;
                 fingerIndex < indices.size();
                 ++fingerIndex) {
                const int transformIndex = indices[fingerIndex];
                if (transformIndex < 0 || transformIndex >= source.numTransforms) {
                    continue;
                }
                const RE::NiTransform& local =
                    authoritativeLocal(source.transforms[transformIndex]);
                if (!finiteTransform(local)) {
                    continue;
                }
                out.localTransforms[fingerIndex] = local;
                out.enabledMask |= static_cast<std::uint16_t>(1u << fingerIndex);
            }
        }

        [[nodiscard]] bool captureNativeHandPose(
            BoneTree& source, NativeHandPoseCapture& out)
        {
            out = {};
            if (!nativeHandCacheMatches(source) &&
                !rebuildNativeHandBoneCache(source)) {
                return false;
            }

            const int primaryHandIndex =
                s_nativeHandBoneCache.primaryHandIndex;
            const int supportHandIndex =
                s_nativeHandBoneCache.supportHandIndex;
            const int weaponIndex = s_nativeHandBoneCache.weaponIndex;
            if (primaryHandIndex < 0 || primaryHandIndex >= source.numTransforms ||
                weaponIndex < 0 || weaponIndex >= source.numTransforms) {
                return false;
            }

            const auto& weaponTransform = source.transforms[weaponIndex];
            const RE::NiTransform& animatedWeaponLocal =
                authoritativeLocal(weaponTransform);
            if (weaponTransform.parPos != primaryHandIndex ||
                !finiteTransform(animatedWeaponLocal)) {
                return false;
            }

            NativeLogicalModelTransform primaryHandModel{};
            if (!composeNativeLogicalModelTransform(
                    source,
                    primaryHandIndex,
                    primaryHandModel)) {
                return false;
            }
            const RE::NiTransform nativeWeaponModel =
                transform_math::composeTransforms(
                    primaryHandModel.model,
                    animatedWeaponLocal);
            if (!finiteTransform(nativeWeaponModel)) {
                return false;
            }

            const auto resolveHandInWeapon = [&](
                                                   const RE::NiTransform& handModel) {
                return native_animation_authority_policy::resolveNativeHandInWeapon(
                    nativeWeaponModel,
                    handModel,
                    [](const RE::NiTransform& parent,
                       const RE::NiTransform& child) {
                        return transform_math::composeTransforms(parent, child);
                    },
                    [](const RE::NiTransform& transform) {
                        return transform_math::invertTransform(transform);
                    });
            };

            out.weaponModel = nativeWeaponModel;
            out.primaryHandInWeapon =
                resolveHandInWeapon(primaryHandModel.model);
            if (!finiteTransform(
                    out.primaryHandInWeapon)) {
                out = {};
                return false;
            }
            out.primaryHandValid = true;
            captureNativeFingerLocals(
                source,
                s_nativeHandBoneCache.primaryFingerIndices,
                out.primaryFingerLocals);

            if (supportHandIndex >= 0 &&
                supportHandIndex < source.numTransforms) {
                NativeLogicalModelTransform supportHandModel{};
                if (composeNativeLogicalModelTransform(
                        source,
                        supportHandIndex,
                        supportHandModel) &&
                    supportHandModel.rootIndex == primaryHandModel.rootIndex) {
                    const RE::NiTransform supportHandInWeapon =
                        resolveHandInWeapon(supportHandModel.model);
                    if (finiteTransform(supportHandInWeapon)) {
                        out.supportHandInWeapon =
                            supportHandInWeapon;
                        out.supportHandValid = true;
                        captureNativeFingerLocals(
                            source,
                            s_nativeHandBoneCache.supportFingerIndices,
                            out.supportFingerLocals);
                    }
                }
            }
            return true;
        }

        void captureCycleTracePose()
        {
            if (!s_cycleTrace || s_cycleTrace->failed.load(std::memory_order_relaxed) ||
                !logger::instance->should_log(spdlog::level::debug) ||
                !s_runtimeEnabled.load(std::memory_order_acquire) || !claimOrValidateThread()) {
                return;
            }
            auto& trace = *s_cycleTrace;
            ++trace.graphSequence;
            trace.nativePose = {};
            if (s_captureValid.load(std::memory_order_acquire)) {
                trace.nativePose = s_nativeHandPoseCapture;
            } else if (auto* source = f4vr::getFirstPersonBoneTree(); validTree(source)) {
                // Read idle graph locals too: an event's first sample may already
                // contain recoil. This diagnostic copy never grants pose authority.
                (void)captureNativeHandPose(*source, trace.nativePose);
            }
        }

        void captureNativePose()
        {
            const std::uint32_t requestedFlags = effectiveRequestedFlags();
            if (requestedFlags == 0 || !claimOrValidateThread()) {
                invalidateCapture();
                return;
            }

            auto* source = f4vr::getFirstPersonBoneTree();
            auto* destination = f4vr::getFlattenedBoneTree();
            if (!validTree(source) || !validTree(destination)) {
                invalidateCapture();
                return;
            }
            if (!cacheMatches(source, destination) && !rebuildCache(source, destination)) {
                invalidateCapture();
                return;
            }

            std::uint32_t capturedCount = 0;
            std::uint32_t destinationMappedFlags = 0;
            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                auto& binding = s_cache.bindings[i];
                binding.captured = false;
                const bool requestedBinding =
                    native_animation_authority_policy::isRequested(binding.flags, requestedFlags);
                const bool controllerAimAnchor = binding.sourceIndex == s_cache.sourceWeaponIndex;
                if ((!requestedBinding && !controllerAimAnchor) ||
                    binding.sourceIndex < 0 || binding.sourceIndex >= source->numTransforms) {
                    continue;
                }

                const auto& local =
                    authoritativeLocal(source->transforms[binding.sourceIndex]);
                if (!finiteTransform(local)) {
                    continue;
                }
                binding.capturedLocal = local;
                binding.captured = true;
                ++capturedCount;
                if (requestedBinding && binding.destinationIndex >= 0) {
                    destinationMappedFlags |= binding.flags;
                }
            }

            if (capturedCount == 0 || (destinationMappedFlags & requestedFlags) != requestedFlags) {
                s_nativeHandPoseCapture = {};
                invalidateCapture();
                return;
            }

            const bool handOnlyAuthority =
                (requestedFlags & native_animation_authority_policy::kWeapon) == 0;
            if (handOnlyAuthority) {
                if ((requestedFlags & native_animation_authority_policy::kArms) == 0 ||
                    !captureNativeHandPose(*source, s_nativeHandPoseCapture)) {
                    s_nativeHandPoseCapture = {};
                    invalidateCapture();
                    return;
                }
            } else {
                // Full reload authority does not require this derived hand pose
                // for application, but retain it for Paper API consumers.
                (void)captureNativeHandPose(*source, s_nativeHandPoseCapture);
            }

            s_capturedFlags.store(requestedFlags, std::memory_order_release);
            s_capturedTransformCount.store(capturedCount, std::memory_order_release);
            s_captureSequence.fetch_add(1, std::memory_order_acq_rel);
            s_captureValid.store(true, std::memory_order_release);
        }

        [[nodiscard]] RE::TESObjectWEAP::InstanceData* currentPlayerWeaponInstanceData(
            RE::TESObjectWEAP*& outWeapon)
        {
            outWeapon = nullptr;
            auto* equipData = f4vr::getEquippedItem();
            auto* weaponForm = equipData ? equipData->item.object : nullptr;
            if (!weaponForm ||
                weaponForm->formType != RE::ENUM_FORM_ID::kWEAP) {
                return nullptr;
            }

            outWeapon = weaponForm->As<RE::TESObjectWEAP>();
            if (!outWeapon) {
                return nullptr;
            }
            return equipData->item.instanceData ?
                static_cast<RE::TESObjectWEAP::InstanceData*>(equipData->item.instanceData.get()) :
                &outWeapon->weaponData;
        }

        [[nodiscard]] float manualCycleWatchdogSeconds(
            const RE::TESObjectWEAP::InstanceData& weaponData)
        {
            const auto* rangedData = weaponData.rangedData;
            if (!rangedData ||
                !std::isfinite(rangedData->boltChargeSeconds) ||
                rangedData->boltChargeSeconds <= 0.0f) {
                return kManualCycleFallbackWatchdogSeconds;
            }

            const float fireSeconds =
                std::isfinite(rangedData->fireSeconds) && rangedData->fireSeconds > 0.0f ?
                rangedData->fireSeconds :
                0.0f;
            float watchdogSeconds =
                fireSeconds + rangedData->boltChargeSeconds +
                kManualCycleWatchdogPaddingSeconds;
            if (watchdogSeconds < kManualCycleMinimumWatchdogSeconds) {
                watchdogSeconds = kManualCycleMinimumWatchdogSeconds;
            } else if (watchdogSeconds > kManualCycleMaximumWatchdogSeconds) {
                watchdogSeconds = kManualCycleMaximumWatchdogSeconds;
            }
            return watchdogSeconds;
        }

        void cancelLocalManualCycleTestLease()
        {
            s_localManualCycleRequestedWatchdogMilliseconds.store(0, std::memory_order_release);
            s_localManualCycleTestLeaseActive.store(false, std::memory_order_release);
        }

        void armLocalReloadTestLeaseFromNativeStart()
        {
            if (!s_runtimeEnabled.load(std::memory_order_acquire) ||
                !s_localReloadTestEnabled.load(std::memory_order_acquire) ||
                !s_hookInstalled.load(std::memory_order_acquire)) {
                return;
            }

            s_localReloadTestLeaseFrames.store(
                kLocalReloadTestLeaseFrames,
                std::memory_order_release);
            s_localReloadTestRequestSequence.fetch_add(
                1,
                std::memory_order_acq_rel);

            PAPER_LOG_INFO(Animation,
                "Native reload partial-authority local test lease armed from Bethesda player reload-start; composition=weapon-fixed-hands watchdog={} ROCK frames",
                kLocalReloadTestLeaseFrames);
        }

        bool onWeaponFire(void* handler, RE::Actor* actor, RE::BSFixedString* eventData)
        {
            // Sample before Bethesda handles the fire event. Replacement clips
            // may publish their frame-zero ReloadEnd marker synchronously from
            // the original handler; arming from the post-call value would lose
            // the first half of the native cycle bracket.
            const auto reloadStartSequenceBeforeFire =
                s_playerReloadStartSequence.load(std::memory_order_acquire);
            const auto reloadEndSequenceBeforeFire =
                s_playerReloadEndSequence.load(std::memory_order_acquire);
            const auto* player = RE::PlayerCharacter::GetSingleton();
            std::uint64_t activityOrderBeforeEvent = 0;
            if (actor && actor == player) {
                activityOrderBeforeEvent =
                    clip_telemetry::activityState().activationOrder;
            }
            const bool handled = s_originalWeaponFire ?
                s_originalWeaponFire(handler, actor, eventData) :
                false;

            if (handled && actor && actor == player) {
                const auto afterHandlerActivityOrder =
                    clip_telemetry::activityState().activationOrder;
                const auto activityOrderAtEvent =
                    afterHandlerActivityOrder > activityOrderBeforeEvent ?
                    afterHandlerActivityOrder :
                    0;
                s_playerWeaponFireActivityOrderAtEvent.store(
                    activityOrderAtEvent,
                    std::memory_order_release);
                s_playerWeaponFireSequence.fetch_add(
                    1,
                    std::memory_order_acq_rel);
            }

            if (!handled || !actor || actor != player ||
                !s_runtimeEnabled.load(std::memory_order_acquire) ||
                !s_localManualCycleTestEnabled.load(std::memory_order_acquire) ||
                !s_manualCycleHandAnimationEligible.load(std::memory_order_acquire) ||
                s_localReloadTestLeaseFrames.load(std::memory_order_acquire) > 0 ||
                s_playerReloadEventActive.load(std::memory_order_acquire)) {
                return handled;
            }

            RE::TESObjectWEAP* weapon = nullptr;
            const auto* weaponData = currentPlayerWeaponInstanceData(weapon);
            if (!weapon || !weaponData) {
                return handled;
            }

            const float watchdogSeconds = manualCycleWatchdogSeconds(*weaponData);
            const auto watchdogMilliseconds = static_cast<std::uint32_t>(
                watchdogSeconds * 1000.0f + 0.5f);
            s_localManualCycleReloadStartSequenceAtArm.store(
                reloadStartSequenceBeforeFire,
                std::memory_order_release);
            s_localManualCycleReloadEndSequenceAtArm.store(
                reloadEndSequenceBeforeFire,
                std::memory_order_release);
            s_localManualCycleRequestedWatchdogMilliseconds.store(
                watchdogMilliseconds,
                std::memory_order_release);
            s_localManualCycleTestRequestSequence.fetch_add(1, std::memory_order_acq_rel);
            s_localManualCycleTestLeaseActive.store(true, std::memory_order_release);
            return handled;
        }

        [[nodiscard]] const RE::BSFixedString* nativeReloadStartStateToken()
        {
            static REL::Relocation<ReloadStateTokenFn> getToken{
                REL::Offset(offsets::kFunc_GetReloadStartStateToken)
            };
            return getToken();
        }

        [[nodiscard]] const RE::BSFixedString* nativeReloadEndStateToken()
        {
            static REL::Relocation<ReloadStateTokenFn> getToken{
                REL::Offset(offsets::kFunc_GetReloadEndStateToken)
            };
            return getToken();
        }

        bool onReloadStateChange(void* handler, RE::Actor* actor, RE::BSFixedString* stateToken)
        {
            const bool handled = s_originalReloadStateChange ?
                s_originalReloadStateChange(handler, actor, stateToken) :
                false;

            const auto* player = RE::PlayerCharacter::GetSingleton();
            if (!actor || actor != player || !stateToken) {
                return handled;
            }

            const auto* startToken = nativeReloadStartStateToken();
            if (startToken && *stateToken == *startToken) {
                manual_reload_only::observePlayerReloadStart();
                cancelLocalManualCycleTestLease();
                s_playerReloadEventActive.store(true, std::memory_order_release);
                s_playerReloadStartSequence.fetch_add(1, std::memory_order_acq_rel);
                tactical_reload_bridge::notifyPlayerReloadStart();
                armLocalReloadTestLeaseFromNativeStart();
                return handled;
            }

            const auto* endToken = nativeReloadEndStateToken();
            if (endToken && *stateToken == *endToken) {
                s_playerReloadEndSequence.fetch_add(1, std::memory_order_acq_rel);
                s_playerReloadEventActive.store(false, std::memory_order_release);
                tactical_reload_bridge::notifyPlayerReloadEnd();
            }
            return handled;
        }

        [[nodiscard]] bool bindingSelectedForTree(
            const Binding& binding,
            bool destination,
            std::uint32_t requestedFlags)
        {
            const int index = destination ? binding.destinationIndex : binding.sourceIndex;
            return binding.captured && index >= 0 &&
                   native_animation_authority_policy::isRequested(binding.flags, requestedFlags);
        }

        [[nodiscard]] bool parentIsSelected(
            int parentIndex,
            bool destination,
            std::uint32_t requestedFlags)
        {
            if (parentIndex < 0) {
                return false;
            }
            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                const auto& candidate = s_cache.bindings[i];
                const int candidateIndex = destination ? candidate.destinationIndex : candidate.sourceIndex;
                if (candidateIndex == parentIndex && bindingSelectedForTree(candidate, destination, requestedFlags)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool usesDifferentSceneParent(const BoneTree& tree, int index)
        {
            if (index < 0 || index >= tree.numTransforms) {
                return false;
            }
            const auto& transform = tree.transforms[index];
            if (!transform.refNode || transform.parPos < 0 || transform.parPos >= tree.numTransforms) {
                return false;
            }
            return transform.refNode->parent != tree.transforms[transform.parPos].refNode;
        }

        [[nodiscard]] bool readPresentationWorld(
            const BoneTree& tree,
            int index,
            RE::NiTransform& outWorld)
        {
            if (index < 0 || index >= tree.numTransforms) {
                return false;
            }
            const auto& transform = tree.transforms[index];
            outWorld = transform.refNode ? transform.refNode->world : transform.world;
            return finiteTransform(outWorld);
        }

        [[nodiscard]] bool readLogicalWorld(
            const BoneTree& tree,
            int index,
            RE::NiTransform& outWorld)
        {
            if (index < 0 || index >= tree.numTransforms) {
                return false;
            }
            const auto& transform = tree.transforms[index];
            if (transform.parPos >= 0 && transform.parPos < tree.numTransforms) {
                const auto& logicalParent = tree.transforms[transform.parPos];
                const RE::NiTransform& parentWorld = logicalParent.refNode ? logicalParent.refNode->world : logicalParent.world;
                if (!finiteTransform(parentWorld) || !finiteTransform(transform.local)) {
                    return false;
                }
                outWorld = transform_math::composeTransforms(parentWorld, transform.local);
                return finiteTransform(outWorld);
            }
            return readPresentationWorld(tree, index, outWorld);
        }

        [[nodiscard]] bool prepareControllerAimFrame(
            const BoneTree& tree,
            int weaponIndex,
            ControllerAimFrame& aimFrame)
        {
            if (weaponIndex < 0 || weaponIndex >= tree.numTransforms) {
                return false;
            }

            const auto& weaponTransform = tree.transforms[weaponIndex];
            auto* weaponNode = weaponTransform.refNode;
            auto* controlParent = weaponNode ? weaponNode->parent : nullptr;
            if (!weaponNode || !controlParent ||
                !finiteTransform(controlParent->world) ||
                !finiteTransform(weaponNode->local)) {
                return false;
            }

            if (!aimFrame.controlBindingCaptured) {
                aimFrame.weaponNode = weaponNode;
                aimFrame.controlParent = controlParent;
                aimFrame.weaponInControlParent = weaponNode->local;
                aimFrame.controlBindingCaptured = true;
            } else if (aimFrame.weaponNode != weaponNode || aimFrame.controlParent != controlParent) {
                // A weapon/skeleton replacement or a live reparent invalidates
                // both the controller and authored baselines. Fail closed; the
                // next authority edge will establish a coherent new session.
                return false;
            }

            aimFrame.controlWeaponWorld = transform_math::composeTransforms(
                aimFrame.controlParent->world,
                aimFrame.weaponInControlParent);
            aimFrame.controlCaptured = finiteTransform(aimFrame.controlWeaponWorld);
            return aimFrame.controlCaptured;
        }

        [[nodiscard]] bool prepareControllerAimFrames()
        {
            if (!validTree(s_cache.sourceTree) || !validTree(s_cache.destinationTree)) {
                return false;
            }
            // The first-person Weapon node is the rendered gun and therefore
            // the only valid live control frame. hFRIK deliberately culls the
            // full-body Weapon node; its world follows the solved body arm and
            // must never become an independent target for the visible arms.
            return prepareControllerAimFrame(
                *s_cache.sourceTree,
                s_cache.sourceWeaponIndex,
                s_sourceAimFrame);
        }

        [[nodiscard]] bool prepareWeaponFixedHandsWeaponNode()
        {
            if (!validTree(s_cache.sourceTree) ||
                s_cache.sourceWeaponIndex < 0 ||
                s_cache.sourceWeaponIndex >= s_cache.sourceTree->numTransforms) {
                return false;
            }
            auto* weaponNode = s_cache.sourceTree
                                   ->transforms[s_cache.sourceWeaponIndex]
                                   .refNode;
            if (!weaponNode) {
                return false;
            }
            if (s_sourceAimFrame.weaponNode &&
                s_sourceAimFrame.weaponNode != weaponNode) {
                return false;
            }
            s_sourceAimFrame.weaponNode = weaponNode;
            return true;
        }

        [[nodiscard]] bool refreshFixedVisibleWeaponTarget(
            RE::NiTransform& outWeaponWorld)
        {
            auto& aimFrame = s_sourceAimFrame;
            auto* weaponNode = aimFrame.weaponNode;
            if (!weaponNode || !weaponNode->parent ||
                !finiteTransform(weaponNode->world)) {
                return false;
            }

            outWeaponWorld = weaponNode->world;
            aimFrame.controlWeaponWorld = outWeaponWorld;
            aimFrame.controlCaptured = true;
            aimFrame.desiredWeaponWorld = outWeaponWorld;
            aimFrame.desiredCaptured = true;
            return true;
        }

        [[nodiscard]] bool restoreFixedVisibleWeaponTarget(
            const RE::NiTransform& weaponWorld)
        {
            auto& aimFrame = s_sourceAimFrame;
            auto* weaponNode = aimFrame.weaponNode;
            auto* weaponParent = weaponNode ? weaponNode->parent : nullptr;
            if (!weaponNode || !weaponParent ||
                !finiteTransform(weaponWorld) ||
                !finiteTransform(weaponParent->world)) {
                return false;
            }

            const RE::NiTransform weaponLocal = transform_math::composeTransforms(
                transform_math::invertTransform(weaponParent->world),
                weaponWorld);
            if (!finiteTransform(weaponLocal)) {
                return false;
            }

            // The native animation graph and OMOD controllers own the weapon's
            // articulated descendants.  Restore only the flattened Weapon root;
            // recursively rebuilding from descendant locals would erase their
            // already-evaluated presentation transforms.
            weaponNode->local = weaponLocal;
            weaponNode->world = weaponWorld;

            if (validTree(s_cache.sourceTree) &&
                s_cache.sourceWeaponIndex >= 0 &&
                s_cache.sourceWeaponIndex < s_cache.sourceTree->numTransforms) {
                // Keep the flattened local as the native graph anchor. Only
                // its presentation world follows the counter-transformed node.
                s_cache.sourceTree->transforms[s_cache.sourceWeaponIndex].world =
                    weaponNode->world;
            }
            aimFrame.controlWeaponWorld = weaponWorld;
            aimFrame.desiredWeaponWorld = weaponWorld;
            aimFrame.controlCaptured = true;
            aimFrame.desiredCaptured = true;
            return true;
        }

        [[nodiscard]] bool tryRestoreFixedVisibleWeaponTarget(
            const RE::NiTransform& weaponWorld)
        {
#if defined(_MSC_VER)
            __try {
                return restoreFixedVisibleWeaponTarget(weaponWorld);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
#else
            return restoreFixedVisibleWeaponTarget(weaponWorld);
#endif
        }

        void clearManualCycleVisualAuthorityPreservingWeapon()
        {
            RE::NiTransform fixedWeaponWorld{};
            const bool restoreWeapon =
                manualCycleVisualAuthorityPublished() &&
                refreshFixedVisibleWeaponTarget(fixedWeaponWorld);
            clearManualCycleVisualAuthority();
            if (restoreWeapon) {
                (void)tryRestoreFixedVisibleWeaponTarget(fixedWeaponWorld);
            }
        }

        [[nodiscard]] ManualCycleHandVisualResult publishManualCycleHandVisual(
            const frik_visual_authority::Hand hand,
            const RE::NiTransform& handInWeapon,
            const frik_visual_authority::FingerLocalTransformOverride& fingerLocals,
            const RE::NiTransform& fixedWeaponWorld,
            const native_animation_authority_policy::WeaponFixedHandTargetMode targetMode)
        {
            const std::size_t handIndex = manualCycleHandIndex(hand);
            auto& publication = s_manualCycleVisualPublications[handIndex];
            auto& handRebase =
                s_sourceAimFrame.manualCycleHandRebases[handIndex];
            handRebase.targetMode = targetMode;
            if (!handRebase.captured) {
                const bool rockBaselineValid =
                    hand == frik_visual_authority::Hand::Left ?
                    s_manualCycleRockGripBaselines.authoredLeftActive :
                    s_manualCycleRockGripBaselines.rightValid;
                const RE::NiTransform& rockBaselineHandInWeapon =
                    hand == frik_visual_authority::Hand::Left ?
                    s_manualCycleRockGripBaselines.leftHandInWeapon :
                    s_manualCycleRockGripBaselines.rightHandInWeapon;
                const bool useRockBaseline = rockBaselineValid &&
                    finiteTransform(rockBaselineHandInWeapon);
                if (hand == frik_visual_authority::Hand::Right) {
                    handRebase.liveBaselineHandInWeapon =
                        native_animation_authority_policy::resolvePrimaryAnimationBaseline(
                            useRockBaseline, rockBaselineHandInWeapon, handInWeapon);
                } else if (useRockBaseline) {
                    // Use ROCK's exact requested grip target, not the solved
                    // hand-bone readback. The latter retains a small hFRIK IK
                    // residual that made cycle motion sit behind the handle.
                    handRebase.liveBaselineHandInWeapon =
                        rockBaselineHandInWeapon;
                } else {
                    const auto& presented = s_presentedGripBaselines;
                    const bool isLeft = hand == frik_visual_authority::Hand::Left;
                    if (presented.weaponGenerationKey == 0 ||
                        presented.weaponGenerationKey != s_manualCycleRockGripBaselines.weaponGenerationKey ||
                        presented.weaponNode != s_manualCycleRockGripBaselines.weaponNode ||
                        !(isLeft ? presented.authoredLeftActive : presented.rightValid)) {
                        if (!handRebase.baselineUnavailableLogged) {
                            PAPER_LOG_WARN(Animation,
                                "Native weapon-fixed hand baseline unavailable hand={} weapon={:016X}; waiting for a valid presented grip",
                                hand == frik_visual_authority::Hand::Left ? "left" : "right",
                                s_manualCycleRockGripBaselines.weaponGenerationKey);
                            handRebase.baselineUnavailableLogged = true;
                        }
                        (void)clearManualCycleVisualForHand(hand);
                        return ManualCycleHandVisualResult::Failed;
                    }
                    handRebase.liveBaselineHandInWeapon = isLeft ? presented.leftHandInWeapon : presented.rightHandInWeapon;
                }

                handRebase.nativeBaselineHandInWeapon = handInWeapon;
                if (!finiteTransform(
                        handRebase.liveBaselineHandInWeapon) ||
                    !finiteTransform(
                        handRebase.nativeBaselineHandInWeapon)) {
                    handRebase = {};
                    (void)clearManualCycleVisualForHand(hand);
                    return ManualCycleHandVisualResult::Failed;
                }
                handRebase.captured = true;
                if (s_framePartialReloadExpected) {
                    const auto& live = handRebase.liveBaselineHandInWeapon.translate;
                    const auto& native = handRebase.nativeBaselineHandInWeapon.translate;
                    PAPER_LOG_INFO(Animation,
                        "Native reload grip baseline hand={} source={} weapon={:016X} capture={} liveT=({:.3f},{:.3f},{:.3f}) nativeT=({:.3f},{:.3f},{:.3f})",
                        hand == frik_visual_authority::Hand::Left ? "left" : "right",
                        useRockBaseline ? "rock-canonical-grip" :
                            (hand == frik_visual_authority::Hand::Right ? "native-weapon-frame" : "presented-hand"),
                        s_manualCycleRockGripBaselines.weaponGenerationKey,
                        s_frameCaptureSequence,
                        live.x, live.y, live.z,
                        native.x, native.y, native.z);
                }
            }

            const auto motion = measureManualCycleHandMotion(
                handRebase.nativeBaselineHandInWeapon,
                handInWeapon);
            handRebase.motionTranslationGameUnits =
                motion.translationGameUnits;
            handRebase.motionRotationDegrees = motion.rotationDegrees;
            handRebase.resolvedHandInWeaponValid = false;
            handRebase.resolvedHandWorldValid = false;
            const bool wasMotionQualified = handRebase.motionQualified;
            handRebase.motionQualified =
                native_animation_authority_policy::
                    updateManualCycleHandMotionQualification(
                        handRebase.motionQualified,
                        motion);
            if (!handRebase.motionQualified) {
                handRebase.resolvedHandInWeapon =
                    handRebase.liveBaselineHandInWeapon;
                handRebase.resolvedHandInWeaponValid = true;
                handRebase.resolvedHandWorld =
                    transform_math::composeTransforms(
                        fixedWeaponWorld,
                        handRebase.liveBaselineHandInWeapon);
                handRebase.resolvedHandWorldValid =
                    finiteTransform(handRebase.resolvedHandWorld);
                // Leave ROCK's priority-100 grip tag as the visual owner. This
                // preserves its exact position, rotation, and finger pose
                // instead of forwarding native idle/squirm noise.
                if (!clearManualCycleVisualForHand(hand)) {
                    return ManualCycleHandVisualResult::Failed;
                }
                return ManualCycleHandVisualResult::Suppressed;
            }
            if (!wasMotionQualified) {
                PAPER_LOG_DEBUG(Animation,
                    "Native weapon-fixed hand motion qualified hand={} anchor={} translation={:.3f}gu rotation={:.2f}deg",
                    hand == frik_visual_authority::Hand::Left ? "left" : "right",
                    targetMode == native_animation_authority_policy::
                                      WeaponFixedHandTargetMode::NativeWeaponRelative ?
                        "native-weapon-relative" :
                        "live-grip-delta",
                    motion.translationGameUnits,
                    motion.rotationDegrees);
            }

            RE::NiTransform targetHandInWeapon = handInWeapon;
            if (targetMode == native_animation_authority_policy::
                                  WeaponFixedHandTargetMode::LiveGripDelta) {
                // The primary uses its canonical firing seat; an authored
                // support grip keeps its retained seat. Apply Bethesda's delta
                // without carrying an arbitrary physical primary grab offset.
                const RE::NiTransform handInWeaponCorrection =
                    native_animation_authority_policy::
                        resolveControllerAnchoredPoseCorrection(
                            handRebase.liveBaselineHandInWeapon,
                            handRebase.nativeBaselineHandInWeapon,
                            handInWeapon,
                            [](const RE::NiTransform& parent,
                               const RE::NiTransform& child) {
                                return transform_math::composeTransforms(
                                    parent,
                                    child);
                            },
                            [](const RE::NiTransform& transform) {
                                return transform_math::invertTransform(transform);
                            });
                targetHandInWeapon = transform_math::composeTransforms(
                    handInWeaponCorrection,
                    handInWeapon);
            } else if (s_manualCycleRockGripBaselines.weaponFormId == 0x0015B043) {
                const auto& primary = s_sourceAimFrame.manualCycleHandRebases[
                    manualCycleHandIndex(frik_visual_authority::Hand::Right)];
                if (!primary.captured || !s_manualCycleRockGripBaselines.rightValid) {
                    if (!handRebase.baselineUnavailableLogged) {
                        PAPER_LOG_WARN(Animation, "SMG reload model frame unavailable: no matching ROCK primary baseline");
                        handRebase.baselineUnavailableLogged = true;
                    }
                    (void)clearManualCycleVisualForHand(hand);
                    return ManualCycleHandVisualResult::Failed;
                }
                targetHandInWeapon = native_animation_authority_policy::resolveNativeWeaponRelativeHand(
                    s_manualCycleRockGripBaselines.weaponFormId, handInWeapon,
                    primary.liveBaselineHandInWeapon, primary.nativeBaselineHandInWeapon);
                if (!wasMotionQualified) {
                    const auto delta = targetHandInWeapon.translate - handInWeapon.translate;
                    PAPER_LOG_INFO(Animation, "SMG reload support model translation applied weapon={:016X} deltaT=({:.3f},{:.3f},{:.3f})",
                        s_manualCycleRockGripBaselines.weaponGenerationKey, delta.x, delta.y, delta.z);
                }
            }
            const RE::NiTransform handWorld =
                native_animation_authority_policy::resolveNativeHandWorld(
                    fixedWeaponWorld,
                    targetHandInWeapon,
                    [](const RE::NiTransform& parent,
                       const RE::NiTransform& child) {
                        return transform_math::composeTransforms(parent, child);
                    });
            if (!finiteTransform(handWorld)) {
                (void)clearManualCycleVisualForHand(hand);
                return ManualCycleHandVisualResult::Failed;
            }
            handRebase.resolvedHandInWeapon = targetHandInWeapon;
            handRebase.resolvedHandInWeaponValid = true;
            handRebase.resolvedHandWorld = handWorld;
            handRebase.resolvedHandWorldValid = true;

            if (fingerLocals.enabledMask != 0) {
                // FRIK's local-transform override augments an existing pose
                // publication under the same tag. Publishing it alone is
                // intentionally rejected, so establish the base tag before
                // attaching Bethesda's exact animated finger locals.
                if (!frik_visual_authority::setHandPoseCustomWithPriority(
                        kManualCycleVisualAuthorityTag,
                        hand,
                        frik_visual_authority::HandPoseData{},
                        kManualCycleVisualAuthorityPriority)) {
                    (void)clearManualCycleVisualForHand(hand);
                    return ManualCycleHandVisualResult::Failed;
                }
                publication.fingerPosePublished = true;
                if (!frik_visual_authority::setHandPoseCustomLocalTransformsWithPriority(
                        kManualCycleVisualAuthorityTag,
                        hand,
                        &fingerLocals,
                        kManualCycleVisualAuthorityPriority)) {
                    (void)clearManualCycleVisualForHand(hand);
                    return ManualCycleHandVisualResult::Failed;
                }
            } else if (publication.fingerPosePublished) {
                if (!frik_visual_authority::clearHandPose(
                        kManualCycleVisualAuthorityTag,
                        hand)) {
                    return ManualCycleHandVisualResult::Failed;
                }
                publication.fingerPosePublished = false;
            }

            if (!frik_visual_authority::applyExternalHandWorldTransform(
                    kManualCycleVisualAuthorityTag,
                    hand,
                    handWorld,
                    kManualCycleVisualAuthorityPriority)) {
                (void)clearManualCycleVisualForHand(hand);
                return ManualCycleHandVisualResult::Failed;
            }
            publication.worldPublished = true;
            return ManualCycleHandVisualResult::Published;
        }

        [[nodiscard]] bool applyWeaponFixedHandsPoseAfterRock()
        {
            if (!s_nativeHandPoseCapture.primaryHandValid) {
                clearManualCycleVisualAuthority();
                return false;
            }

            RE::NiTransform fixedWeaponWorld{};
            if (!refreshFixedVisibleWeaponTarget(fixedWeaponWorld)) {
                clearManualCycleVisualAuthority();
                return false;
            }

            const ManualCycleHandVisualResult primaryResult =
                publishManualCycleHandVisual(
                    frik_visual_authority::Hand::Right,
                    s_nativeHandPoseCapture.primaryHandInWeapon,
                    s_nativeHandPoseCapture.primaryFingerLocals,
                    fixedWeaponWorld,
                    native_animation_authority_policy::
                        resolveWeaponFixedHandTargetMode(
                            s_framePartialReloadExpected,
                            native_animation_authority_policy::
                                WeaponFixedHandRole::Primary,
                            s_manualCycleRockGripBaselines.authoredLeftActive));
            if (s_nativeHandPoseCapture.supportHandValid) {
                (void)publishManualCycleHandVisual(
                    frik_visual_authority::Hand::Left,
                    s_nativeHandPoseCapture.supportHandInWeapon,
                    s_nativeHandPoseCapture.supportFingerLocals,
                    fixedWeaponWorld,
                    native_animation_authority_policy::
                        resolveWeaponFixedHandTargetMode(
                            s_framePartialReloadExpected,
                            native_animation_authority_policy::
                                WeaponFixedHandRole::Support,
                            s_manualCycleRockGripBaselines.authoredLeftActive));
            } else {
                (void)clearManualCycleVisualForHand(
                    frik_visual_authority::Hand::Left);
            }

            // hFRIK's external-hand solver deliberately excludes the Weapon
            // child while moving an arm. Recompute the Weapon local anyway so
            // its scene hierarchy remains coherent with the controller-fixed
            // world before the next engine transform propagation.
            if (!restoreFixedVisibleWeaponTarget(fixedWeaponWorld)) {
                clearManualCycleVisualAuthority();
                (void)tryRestoreFixedVisibleWeaponTarget(fixedWeaponWorld);
                return false;
            }
            if (primaryResult == ManualCycleHandVisualResult::Failed) {
                clearManualCycleVisualAuthority();
                (void)tryRestoreFixedVisibleWeaponTarget(fixedWeaponWorld);
                return false;
            }

            if (!s_sourceAimFrame.manualCycleIkLogged) {
                PAPER_LOG_INFO(Animation,
                    "Native weapon-fixed hand IK ready priority={} motionGate=({:.2f}gu,{:.1f}deg) weaponT=({:.3f},{:.3f},{:.3f})",
                    kManualCycleVisualAuthorityPriority,
                    native_animation_authority_policy::
                        kManualCycleHandMotionTranslationThresholdGameUnits,
                    native_animation_authority_policy::
                        kManualCycleHandMotionRotationThresholdDegrees,
                    fixedWeaponWorld.translate.x,
                    fixedWeaponWorld.translate.y,
                    fixedWeaponWorld.translate.z);
                s_sourceAimFrame.manualCycleIkLogged = true;
            }
            return true;
        }

        [[nodiscard]] RE::NiTransform resolveControllerAimCorrection(
            const RE::NiTransform& liveControl,
            const RE::NiTransform& authoredBaseline,
            const RE::NiTransform& authoredCurrent)
        {
            return native_animation_authority_policy::resolveControllerAnchoredPoseCorrection(
                liveControl,
                authoredBaseline,
                authoredCurrent,
                [](const RE::NiTransform& parent, const RE::NiTransform& child) {
                    return transform_math::composeTransforms(parent, child);
                },
                [](const RE::NiTransform& transform) {
                    return transform_math::invertTransform(transform);
                });
        }

        [[nodiscard]] RE::NiTransform resolveWorldTargetCorrection(
            const RE::NiTransform& worldTarget,
            const RE::NiTransform& authoredCurrent)
        {
            return native_animation_authority_policy::resolvePoseCorrectionToWorldTarget(
                worldTarget,
                authoredCurrent,
                [](const RE::NiTransform& parent, const RE::NiTransform& child) {
                    return transform_math::composeTransforms(parent, child);
                },
                [](const RE::NiTransform& transform) {
                    return transform_math::invertTransform(transform);
                });
        }

        void writeCapturedLocals(BoneTree& tree, bool destination, std::uint32_t requestedFlags)
        {
            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                const auto& binding = s_cache.bindings[i];
                if (!bindingSelectedForTree(binding, destination, requestedFlags)) {
                    continue;
                }
                const int index = destination ? binding.destinationIndex : binding.sourceIndex;
                if (index < 0 || index >= tree.numTransforms) {
                    continue;
                }
                auto& transform = tree.transforms[index];
                transform.local = binding.capturedLocal;
                if (transform.refNode && !usesDifferentSceneParent(tree, index)) {
                    transform.refNode->local = binding.capturedLocal;
                }
            }
        }

        void propagateSelectedRoots(BoneTree& tree, bool destination, std::uint32_t requestedFlags)
        {
            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                const auto& binding = s_cache.bindings[i];
                if (!bindingSelectedForTree(binding, destination, requestedFlags)) {
                    continue;
                }
                const int index = destination ? binding.destinationIndex : binding.sourceIndex;
                if (index < 0 || index >= tree.numTransforms) {
                    continue;
                }
                auto& transform = tree.transforms[index];
                if (transform.refNode &&
                    !usesDifferentSceneParent(tree, index) &&
                    !parentIsSelected(transform.parPos, destination, requestedFlags)) {
                    f4vr::updateTransformsDown(transform.refNode, true);
                }
            }
        }

        [[nodiscard]] bool applyControllerAimFrame(
            BoneTree& tree,
            bool destination,
            std::uint32_t requestedFlags)
        {
            auto& aimFrame = s_sourceAimFrame;
            const int weaponIndex = destination ? s_cache.destinationWeaponIndex : s_cache.sourceWeaponIndex;
            if (!aimFrame.controlCaptured || (destination && !aimFrame.desiredCaptured)) {
                return false;
            }

            RE::NiTransform nativeWeaponWorld{};
            if (!readLogicalWorld(tree, weaponIndex, nativeWeaponWorld)) {
                return false;
            }
            const bool weaponTransformRequested =
                (requestedFlags & native_animation_authority_policy::kWeapon) != 0;
            if (!weaponTransformRequested) {
                return false;
            }
            const bool initializedBaseline =
                !destination &&
                !aimFrame.nativeBaselineCaptured;
            RE::NiTransform correction{};
            if (!destination) {
                if (initializedBaseline) {
                    aimFrame.nativeBaselineWeaponWorld = nativeWeaponWorld;
                    aimFrame.nativeBaselineCaptured = true;
                }
                correction = resolveControllerAimCorrection(
                    aimFrame.controlWeaponWorld,
                    aimFrame.nativeBaselineWeaponWorld,
                    nativeWeaponWorld);
                aimFrame.desiredWeaponWorld = transform_math::composeTransforms(
                    correction,
                    nativeWeaponWorld);
                aimFrame.desiredCaptured = finiteTransform(aimFrame.desiredWeaponWorld);
                if (!aimFrame.desiredCaptured) {
                    return false;
                }
            } else {
                // The destination tree can be in a completely different world
                // basis. Resolve its own native Weapon to the source tree's
                // visible-gun target instead of reusing a source-space matrix.
                correction = resolveWorldTargetCorrection(
                    aimFrame.desiredWeaponWorld,
                    nativeWeaponWorld);
            }
            if (!finiteTransform(correction)) {
                return false;
            }

            struct RootTarget
            {
                int index{ -1 };
                RE::NiTransform local{};
                RE::NiTransform world{};
            };
            std::array<RootTarget, kMaxBindings> roots{};
            std::size_t rootCount = 0;

            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                const auto& binding = s_cache.bindings[i];
                if (!bindingSelectedForTree(binding, destination, requestedFlags)) {
                    continue;
                }
                const int index = destination ? binding.destinationIndex : binding.sourceIndex;
                if (index < 0 || index >= tree.numTransforms) {
                    return false;
                }
                const auto& transform = tree.transforms[index];
                if (parentIsSelected(transform.parPos, destination, requestedFlags)) {
                    continue;
                }
                if (rootCount >= roots.size()) {
                    return false;
                }

                RE::NiTransform nativeRootWorld{};
                if (!readLogicalWorld(tree, index, nativeRootWorld)) {
                    return false;
                }
                auto& target = roots[rootCount++];
                target.index = index;
                target.world = transform_math::composeTransforms(correction, nativeRootWorld);
                if (!finiteTransform(target.world)) {
                    return false;
                }

                if (transform.parPos >= 0 && transform.parPos < tree.numTransforms) {
                    const auto& logicalParent = tree.transforms[transform.parPos];
                    const RE::NiTransform& parentWorld = logicalParent.refNode ? logicalParent.refNode->world : logicalParent.world;
                    if (!finiteTransform(parentWorld)) {
                        return false;
                    }
                    target.local = transform_math::composeTransforms(
                        transform_math::invertTransform(parentWorld),
                        target.world);
                } else {
                    target.local = target.world;
                }
                if (!finiteTransform(target.local)) {
                    return false;
                }
            }

            if (rootCount == 0) {
                return false;
            }
            for (std::size_t i = 0; i < rootCount; ++i) {
                const auto& target = roots[i];
                auto& transform = tree.transforms[target.index];
                transform.local = target.local;
                if (transform.refNode && !usesDifferentSceneParent(tree, target.index)) {
                    transform.refNode->local = target.local;
                    f4vr::updateTransformsDown(transform.refNode, true);
                    transform.world = transform.refNode->world;
                } else {
                    transform.world = target.world;
                }
            }

            if (initializedBaseline) {
                PAPER_LOG_INFO(Animation,
                    "Native animation shared weapon frame ready tree=first-person roots={} controlWeaponT=({:.3f},{:.3f},{:.3f}) nativeWeaponT=({:.3f},{:.3f},{:.3f}) sharedWeaponT=({:.3f},{:.3f},{:.3f})",
                    rootCount,
                    aimFrame.controlWeaponWorld.translate.x,
                    aimFrame.controlWeaponWorld.translate.y,
                    aimFrame.controlWeaponWorld.translate.z,
                    nativeWeaponWorld.translate.x,
                    nativeWeaponWorld.translate.y,
                    nativeWeaponWorld.translate.z,
                    aimFrame.desiredWeaponWorld.translate.x,
                    aimFrame.desiredWeaponWorld.translate.y,
                    aimFrame.desiredWeaponWorld.translate.z);
            } else if (destination && !aimFrame.destinationAlignmentLogged) {
                const RE::NiTransform resolvedWeaponWorld = transform_math::composeTransforms(
                    correction,
                    nativeWeaponWorld);
                PAPER_LOG_INFO(Animation,
                    "Native animation shared weapon frame applied tree=full-body roots={} nativeWeaponT=({:.3f},{:.3f},{:.3f}) sharedWeaponT=({:.3f},{:.3f},{:.3f}) resolvedWeaponT=({:.3f},{:.3f},{:.3f})",
                    rootCount,
                    nativeWeaponWorld.translate.x,
                    nativeWeaponWorld.translate.y,
                    nativeWeaponWorld.translate.z,
                    aimFrame.desiredWeaponWorld.translate.x,
                    aimFrame.desiredWeaponWorld.translate.y,
                    aimFrame.desiredWeaponWorld.translate.z,
                    resolvedWeaponWorld.translate.x,
                    resolvedWeaponWorld.translate.y,
                    resolvedWeaponWorld.translate.z);
                aimFrame.destinationAlignmentLogged = true;
            }
            return true;
        }

        void synchronizeFlattenedWorlds(BoneTree& tree, bool destination, std::uint32_t requestedFlags)
        {
            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                const auto& binding = s_cache.bindings[i];
                if (!bindingSelectedForTree(binding, destination, requestedFlags)) {
                    continue;
                }
                const int index = destination ? binding.destinationIndex : binding.sourceIndex;
                if (index < 0 || index >= tree.numTransforms) {
                    continue;
                }

                auto& transform = tree.transforms[index];
                if (transform.refNode && !usesDifferentSceneParent(tree, index)) {
                    transform.world = transform.refNode->world;
                } else if (!transform.refNode && transform.parPos >= 0 && transform.parPos < tree.numTransforms) {
                    transform.world = transform_math::composeTransforms(
                        tree.transforms[transform.parPos].world,
                        transform.local);
                } else if (!transform.refNode) {
                    transform.world = transform.local;
                }
            }
        }

        void rebaseDifferentSceneParents(BoneTree& tree, bool destination, std::uint32_t requestedFlags)
        {
            for (std::size_t i = 0; i < s_cache.bindingCount; ++i) {
                const auto& binding = s_cache.bindings[i];
                if (!bindingSelectedForTree(binding, destination, requestedFlags)) {
                    continue;
                }
                const int index = destination ? binding.destinationIndex : binding.sourceIndex;
                if (index < 0 || index >= tree.numTransforms || !usesDifferentSceneParent(tree, index)) {
                    continue;
                }

                auto& transform = tree.transforms[index];
                auto* refNode = transform.refNode;
                const auto& logicalParent = tree.transforms[transform.parPos];
                const RE::NiTransform& logicalParentWorld = logicalParent.refNode ? logicalParent.refNode->world : logicalParent.world;
                const RE::NiTransform logicalTargetWorld = transform_math::composeTransforms(
                    logicalParentWorld,
                    transform.local);

                if (refNode->parent) {
                    refNode->local = transform_math::composeTransforms(
                        transform_math::invertTransform(refNode->parent->world),
                        logicalTargetWorld);
                } else {
                    refNode->local = logicalTargetWorld;
                }
                f4vr::updateTransformsDown(refNode, true);
                transform.world = refNode->world;
            }
        }

        [[nodiscard]] bool applyToTree(BoneTree* tree, bool destination, std::uint32_t requestedFlags)
        {
            if (!validTree(tree)) {
                return false;
            }
            const bool identityMatches = destination ?
                tree == s_cache.destinationTree && tree->transforms == s_cache.destinationTransforms && tree->numTransforms == s_cache.destinationCount :
                tree == s_cache.sourceTree && tree->transforms == s_cache.sourceTransforms && tree->numTransforms == s_cache.sourceCount;
            if (!identityMatches) {
                return false;
            }

            writeCapturedLocals(*tree, destination, requestedFlags);
            propagateSelectedRoots(*tree, destination, requestedFlags);
            synchronizeFlattenedWorlds(*tree, destination, requestedFlags);
            if (!applyControllerAimFrame(*tree, destination, requestedFlags)) {
                return false;
            }
            synchronizeFlattenedWorlds(*tree, destination, requestedFlags);
            // hFRIK may retain Weapon/WeaponLeft under a wand node. Preserve
            // the corrected logical pose, but convert its target world into
            // that live scene parent's local before presentation.
            rebaseDifferentSceneParents(*tree, destination, requestedFlags);
            return true;
        }

        [[nodiscard]] const char* localReloadLeaseEndReasonName(
            native_animation_authority_policy::LocalReloadLeaseEndReason reason)
        {
            using Reason = native_animation_authority_policy::LocalReloadLeaseEndReason;
            switch (reason) {
            case Reason::ReloadEnded:
                return "native reload-end event observed";
            case Reason::WatchdogExpired:
                return "watchdog expired";
            case Reason::None:
            default:
                return "active";
            }
        }

        [[nodiscard]] bool refreshLocalReloadTestLease()
        {
            const auto requestSequence = s_localReloadTestRequestSequence.load(std::memory_order_acquire);
            const bool requestChanged = requestSequence != s_seenLocalReloadTestRequestSequence;
            if (requestChanged) {
                s_seenLocalReloadTestRequestSequence = requestSequence;
                s_localReloadLeaseState = native_animation_authority_policy::LocalReloadLeaseState{
                    .watchdogFramesRemaining = s_localReloadTestLeaseFrames.load(std::memory_order_acquire),
                    .startSequenceAtArm = s_playerReloadStartSequence.load(std::memory_order_acquire),
                    .endSequenceAtArm = s_playerReloadEndSequence.load(std::memory_order_acquire),
                    .observedReloadStart = s_playerReloadEventActive.load(std::memory_order_acquire),
                };
            }

            if (s_localReloadTestLeaseFrames.load(std::memory_order_acquire) == 0) {
                return requestChanged;
            }

            const bool wasReloadStartObserved = s_localReloadLeaseState.observedReloadStart;
            const auto step = native_animation_authority_policy::advanceLocalReloadLease(
                s_localReloadLeaseState,
                native_animation_authority_policy::LocalReloadLifecycleSignal{
                    .startSequence = s_playerReloadStartSequence.load(std::memory_order_acquire),
                    .endSequence = s_playerReloadEndSequence.load(std::memory_order_acquire),
                    .reloadActive = s_playerReloadEventActive.load(std::memory_order_acquire),
                });
            s_localReloadLeaseState = step.state;
            if (!wasReloadStartObserved && step.state.observedReloadStart) {
                PAPER_LOG_INFO(Animation,
                    "Native reload animation authority observed Bethesda's player reload-start event; exact-end return armed");
            }

            if (step.active()) {
                s_localReloadTestLeaseFrames.store(step.state.watchdogFramesRemaining, std::memory_order_release);
            } else {
                s_localReloadTestLeaseFrames.store(0, std::memory_order_release);
                PAPER_LOG_INFO(Animation,
                    "Native reload animation authority local test lease released: {}",
                    localReloadLeaseEndReasonName(step.endReason));
            }
            return requestChanged;
        }

        [[nodiscard]] const char* localManualCycleLeaseEndReasonName(
            native_animation_authority_policy::LocalManualCycleLeaseEndReason reason)
        {
            using Reason = native_animation_authority_policy::LocalManualCycleLeaseEndReason;
            switch (reason) {
            case Reason::CycleBracketEnded:
                return "native manual-cycle clip end observed";
            case Reason::ReloadStarted:
                return "native reload-start event preempted cycle";
            case Reason::WatchdogExpired:
                return "animation-duration watchdog expired";
            case Reason::None:
            default:
                return "active";
            }
        }

        [[nodiscard]] bool refreshLocalManualCycleTestLease(const float deltaSeconds)
        {
            const auto requestSequence =
                s_localManualCycleTestRequestSequence.load(std::memory_order_acquire);
            const bool requestChanged =
                requestSequence != s_seenLocalManualCycleTestRequestSequence;
            if (requestChanged) {
                s_seenLocalManualCycleTestRequestSequence = requestSequence;
                const auto watchdogMilliseconds =
                    s_localManualCycleRequestedWatchdogMilliseconds.load(
                        std::memory_order_acquire);
                s_localManualCycleLeaseState =
                    native_animation_authority_policy::LocalManualCycleLeaseState{
                        .watchdogSecondsRemaining =
                            static_cast<float>(watchdogMilliseconds) / 1000.0f,
                        .reloadStartSequenceAtArm =
                            s_localManualCycleReloadStartSequenceAtArm.load(
                                std::memory_order_acquire),
                        .lastReloadEndSequence =
                            s_localManualCycleReloadEndSequenceAtArm.load(
                                std::memory_order_acquire),
                    };

                const bool leaseActive =
                    s_localManualCycleTestLeaseActive.load(
                        std::memory_order_acquire);
                const auto latchState =
                    native_animation_authority_policy::
                        advanceManualCycleAuthoredSupportGripLatch(
                            {},
                            native_animation_authority_policy::
                                ManualCycleAuthoredSupportGripLatchObservation{
                                    .observedWeaponGenerationKey =
                                        s_latestManualCycleRockGripBaselines.
                                            weaponGenerationKey,
                                    .leaseActive = leaseActive,
                                    .leaseStarted = true,
                                    .authoredSupportGripActive =
                                        s_latestManualCycleRockGripBaselines.
                                            authoredLeftActive,
                                });
                s_manualCycleAuthoredSupportGripLatch = {};
                if (latchState.active) {
                    s_manualCycleAuthoredSupportGripLatch.handInWeapon =
                        s_latestManualCycleRockGripBaselines.
                            leftHandInWeapon;
                    s_manualCycleAuthoredSupportGripLatch.state =
                        latchState;
                }
                refreshEffectiveManualCycleRockGripBaselines(leaseActive);
            }

            if (!s_localManualCycleTestLeaseActive.load(std::memory_order_acquire)) {
                s_manualCycleAuthoredSupportGripLatch = {};
                refreshEffectiveManualCycleRockGripBaselines(false);
                return requestChanged;
            }

            const auto step =
                native_animation_authority_policy::advanceLocalManualCycleLease(
                    s_localManualCycleLeaseState,
                    native_animation_authority_policy::LocalManualCycleLifecycleSignal{
                        .reloadStartSequence =
                            s_playerReloadStartSequence.load(std::memory_order_acquire),
                        .reloadEndSequence =
                            s_playerReloadEndSequence.load(std::memory_order_acquire),
                        .deltaSeconds = deltaSeconds,
                    });
            s_localManualCycleLeaseState = step.state;
            if (!step.active()) {
                s_localManualCycleTestLeaseActive.store(false, std::memory_order_release);
                s_manualCycleAuthoredSupportGripLatch = {};
                refreshEffectiveManualCycleRockGripBaselines(false);
                PAPER_LOG_DEBUG(Animation,
                    "Native manual-cycle hand-only authority released: {}",
                    localManualCycleLeaseEndReasonName(step.endReason));
            }
            return requestChanged;
        }

        [[nodiscard]] bool installWeaponFireHook()
        {
            if (s_weaponFireHookInstalled.load(std::memory_order_acquire)) {
                return s_originalWeaponFire != nullptr;
            }
            if (s_weaponFireHookInstallFailed.load(std::memory_order_acquire)) {
                return false;
            }

            REL::Relocation<std::uintptr_t> entry{
                REL::Offset(offsets::kVtableEntry_WeaponFireHandler_Handle)
            };
            REL::Relocation<std::uintptr_t> expectedTarget{
                REL::Offset(offsets::kFunc_WeaponFireHandler_Handle)
            };
            auto* slot = reinterpret_cast<std::uintptr_t*>(entry.address());
            if (!slot || *slot != expectedTarget.address()) {
                PAPER_LOG_ERROR(Init,
                    "WeaponFireHandler hook validation failed at 0x{:X}; expected target 0x{:X}, found 0x{:X}",
                    entry.address(),
                    expectedTarget.address(),
                    slot ? *slot : 0);
                s_weaponFireHookInstallFailed.store(true, std::memory_order_release);
                return false;
            }

            s_originalWeaponFire = reinterpret_cast<WeaponFireHandlerFn>(*slot);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                PAPER_LOG_ERROR(Init,
                    "WeaponFireHandler hook install failed at 0x{:X}: VirtualProtect failed",
                    entry.address());
                s_originalWeaponFire = nullptr;
                s_weaponFireHookInstallFailed.store(true, std::memory_order_release);
                return false;
            }

            *slot = reinterpret_cast<std::uintptr_t>(&onWeaponFire);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
            DWORD unusedProtect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), oldProtect, &unusedProtect)) {
                PAPER_LOG_WARN(Init,
                    "WeaponFireHandler hook installed at 0x{:X}, but restoring page protection failed",
                    entry.address());
            }

            s_weaponFireHookInstalled.store(true, std::memory_order_release);
            PAPER_LOG_INFO(Init,
                "Installed validated WeaponFireHandler manual-cycle hook at 0x{:X}, original=0x{:X}",
                entry.address(),
                reinterpret_cast<std::uintptr_t>(s_originalWeaponFire));
            return true;
        }

        [[nodiscard]] bool installReloadStateChangeHook()
        {
            if (s_reloadStateHookInstalled.load(std::memory_order_acquire)) {
                return s_originalReloadStateChange != nullptr;
            }

            REL::Relocation<std::uintptr_t> entry{
                REL::Offset(offsets::kVtableEntry_ReloadStateChangeHandler_Handle)
            };
            REL::Relocation<std::uintptr_t> expectedTarget{
                REL::Offset(offsets::kFunc_ReloadStateChangeHandler_Handle)
            };
            auto* slot = reinterpret_cast<std::uintptr_t*>(entry.address());
            if (!slot || *slot != expectedTarget.address()) {
                PAPER_LOG_ERROR(Init,
                    "ReloadStateChangeHandler hook validation failed at 0x{:X}; expected target 0x{:X}, found 0x{:X}",
                    entry.address(),
                    expectedTarget.address(),
                    slot ? *slot : 0);
                return false;
            }

            s_originalReloadStateChange = reinterpret_cast<ReloadStateChangeHandlerFn>(*slot);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                PAPER_LOG_ERROR(Init,
                    "ReloadStateChangeHandler hook install failed at 0x{:X}: VirtualProtect failed",
                    entry.address());
                s_originalReloadStateChange = nullptr;
                return false;
            }

            *slot = reinterpret_cast<std::uintptr_t>(&onReloadStateChange);
            FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
            DWORD unusedProtect = 0;
            if (!VirtualProtect(slot, sizeof(*slot), oldProtect, &unusedProtect)) {
                PAPER_LOG_WARN(Init,
                    "ReloadStateChangeHandler hook installed at 0x{:X}, but restoring page protection failed",
                    entry.address());
            }

            s_reloadStateHookInstalled.store(true, std::memory_order_release);
            PAPER_LOG_INFO(Init,
                "Installed validated ReloadStateChangeHandler lifecycle hook at 0x{:X}, original=0x{:X}",
                entry.address(),
                reinterpret_cast<std::uintptr_t>(s_originalReloadStateChange));
            return true;
        }

    }

    bool installEventHooks()
    {
        if (s_hookInstalled.load(std::memory_order_acquire)) {
            if (!s_weaponFireHookInstalled.load(std::memory_order_acquire) &&
                !s_weaponFireHookInstallFailed.load(std::memory_order_acquire)) {
                if (!installWeaponFireHook()) {
                    PAPER_LOG_WARN(Init,
                        "Native reload authority remains available, but manual-cycle hand-only animation is disabled because WeaponFireHandler was not installed");
                }
            }
            return true;
        }
        if (s_hookInstallFailed.load(std::memory_order_acquire)) {
            return false;
        }
        if (!installReloadStateChangeHook()) {
            s_hookInstallFailed.store(true, std::memory_order_release);
            return false;
        }
        s_hookInstalled.store(true, std::memory_order_release);
        if (!installWeaponFireHook()) {
            PAPER_LOG_WARN(Init,
                "Native reload authority remains available, but manual-cycle hand-only animation is disabled because WeaponFireHandler was not installed");
        }
        PAPER_LOG_INFO(Init,
            "Native animation lifecycle hooks ready; graph capture=ROCK V1 NativeGraphOutput scope=arms,hands,Weapon/WeaponLeft lifecycle=WeaponFireHandler+ReloadStateChangeHandler");
        return true;
    }

    void captureNativeGraphOutput()
    {
#if defined(_MSC_VER)
        __try {
            captureNativePose();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_captureFault.store(true, std::memory_order_release);
            invalidateCapture();
        }
        __try {
            captureCycleTracePose();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // A diagnostic read failure must not invalidate the normal pose.
            if (s_cycleTrace) {
                s_cycleTrace->nativePose = {};
                s_cycleTrace->failed.store(true, std::memory_order_relaxed);
            }
        }
#else
        captureNativePose();
        captureCycleTracePose();
#endif
    }

    void setRuntimeEnabled(const bool enabled)
    {
        s_runtimeEnabled.store(enabled && s_hookInstalled.load(std::memory_order_acquire), std::memory_order_release);
        if (!enabled) {
            s_manualCycleHandAnimationEligible.store(false, std::memory_order_release);
            clearManualCycleRockGripState();
            cancelLocalManualCycleTestLease();
            const DWORD ownerThread =
                s_ownerThreadId.load(std::memory_order_acquire);
            if (ownerThread == 0 || ownerThread == GetCurrentThreadId()) {
                clearManualCycleVisualAuthorityPreservingWeapon();
            }
            invalidateCapture();
            s_frameCaptureReady = false;
            resetHybridPoseState();
        }
    }

    void setLocalManualCycleTestEnabled(const bool enabled)
    {
        const bool effectiveEnabled = enabled &&
            s_hookInstalled.load(std::memory_order_acquire) &&
            s_weaponFireHookInstalled.load(std::memory_order_acquire);
        s_localManualCycleTestEnabled.store(effectiveEnabled, std::memory_order_release);
        if (!effectiveEnabled) {
            clearManualCycleRockGripState();
            cancelLocalManualCycleTestLease();
        }
    }

    void setLocalReloadTestEnabled(const bool enabled)
    {
        const bool effectiveEnabled = enabled &&
            s_hookInstalled.load(std::memory_order_acquire) &&
            s_reloadStateHookInstalled.load(std::memory_order_acquire);
        const bool wasEnabled = s_localReloadTestEnabled.exchange(
            effectiveEnabled,
            std::memory_order_acq_rel);
        if (!effectiveEnabled && wasEnabled) {
            s_localReloadTestLeaseFrames.store(0, std::memory_order_release);
        }
    }

    void setManualCycleHandAnimationEligible(const bool eligible)
    {
        const bool effectiveEligibility = eligible &&
            s_runtimeEnabled.load(std::memory_order_acquire) &&
            s_localManualCycleTestEnabled.load(std::memory_order_acquire);
        const bool wasEligible = s_manualCycleHandAnimationEligible.exchange(
            effectiveEligibility,
            std::memory_order_acq_rel);
        if (!effectiveEligibility) {
            clearManualCycleRockGripState();
        }
        if (wasEligible && !effectiveEligibility &&
            s_localManualCycleTestLeaseActive.load(std::memory_order_acquire)) {
            cancelLocalManualCycleTestLease();
            PAPER_LOG_DEBUG(Animation,
                "Native manual-cycle hand-only authority released: equipped-weapon animation eligibility lost");
        }
    }

    void setManualCycleRockGripSnapshot(
        const ManualCycleRockGripSnapshot& snapshot)
    {
        ResolvedManualCycleRockGripBaselines resolved{};
        resolved.weaponNode = snapshot.weaponNode;
        resolved.weaponGenerationKey = snapshot.weaponGenerationKey;
        resolved.weaponFormId = snapshot.weaponFormId;
        if (s_manualCycleHandAnimationEligible.load(
                std::memory_order_acquire)) {
            if (snapshot.rightValid &&
                finiteTransform(snapshot.rightHandInWeapon)) {
                resolved.rightHandInWeapon = snapshot.rightHandInWeapon;
                resolved.rightValid = true;
            }
            if (snapshot.leftSupportGripValid &&
                snapshot.authoredLeftSupportGripActive &&
                finiteTransform(snapshot.leftSupportHandInWeapon)) {
                resolved.leftHandInWeapon =
                    snapshot.leftSupportHandInWeapon;
                resolved.authoredLeftActive = true;
            }
        }

        s_latestManualCycleRockGripBaselines = resolved;
        const bool manualCycleLeaseActive =
            s_localManualCycleTestLeaseActive.load(
                std::memory_order_acquire);
        if (manualCycleLeaseActive) {
            const bool activeNonAuthoredGripObserved =
                snapshot.leftPartGripStateValid &&
                snapshot.leftPartGripActive &&
                (!snapshot.leftSupportGripValid ||
                    !snapshot.authoredLeftSupportGripActive);
            const auto latchState =
                native_animation_authority_policy::
                    advanceManualCycleAuthoredSupportGripLatch(
                        s_manualCycleAuthoredSupportGripLatch.state,
                        native_animation_authority_policy::
                            ManualCycleAuthoredSupportGripLatchObservation{
                                .observedWeaponGenerationKey =
                                    resolved.weaponGenerationKey,
                                .leaseActive = true,
                                .activeNonAuthoredGripObserved =
                                    activeNonAuthoredGripObserved,
                            });
            if (latchState.active) {
                s_manualCycleAuthoredSupportGripLatch.state =
                    latchState;
            } else {
                s_manualCycleAuthoredSupportGripLatch = {};
            }
        } else {
            s_manualCycleAuthoredSupportGripLatch = {};
        }
        refreshEffectiveManualCycleRockGripBaselines(
            manualCycleLeaseActive);
    }

    void beginRockFrame(const float deltaSeconds)
    {
        const bool localReloadRequestChanged = refreshLocalReloadTestLease();
        const bool localManualCycleRequestChanged =
            refreshLocalManualCycleTestLease(deltaSeconds);
        s_frameCaptureReady = false;
        s_frameCapturePrepared = false;
        s_frameCaptureFlags = 0;
        s_frameCaptureSequence = 0;
        s_frameWeaponFixedHandsExpected = false;
        s_framePartialReloadExpected = false;
        s_frameWeaponFixedHandsApplied = false;
        s_frameWeaponFixedHandsCleanupPending = false;
        s_beforeRockApplicationAttempted = false;
        s_beforeRockApplicationSucceeded = false;
        s_afterRockApplicationAttempted = false;
        s_afterRockApplicationSucceeded = false;

        const std::uint32_t currentFlags = effectiveRequestedFlags();
        const std::uint32_t previousFlags = s_lastLoggedEffectiveFlags;
        const bool weaponFixedHandsRequested = currentFlags != 0 &&
            (currentFlags & native_animation_authority_policy::kWeapon) == 0 &&
            (currentFlags & native_animation_authority_policy::kArms) != 0;
        if (!weaponFixedHandsRequested && currentFlags != 0) {
            // Full reload authority is taking over and will immediately write
            // its own pose in both ROCK phases, so release a preceding weapon-
            // fixed hand overlay before that composition is applied.
            clearManualCycleVisualAuthorityPreservingWeapon();
        } else if (!weaponFixedHandsRequested) {
            // With no replacement pose, retain the selected weapon-fixed tags until
            // ROCK has refreshed its normal grip targets. Releasing them here
            // would make hFRIK apply the previous frame's lower-priority pose
            // immediately before ROCK samples the controller-driven hands.
            s_frameWeaponFixedHandsCleanupPending =
                manualCycleVisualAuthorityPublished();
        } else {
            // hFRIK has already restored the scene arms from the controllers.
            // Keep ROCK's higher-priority weapon-fixed tags selected while the
            // normal weapon/grip update advances its lower-priority targets
            // underneath, whether this is a one- or two-hand reload/cycle.
            // Clearing here re-selects those previous-frame targets and feeds
            // a stale hand/weapon pose back into the current weapon solve.
        }
        if (localReloadRequestChanged || localManualCycleRequestChanged ||
            currentFlags != previousFlags) {
            resetHybridPoseState();
        }
        s_frameWeaponFixedHandsExpected = weaponFixedHandsRequested;
        s_framePartialReloadExpected =
            weaponFixedHandsRequested && localReloadLeaseActive();
        if (currentFlags != s_lastLoggedEffectiveFlags) {
            const char* composition = s_frameWeaponFixedHandsExpected ?
                "post-rock-weapon-anchored-hand-ik" :
                "visible-weapon-shared-rigid-pose";
            PAPER_LOG_INFO(Animation,
                "Native animation authority {} flags=0x{:X} composition={}",
                currentFlags != 0 ? "enabled" : "disabled",
                currentFlags,
                composition);
            s_lastLoggedEffectiveFlags = currentFlags;
        }

        if (currentFlags == 0 || !s_captureValid.load(std::memory_order_acquire) || !claimOrValidateThread()) {
            return;
        }

        const auto sequence = s_captureSequence.load(std::memory_order_acquire);
        const auto capturedFlags = s_capturedFlags.load(std::memory_order_acquire) & currentFlags;
        if (sequence == 0 || sequence == s_lastCompletedCaptureSequence || capturedFlags == 0) {
            return;
        }
        const bool framePrepared = s_frameWeaponFixedHandsExpected ?
            prepareWeaponFixedHandsWeaponNode() :
            prepareControllerAimFrames();
        if (!framePrepared) {
            invalidateCapture();
            return;
        }

        s_frameCaptureSequence = sequence;
        s_frameCaptureFlags = capturedFlags;
        s_frameCapturePrepared = true;
        s_frameCaptureReady = true;
    }

    bool applyCapturedPose(const ApplyPhase phase)
    {
        bool& attempted = phase == ApplyPhase::BeforeRock ?
            s_beforeRockApplicationAttempted :
            s_afterRockApplicationAttempted;
        bool& succeeded = phase == ApplyPhase::BeforeRock ?
            s_beforeRockApplicationSucceeded :
            s_afterRockApplicationSucceeded;
        attempted = true;
        succeeded = false;
        const auto finishApplication = [&](const bool result) {
            succeeded = result;
            return result;
        };
        if (!s_frameCaptureReady || s_frameCaptureFlags == 0 || !claimOrValidateThread()) {
            return finishApplication(false);
        }

        if (s_frameWeaponFixedHandsExpected) {
            // ROCK's collision/grab/two-hand pass must see only controller-
            // tracked hands. Publishing the animated IK target before that
            // pass feeds the clip back into the weapon solve and rotates the
            // gun away from the controllers.
            if (phase == ApplyPhase::BeforeRock) {
                return finishApplication(true);
            }

            const std::uint32_t currentFlags = effectiveRequestedFlags();
            const bool weaponFixedHandsStillRequested = currentFlags != 0 &&
                (currentFlags & native_animation_authority_policy::kWeapon) == 0 &&
                (currentFlags & native_animation_authority_policy::kArms) != 0;
            if (!weaponFixedHandsStillRequested) {
                // A cycle can lose its equipped-weapon eligibility, or a reload
                // can end, during ROCK's own update. Drop the native overlay
                // only afterward, when normal grip tags hold current-frame
                // controller/weapon targets.
                clearManualCycleVisualAuthorityPreservingWeapon();
                invalidateCapture();
                s_frameCaptureReady = false;
                s_frameWeaponFixedHandsApplied = true;
                return finishApplication(false);
            }

            bool applied = false;
#if defined(_MSC_VER)
            __try {
                applied = applyWeaponFixedHandsPoseAfterRock();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                clearManualCycleVisualAuthority();
                s_captureFault.store(true, std::memory_order_release);
            }
#else
            applied = applyWeaponFixedHandsPoseAfterRock();
#endif
            if (!applied) {
                s_frameCaptureReady = false;
                invalidateCapture();
                return finishApplication(false);
            }
            s_frameWeaponFixedHandsApplied = true;
            return finishApplication(true);
        }

        bool sourceApplied = false;
        bool destinationApplied = false;
#if defined(_MSC_VER)
        __try {
            sourceApplied = applyToTree(
                s_cache.sourceTree,
                false,
                s_frameCaptureFlags);
            destinationApplied =
                s_cache.destinationTree == s_cache.sourceTree ?
                sourceApplied :
                sourceApplied && applyToTree(
                    s_cache.destinationTree,
                    true,
                    s_frameCaptureFlags);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_captureFault.store(true, std::memory_order_release);
            invalidateCapture();
            s_frameCaptureReady = false;
            return finishApplication(false);
        }
#else
        sourceApplied = applyToTree(
            s_cache.sourceTree,
            false,
            s_frameCaptureFlags);
        destinationApplied =
            s_cache.destinationTree == s_cache.sourceTree ?
            sourceApplied :
            sourceApplied && applyToTree(
                s_cache.destinationTree,
                true,
                s_frameCaptureFlags);
#endif
        if (!sourceApplied || !destinationApplied) {
            s_frameCaptureReady = false;
            invalidateCapture();
            return finishApplication(false);
        }
        return finishApplication(true);
    }

    void initializeCycleTrace()
    {
        if (s_cycleTrace || !logger::instance ||
            !logger::instance->should_log(spdlog::level::debug)) return;
        try {
            auto trace = std::make_unique<CycleTrace>();
            trace->pool = std::make_shared<spdlog::details::thread_pool>(1024, 1);
            trace->log = std::make_shared<spdlog::async_logger>(
                "PAPER_CycleTrace", logger::instance->sinks().begin(),
                logger::instance->sinks().end(), trace->pool,
                spdlog::async_overflow_policy::overrun_oldest);
            trace->log->set_level(spdlog::level::debug);
            trace->log->set_error_handler([state = trace.get()](const std::string&) {
                state->failed.store(true, std::memory_order_relaxed);
            });
            trace->log->info("CYCLE_TRACE start version=2 pid={} build={} {} weapons=all hands=both activeStride=3 idleStride=90 phase=complete-before-final-presentation matrices=Ni-stored-rows",
                GetCurrentProcessId(), __DATE__, __TIME__);
            s_cycleTrace = std::move(trace);
        } catch (const std::exception& error) {
            PAPER_LOG_ERROR(Animation, "Cycle trace initialization failed: {}", error.what());
        }
    }

    void capturePresentedHandBaselines(std::uint64_t frameIndex)
    {
        s_presentedGripBaselines = {};
        const auto& identity = s_latestManualCycleRockGripBaselines;
        auto* weapon = s_sourceAimFrame.weaponNode;
        if (!weapon || reinterpret_cast<std::uintptr_t>(weapon) != identity.weaponNode ||
            identity.weaponGenerationKey == 0 || !weapon->parent || !finiteTransform(weapon->world)) return;
        const auto inverseWeapon = transform_math::invertTransform(weapon->world);
        auto& captured = s_presentedGripBaselines;
        captured.weaponNode = identity.weaponNode;
        captured.weaponGenerationKey = identity.weaponGenerationKey;
        captured.weaponFormId = identity.weaponFormId;
        for (const bool left : { false, true }) {
            rock::api::hands::PresentedHandPoseV1 pose{};
            if (!rockApiClient().queryPresentedHandPose(left ? rock::api::Hand::Left : rock::api::Hand::Right, pose) ||
                pose.frameIndex != frameIndex || pose.presentationSequence != frameIndex) continue;
            const auto local = transform_math::composeTransforms(inverseWeapon, api_transform::toNi(pose.handWorld));
            if (!finiteTransform(local)) continue;
            (left ? captured.leftHandInWeapon : captured.rightHandInWeapon) = local;
            (left ? captured.authoredLeftActive : captured.rightValid) = true;
        }
    }

    void completeRockFrame(std::uint64_t frameIndex)
    {
        if (s_cycleTrace && s_cycleTrace->failed.load(std::memory_order_relaxed) &&
            !s_cycleTrace->failureReported) {
            s_cycleTrace->failureReported = true;
            PAPER_LOG_ERROR(Animation, "Cycle trace failed; further samples disabled");
        }
        if ((s_frameWeaponFixedHandsExpected && !s_frameWeaponFixedHandsApplied) ||
            s_frameWeaponFixedHandsCleanupPending) {
            // Failure cleanup runs after ROCK has refreshed its normal grip
            // targets, so releasing the retained weapon-fixed tags cannot feed a
            // previous-frame pose into the weapon solver.
            clearManualCycleVisualAuthorityPreservingWeapon();
        }
        if (s_cycleTrace && logger::instance->should_log(spdlog::level::debug) &&
            !s_cycleTrace->failed.load(std::memory_order_relaxed)) {
            try {
                DebugAuthoritySnapshot sample{};
                if (queryDebugAuthoritySnapshot(sample)) {
                    auto& trace = *s_cycleTrace;
                    const bool active = sample.runtime.effectiveFlags != 0;
                    const std::array published{ sample.rightHand.visualAuthorityPublished,
                        sample.leftHand.visualAuthorityPublished };
                    const std::array qualified{ sample.rightHand.motionQualified,
                        sample.leftHand.motionQualified };
                    const bool edge = trace.active != active || trace.published != published ||
                        trace.qualified != qualified || trace.fireSequence != sample.runtime.fireSequence ||
                        trace.reloadStartSequence != sample.runtime.reloadStartSequence ||
                        trace.reloadEndSequence != sample.runtime.reloadEndSequence ||
                        trace.weaponFormId != s_manualCycleRockGripBaselines.weaponFormId ||
                        trace.weaponGenerationKey != s_manualCycleRockGripBaselines.weaponGenerationKey;
                    trace.active = active;
                    trace.published = published;
                    trace.qualified = qualified;
                    trace.fireSequence = sample.runtime.fireSequence;
                    trace.reloadStartSequence = sample.runtime.reloadStartSequence;
                    trace.reloadEndSequence = sample.runtime.reloadEndSequence;
                    trace.weaponFormId = s_manualCycleRockGripBaselines.weaponFormId;
                    trace.weaponGenerationKey = s_manualCycleRockGripBaselines.weaponGenerationKey;
                    if (edge || (trace.weaponFormId != 0 && frameIndex % (active ? 3 : 90) == 0)) {
                        trace.log->debug("CYCLE_TRACE frame={} graph={} fire={} reloadStart={} reloadEnd={} weapon={:08X}/{:016X} active={} cycle={} reload={} support={} edge={} capture={} ready={} applied={} overruns={}",
                            frameIndex, trace.graphSequence, trace.fireSequence,
                            trace.reloadStartSequence, trace.reloadEndSequence,
                            trace.weaponFormId, trace.weaponGenerationKey,
                            trace.active, sample.runtime.localManualCycleLeaseActive,
                            sample.partialReloadExpected, s_manualCycleRockGripBaselines.authoredLeftActive, edge,
                            sample.frameCaptureSequence, sample.frameCaptureReady,
                            sample.afterRockApplicationSucceeded,
                            trace.pool->overrun_counter());
                        const auto pose = [&](const char* hand, const char* label, bool valid, const RE::NiTransform& value) {
                            const auto& t = value.translate;
                            const auto& r = value.rotate.entry;
                            trace.log->debug("CYCLE_TRACE pose frame={} hand={} label={} valid={} T=({:.5f},{:.5f},{:.5f}) S={:.6f} R=({:.7f},{:.7f},{:.7f};{:.7f},{:.7f},{:.7f};{:.7f},{:.7f},{:.7f})",
                                frameIndex, hand, label, valid && finiteTransform(value), t.x, t.y, t.z, value.scale,
                                r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
                        };
                        pose("weapon", "native-model", trace.nativePose.primaryHandValid, trace.nativePose.weaponModel);
                        for (std::size_t index = 0; index < 2; ++index) {
                            const bool left = index == 1;
                            const char* name = left ? "left" : "right";
                            const auto& hand = left ? sample.leftHand : sample.rightHand;
                            trace.log->debug("CYCLE_TRACE hand frame={} hand={} mode={} qualified={} published={} motion=({:.5f}gu,{:.5f}deg)",
                                frameIndex, name, static_cast<unsigned>(hand.targetMode),
                                hand.motionQualified, hand.visualAuthorityPublished,
                                hand.motionTranslationGameUnits, hand.motionRotationDegrees);
                            pose(name, "native-current", left ? trace.nativePose.supportHandValid : trace.nativePose.primaryHandValid,
                                left ? trace.nativePose.supportHandInWeapon : trace.nativePose.primaryHandInWeapon);
                            if (active || edge) {
                                pose(name, "native-baseline", hand.nativeBaselineValid, hand.nativeBaselineHandInWeapon);
                                pose(name, "live-baseline", hand.liveBaselineValid, hand.liveBaselineHandInWeapon);
                                pose(name, "resolved-in-weapon", sample.frameCaptureReady && hand.resolvedHandInWeaponValid, hand.resolvedHandInWeapon);
                                pose(name, "submitted-world", hand.visualAuthorityPublished && hand.resolvedHandWorldValid, hand.resolvedHandWorld);
                            }
                        }
                        if (edge) {
                            trace.log->flush();
                        }
                    }
                }
            } catch (...) {
                s_cycleTrace->failed.store(true, std::memory_order_relaxed);
            }
        }
        if (s_cycleTrace) {
            // Do not label a previous graph sample as current after a missed
            // callback, load, or skeleton transition.
            s_cycleTrace->nativePose = {};
        }
        if (s_frameCaptureReady) {
            s_lastCompletedCaptureSequence = s_frameCaptureSequence;
        }
        s_frameCaptureReady = false;
        s_frameCapturePrepared = false;
        s_frameCaptureFlags = 0;
        s_frameCaptureSequence = 0;
        s_frameWeaponFixedHandsExpected = false;
        s_framePartialReloadExpected = false;
        s_frameWeaponFixedHandsApplied = false;
        s_frameWeaponFixedHandsCleanupPending = false;
        s_beforeRockApplicationAttempted = false;
        s_beforeRockApplicationSucceeded = false;
        s_afterRockApplicationAttempted = false;
        s_afterRockApplicationSucceeded = false;
    }

    void resetTransientState()
    {
        s_runtimeEnabled.store(false, std::memory_order_release);
        s_localManualCycleTestEnabled.store(false, std::memory_order_release);
        s_localReloadTestEnabled.store(false, std::memory_order_release);
        s_manualCycleHandAnimationEligible.store(false, std::memory_order_release);
        clearManualCycleRockGripState();
        s_localReloadTestLeaseFrames.store(0, std::memory_order_release);
        cancelLocalManualCycleTestLease();
        s_localReloadLeaseState = {};
        s_localManualCycleLeaseState = {};
        s_seenLocalReloadTestRequestSequence = s_localReloadTestRequestSequence.load(std::memory_order_acquire);
        s_seenLocalManualCycleTestRequestSequence =
            s_localManualCycleTestRequestSequence.load(std::memory_order_acquire);
        s_playerReloadEventActive.store(false, std::memory_order_release);
        s_playerWeaponFireActivityOrderAtEvent.store(
            0,
            std::memory_order_release);
        const DWORD ownerThread = s_ownerThreadId.load(std::memory_order_acquire);
        if (ownerThread == 0 || ownerThread == GetCurrentThreadId()) {
            clearManualCycleVisualAuthorityPreservingWeapon();
        }
        invalidateCapture();
        resetHybridPoseState();
        s_frameCaptureReady = false;
        s_frameCapturePrepared = false;
        s_frameCaptureFlags = 0;
        s_frameCaptureSequence = 0;
        s_frameWeaponFixedHandsExpected = false;
        s_framePartialReloadExpected = false;
        s_frameWeaponFixedHandsApplied = false;
        s_frameWeaponFixedHandsCleanupPending = false;
        s_beforeRockApplicationAttempted = false;
        s_beforeRockApplicationSucceeded = false;
        s_afterRockApplicationAttempted = false;
        s_afterRockApplicationSucceeded = false;
        s_lastCompletedCaptureSequence = s_captureSequence.load(std::memory_order_acquire);
        if (ownerThread == 0 || ownerThread == GetCurrentThreadId()) {
            s_cache = {};
            s_nativeHandBoneCache = {};
            s_manualCycleVisualPublications = {};
        }
    }

    bool isHookInstalled()
    {
        return s_hookInstalled.load(std::memory_order_acquire);
    }

    RuntimeStatus queryRuntimeStatus()
    {
        RuntimeStatus result{};
        result.effectiveFlags = effectiveRequestedFlags();
        result.capturedTransformCount = s_capturedTransformCount.load(std::memory_order_acquire);
        result.captureSequence = s_captureSequence.load(std::memory_order_acquire);
        result.reloadStartSequence =
            s_playerReloadStartSequence.load(std::memory_order_acquire);
        result.reloadEndSequence =
            s_playerReloadEndSequence.load(std::memory_order_acquire);
        result.fireSequence =
            s_playerWeaponFireSequence.load(std::memory_order_acquire);
        result.fireActivityOrderAtEvent =
            s_playerWeaponFireActivityOrderAtEvent.load(
                std::memory_order_acquire);
        result.reloadEventActive =
            s_playerReloadEventActive.load(std::memory_order_acquire);
        result.localManualCycleLeaseActive =
            s_localManualCycleTestLeaseActive.load(std::memory_order_acquire);
        result.weaponFireHookReady =
            s_weaponFireHookInstalled.load(std::memory_order_acquire);
        if (s_hookInstalled.load(std::memory_order_acquire)) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::HookInstalled);
        }
        if (s_runtimeEnabled.load(std::memory_order_acquire)) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::RuntimeEnabled);
        }
        if (s_captureValid.load(std::memory_order_acquire)) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::CaptureValid);
        }
        if (s_localReloadTestLeaseFrames.load(std::memory_order_acquire) > 0) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::LocalReloadTestLeaseActive);
        }
        if (s_hookInstallFailed.load(std::memory_order_acquire)) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::HookInstallFailed);
        }
        if (s_threadMismatch.load(std::memory_order_acquire)) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::ThreadMismatch);
        }
        if (s_captureFault.load(std::memory_order_acquire)) {
            result.statusFlags |= static_cast<std::uint32_t>(RuntimeStatusFlag::CaptureFault);
        }
        return result;
    }

    std::uint32_t currentLocalAuthorityFlags()
    {
        return localRequestedFlags();
    }

    std::uint32_t copyCapturedTransforms(
        CapturedTransform* outTransforms,
        const std::uint32_t maxTransforms)
    {
        if (!outTransforms || maxTransforms == 0 ||
            !s_captureValid.load(std::memory_order_acquire) ||
            !claimOrValidateThread() ||
            !validTree(s_cache.sourceTree)) {
            return 0;
        }

        const std::uint32_t limit = std::min(
            maxTransforms,
            static_cast<std::uint32_t>(s_cache.bindingCount));
        std::uint32_t copied = 0;
        for (std::uint32_t index = 0; index < limit; ++index) {
            const auto& binding = s_cache.bindings[index];
            if (!binding.captured ||
                binding.sourceIndex < 0 ||
                binding.sourceIndex >= s_cache.sourceTree->numTransforms) {
                continue;
            }

            auto& destination = outTransforms[copied++];
            destination = {};
            const auto name = transformName(
                s_cache.sourceTree->transforms[binding.sourceIndex]);
            const auto nameBytes = std::min(
                name.size(),
                kCapturedTransformNameCapacity - 1);
            std::memcpy(destination.name, name.data(), nameBytes);
            destination.name[nameBytes] = '\0';
            destination.local = binding.capturedLocal;
            destination.flags = binding.flags;
            destination.sourceIndex = binding.sourceIndex;
            destination.destinationIndex = binding.destinationIndex;
        }
        return copied;
    }

    bool queryNativeHandPose(const bool left, NativeHandPose& outPose)
    {
        outPose = {};
        if (!s_captureValid.load(std::memory_order_acquire) ||
            !claimOrValidateThread()) {
            return false;
        }

        const bool valid = left ?
            s_nativeHandPoseCapture.supportHandValid :
            s_nativeHandPoseCapture.primaryHandValid;
        if (!valid) {
            return false;
        }

        const auto& fingers = left ?
            s_nativeHandPoseCapture.supportFingerLocals :
            s_nativeHandPoseCapture.primaryFingerLocals;
        outPose.handInWeapon = left ?
            s_nativeHandPoseCapture.supportHandInWeapon :
            s_nativeHandPoseCapture.primaryHandInWeapon;
        outPose.fingerLocals = fingers.localTransforms;
        outPose.fingerLocalMask = fingers.enabledMask;
        outPose.captureSequence =
            s_captureSequence.load(std::memory_order_acquire);
        outPose.valid = true;
        return true;
    }

    bool queryDebugAuthoritySnapshot(DebugAuthoritySnapshot& outSnapshot)
    {
        outSnapshot = {};
        if (!claimOrValidateThread()) {
            return false;
        }

        outSnapshot.runtime = queryRuntimeStatus();
        outSnapshot.frameCaptureSequence = s_frameCaptureSequence;
        outSnapshot.frameCaptureReady = s_frameCaptureReady;
        outSnapshot.frameCapturePrepared = s_frameCapturePrepared;
        outSnapshot.weaponFixedHandsExpected =
            s_frameWeaponFixedHandsExpected;
        outSnapshot.partialReloadExpected = s_framePartialReloadExpected;
        outSnapshot.weaponFixedHandsApplied =
            s_frameWeaponFixedHandsApplied;
        outSnapshot.beforeRockApplicationAttempted =
            s_beforeRockApplicationAttempted;
        outSnapshot.beforeRockApplicationSucceeded =
            s_beforeRockApplicationSucceeded;
        outSnapshot.afterRockApplicationAttempted =
            s_afterRockApplicationAttempted;
        outSnapshot.afterRockApplicationSucceeded =
            s_afterRockApplicationSucceeded;

        const auto& aimFrame = s_sourceAimFrame;
        if (aimFrame.controlCaptured &&
            finiteTransform(aimFrame.controlWeaponWorld)) {
            outSnapshot.controllerWeaponWorld =
                aimFrame.controlWeaponWorld;
            outSnapshot.controllerWeaponValid = true;
        }
        if (aimFrame.nativeBaselineCaptured &&
            finiteTransform(aimFrame.nativeBaselineWeaponWorld)) {
            outSnapshot.nativeBaselineWeaponWorld =
                aimFrame.nativeBaselineWeaponWorld;
            outSnapshot.nativeBaselineWeaponValid = true;
        }
        if (aimFrame.desiredCaptured &&
            finiteTransform(aimFrame.desiredWeaponWorld)) {
            outSnapshot.desiredWeaponWorld = aimFrame.desiredWeaponWorld;
            outSnapshot.desiredWeaponValid = true;
        }

        const auto copyHand = [&](const bool left,
                                  DebugHandSnapshot& destination) {
            const auto& pose = left ?
                s_nativeHandPoseCapture.supportHandInWeapon :
                s_nativeHandPoseCapture.primaryHandInWeapon;
            const bool poseValid = left ?
                s_nativeHandPoseCapture.supportHandValid :
                s_nativeHandPoseCapture.primaryHandValid;
            if (poseValid && finiteTransform(pose)) {
                destination.nativeHandInWeapon = pose;
                destination.nativeHandValid = true;
            }
            const auto& fingers = left ?
                s_nativeHandPoseCapture.supportFingerLocals :
                s_nativeHandPoseCapture.primaryFingerLocals;
            destination.nativeFingerLocals = fingers.localTransforms;
            destination.nativeFingerLocalMask = fingers.enabledMask;

            const auto handIndex = left ? 1u : 0u;
            const auto& rebase = aimFrame.manualCycleHandRebases[handIndex];
            if (rebase.captured &&
                finiteTransform(rebase.nativeBaselineHandInWeapon)) {
                destination.nativeBaselineHandInWeapon =
                    rebase.nativeBaselineHandInWeapon;
                destination.nativeBaselineValid = true;
            }
            if (rebase.captured &&
                finiteTransform(rebase.liveBaselineHandInWeapon)) {
                destination.liveBaselineHandInWeapon =
                    rebase.liveBaselineHandInWeapon;
                destination.liveBaselineValid = true;
            }
            if (rebase.resolvedHandInWeaponValid &&
                finiteTransform(rebase.resolvedHandInWeapon)) {
                destination.resolvedHandInWeapon =
                    rebase.resolvedHandInWeapon;
                destination.resolvedHandInWeaponValid = true;
            }
            if (rebase.resolvedHandWorldValid &&
                finiteTransform(rebase.resolvedHandWorld)) {
                destination.resolvedHandWorld = rebase.resolvedHandWorld;
                destination.resolvedHandWorldValid = true;
            }
            destination.motionTranslationGameUnits =
                rebase.motionTranslationGameUnits;
            destination.motionRotationDegrees =
                rebase.motionRotationDegrees;
            destination.motionGateApplicable = rebase.captured;
            destination.motionQualified = rebase.motionQualified;
            if (rebase.captured) {
                destination.targetMode =
                    rebase.targetMode == native_animation_authority_policy::
                                             WeaponFixedHandTargetMode::
                                                 NativeWeaponRelative ?
                    DebugHandTargetMode::NativeWeaponRelative :
                    DebugHandTargetMode::LiveGripDelta;
            }
            destination.visualAuthorityPublished =
                s_manualCycleVisualPublications[handIndex].worldPublished;
        };
        copyHand(false, outSnapshot.rightHand);
        copyHand(true, outSnapshot.leftHand);
        return true;
    }
}
