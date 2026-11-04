#include "debug/NativeAnimationDebugVisualization.h"

#include "animation/NativeAnimationAuthority.h"
#include "api/ApiTransform.h"
#include "api/RockApiClient.h"
#include "PaperConfig.h"
#include "PaperLog.h"
#include "support/TransformMath.h"
#include "debug/WorldDrawClient.h"

#include "RE/NetImmerse/NiPoint.h"
#include "RE/NetImmerse/NiTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>

namespace paper::debug_visualization
{
    namespace
    {
        using Color = std::array<float, 4>;

        constexpr std::size_t kLineCapacity = 256;
        constexpr std::size_t kTextCapacity = 8;
        constexpr Color kControllerWeaponColor{ 1.0f, 1.0f, 1.0f, 0.92f };
        constexpr Color kNativeWeaponColor{ 1.0f, 0.10f, 0.85f, 0.92f };
        constexpr Color kDesiredWeaponColor{ 1.0f, 0.78f, 0.05f, 0.95f };
        constexpr Color kNativeHandColor{ 0.10f, 1.0f, 0.20f, 0.95f };
        constexpr Color kRockGripColor{ 0.10f, 0.45f, 1.0f, 0.95f };
        constexpr Color kResolvedHandColor{ 1.0f, 0.55f, 0.05f, 0.95f };
        constexpr Color kAppliedHandColor{ 0.10f, 1.0f, 1.0f, 0.95f };
        constexpr Color kGoodErrorColor{ 0.15f, 1.0f, 0.25f, 0.92f };
        constexpr Color kBadErrorColor{ 1.0f, 0.10f, 0.08f, 0.95f };
        constexpr Color kTextColor{ 0.92f, 1.0f, 0.96f, 0.96f };
        constexpr Color kRightTextColor{ 0.35f, 0.90f, 1.0f, 0.96f };
        constexpr Color kLeftTextColor{ 1.0f, 0.70f, 0.25f, 0.96f };
        constexpr float kGoodAppliedErrorGameUnits = 0.35f;

        static_assert(kLineCapacity <= rpsui::api::world_draw::kMaxLines);
        static_assert(kTextCapacity <= rpsui::api::world_draw::kMaxTexts);
        constexpr std::uint32_t kLeaseFrames = 3;

        WorldDrawClient s_worldDraw{};
        bool s_published{ false };
        bool s_publishFailureReported{ false };

        [[nodiscard]] bool finitePoint(const RE::NiPoint3& point)
        {
            return std::isfinite(point.x) &&
                   std::isfinite(point.y) &&
                   std::isfinite(point.z);
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
            return finitePoint(transform.translate) &&
                   std::isfinite(transform.scale) &&
                   std::abs(transform.scale) > 0.000001f;
        }

        [[nodiscard]] bool hasGripFlag(
            const std::uint32_t flags,
            const rock::api::weapon::GripFlag flag)
        {
            return (flags & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] float pointDistance(
            const RE::NiPoint3& lhs,
            const RE::NiPoint3& rhs)
        {
            const float x = lhs.x - rhs.x;
            const float y = lhs.y - rhs.y;
            const float z = lhs.z - rhs.z;
            return std::sqrt(x * x + y * y + z * z);
        }

        struct Builder
        {
            std::array<rpsui::api::world_draw::Line, kLineCapacity> lines{};
            std::array<rpsui::api::world_draw::Text, kTextCapacity> textEntries{};
            std::uint32_t lineCount{ 0 };
            std::uint32_t textCount{ 0 };

            void addLine(
                const RE::NiPoint3& start,
                const RE::NiPoint3& end,
                const Color& color)
            {
                if (!finitePoint(start) || !finitePoint(end) ||
                    lineCount >= lines.size()) {
                    return;
                }
                auto& line = lines[lineCount++];
                line.start[0] = start.x;
                line.start[1] = start.y;
                line.start[2] = start.z;
                line.end[0] = end.x;
                line.end[1] = end.y;
                line.end[2] = end.z;
                std::copy_n(color.data(), color.size(), line.color);
            }

            void addCross(
                const RE::NiPoint3& position,
                const float size,
                const Color& color)
            {
                addLine(
                    position + RE::NiPoint3(-size, 0.0f, 0.0f),
                    position + RE::NiPoint3(size, 0.0f, 0.0f),
                    color);
                addLine(
                    position + RE::NiPoint3(0.0f, -size, 0.0f),
                    position + RE::NiPoint3(0.0f, size, 0.0f),
                    color);
                addLine(
                    position + RE::NiPoint3(0.0f, 0.0f, -size),
                    position + RE::NiPoint3(0.0f, 0.0f, size),
                    color);
            }

            void addAxis(
                const RE::NiTransform& transform,
                const float length)
            {
                if (!finiteTransform(transform)) {
                    return;
                }
                RE::NiTransform unitScale = transform;
                unitScale.scale = 1.0f;
                constexpr Color xColor{ 1.0f, 0.05f, 0.05f, 0.90f };
                constexpr Color yColor{ 0.05f, 1.0f, 0.10f, 0.90f };
                constexpr Color zColor{ 0.10f, 0.35f, 1.0f, 0.90f };
                addLine(
                    transform.translate,
                    transform_math::localPointToWorld(
                        unitScale,
                        RE::NiPoint3(length, 0.0f, 0.0f)),
                    xColor);
                addLine(
                    transform.translate,
                    transform_math::localPointToWorld(
                        unitScale,
                        RE::NiPoint3(0.0f, length, 0.0f)),
                    yColor);
                addLine(
                    transform.translate,
                    transform_math::localPointToWorld(
                        unitScale,
                        RE::NiPoint3(0.0f, 0.0f, length)),
                    zColor);
            }

            template <class... Args>
            void addWorldText(
                const RE::NiPoint3& anchor,
                const Color& color,
                const char* format,
                Args&&... args)
            {
                if (!g_config.debugDrawNativeAnimationText || !format ||
                    !finitePoint(anchor) || textCount >= textEntries.size()) {
                    return;
                }
                auto& entry = textEntries[textCount++];
                std::copy_n(color.data(), color.size(), entry.color);
                entry.anchor[0] = anchor.x;
                entry.anchor[1] = anchor.y;
                entry.anchor[2] = anchor.z;
                std::snprintf(
                    entry.text,
                    sizeof(entry.text),
                    format,
                    std::forward<Args>(args)...);
            }
        };

        [[nodiscard]] bool queryAppliedHandWorld(
            const rock::api::Hand hand,
            RE::NiTransform& outWorld)
        {
            outWorld = {};
                if (!rockApiClient().ready()) {
                return false;
            }
            rock::api::hands::HandFrame frame{};
            if (!rockApiClient().queryPresentedHandFrame(hand,frame) ||
                (frame.flags & static_cast<std::uint32_t>(
                    rock::api::hands::HandFrameFlag::PresentedVisual)) == 0) {
                return false;
            }
            outWorld = api_transform::toNi(frame.transform);
            return finiteTransform(outWorld);
        }

        [[nodiscard]] const char* modeName(
            const native_animation_authority::DebugAuthoritySnapshot& snapshot)
        {
            if (snapshot.runtime.localManualCycleLeaseActive) {
                return "manual-cycle";
            }
            if (snapshot.partialReloadExpected) {
                return "partial-reload";
            }
            if (snapshot.weaponFixedHandsExpected) {
                return "weapon-fixed";
            }
            if (snapshot.runtime.reloadEventActive) {
                return "full-reload";
            }
            return "idle";
        }

        void addWeaponFrame(
            Builder& builder,
            const RE::NiTransform& transform,
            const bool valid,
            const Color& color)
        {
            if (!valid || !finiteTransform(transform)) {
                return;
            }
            builder.addCross(
                transform.translate,
                g_config.debugNativeAnimationMarkerSize,
                color);
            builder.addAxis(
                transform,
                g_config.debugNativeAnimationAxisLength);
        }

        void addHandVisualization(
            Builder& builder,
            const char* label,
            const bool left,
            const bool nativeAuthorityActive,
            const bool motionGateApplicable,
            const native_animation_authority::DebugHandSnapshot& handSnapshot,
            const RE::NiTransform& nativeWeaponWorld,
            const bool nativeWeaponValid,
            const RE::NiTransform& rockWeaponWorld,
            const bool rockWeaponValid,
            const RE::NiTransform& rockHandInWeapon,
            const bool rockHandValid)
        {
            RE::NiTransform nativeTarget{};
            bool nativeTargetValid = false;
            if (nativeAuthorityActive && nativeWeaponValid &&
                handSnapshot.nativeHandValid &&
                finiteTransform(handSnapshot.nativeHandInWeapon)) {
                nativeTarget = transform_math::composeTransforms(
                    nativeWeaponWorld,
                    handSnapshot.nativeHandInWeapon);
                nativeTargetValid = finiteTransform(nativeTarget);
            }

            RE::NiTransform rockTarget{};
            bool rockTargetValid = false;
            if (rockWeaponValid && rockHandValid &&
                finiteTransform(rockHandInWeapon)) {
                rockTarget = transform_math::composeTransforms(
                    rockWeaponWorld,
                    rockHandInWeapon);
                rockTargetValid = finiteTransform(rockTarget);
            }

            RE::NiTransform resolvedTarget{};
            bool resolvedTargetValid = false;
            if (nativeAuthorityActive &&
                handSnapshot.resolvedHandWorldValid &&
                finiteTransform(handSnapshot.resolvedHandWorld)) {
                resolvedTarget = handSnapshot.resolvedHandWorld;
                resolvedTargetValid = true;
            } else if (nativeTargetValid) {
                resolvedTarget = nativeTarget;
                resolvedTargetValid = true;
            }

            RE::NiTransform appliedHand{};
            const bool appliedHandValid = queryAppliedHandWorld(
                left ? rock::api::Hand::Left :
                       rock::api::Hand::Right,
                appliedHand);

            const float markerSize = g_config.debugNativeAnimationMarkerSize;
            if (nativeTargetValid) {
                builder.addCross(
                    nativeTarget.translate,
                    markerSize,
                    kNativeHandColor);
            }
            if (rockTargetValid) {
                builder.addCross(
                    rockTarget.translate,
                    markerSize,
                    kRockGripColor);
            }
            if (resolvedTargetValid) {
                builder.addCross(
                    resolvedTarget.translate,
                    markerSize,
                    kResolvedHandColor);
                builder.addAxis(
                    resolvedTarget,
                    g_config.debugNativeAnimationAxisLength * 0.75f);
            }
            if (appliedHandValid) {
                builder.addCross(
                    appliedHand.translate,
                    markerSize * 0.75f,
                    kAppliedHandColor);
                builder.addAxis(
                    appliedHand,
                    g_config.debugNativeAnimationAxisLength * 0.55f);
            }
            if (nativeTargetValid && resolvedTargetValid) {
                builder.addLine(
                    nativeTarget.translate,
                    resolvedTarget.translate,
                    kNativeWeaponColor);
            }
            if (rockTargetValid && resolvedTargetValid) {
                builder.addLine(
                    rockTarget.translate,
                    resolvedTarget.translate,
                    kDesiredWeaponColor);
            }

            float appliedError = 0.0f;
            bool appliedErrorValid = false;
            if (resolvedTargetValid && appliedHandValid) {
                appliedError = pointDistance(
                    resolvedTarget.translate,
                    appliedHand.translate);
                appliedErrorValid = std::isfinite(appliedError);
                builder.addLine(
                    resolvedTarget.translate,
                    appliedHand.translate,
                    appliedErrorValid &&
                            appliedError <= kGoodAppliedErrorGameUnits ?
                        kGoodErrorColor :
                        kBadErrorColor);
            }

            RE::NiPoint3 labelAnchor{};
            if (appliedHandValid) {
                labelAnchor = appliedHand.translate;
            } else if (resolvedTargetValid) {
                labelAnchor = resolvedTarget.translate;
            } else if (rockTargetValid) {
                labelAnchor = rockTarget.translate;
            } else {
                return;
            }
            labelAnchor.z += markerSize * 2.0f;
            const char* authorityState = !nativeAuthorityActive ?
                "inactive" :
                (motionGateApplicable ?
                        (handSnapshot.motionQualified ?
                                "qualified" :
                                "gated") :
                        "direct");
            if (appliedErrorValid) {
                builder.addWorldText(
                    labelAnchor,
                    left ? kLeftTextColor : kRightTextColor,
                    "%s err=%.2fgu motion=%.2fgu/%.1fdeg %s%s",
                    label,
                    appliedError,
                    handSnapshot.motionTranslationGameUnits,
                    handSnapshot.motionRotationDegrees,
                    authorityState,
                    handSnapshot.visualAuthorityPublished ? "/published" : "");
            } else {
                builder.addWorldText(
                    labelAnchor,
                    left ? kLeftTextColor : kRightTextColor,
                    "%s err=n/a motion=%.2fgu/%.1fdeg %s%s",
                    label,
                    handSnapshot.motionTranslationGameUnits,
                    handSnapshot.motionRotationDegrees,
                    authorityState,
                    handSnapshot.visualAuthorityPublished ? "/published" : "");
            }
        }
    }

    void publish(
        const rock::api::animation::AnimationPhaseContext& context,
        const paper::RockWeaponGripState& gripState)
    {
        if (!g_config.enabled || !g_config.debugDrawNativeAnimation ||
            !rockApiClient().ready()) {
            clear();
            return;
        }

        native_animation_authority::DebugAuthoritySnapshot snapshot{};
        if (!native_animation_authority::queryDebugAuthoritySnapshot(snapshot)) {
            clear();
            return;
        }

        Builder builder{};
        const bool gripValid = hasGripFlag(
            gripState.flags,
            rock::api::weapon::GripFlag::Valid);
        const RE::NiTransform rockWeaponWorld =
            api_transform::toNi(gripState.weaponWorld);
        const bool rockWeaponValid =
            gripValid && finiteTransform(rockWeaponWorld);

        addWeaponFrame(
            builder,
            snapshot.controllerWeaponWorld,
            snapshot.controllerWeaponValid,
            kControllerWeaponColor);
        addWeaponFrame(
            builder,
            snapshot.nativeBaselineWeaponWorld,
            snapshot.nativeBaselineWeaponValid,
            kNativeWeaponColor);
        addWeaponFrame(
            builder,
            snapshot.desiredWeaponWorld,
            snapshot.desiredWeaponValid,
            kDesiredWeaponColor);

        RE::NiTransform nativeWeaponWorld{};
        bool nativeWeaponValid = false;
        if (snapshot.desiredWeaponValid) {
            nativeWeaponWorld = snapshot.desiredWeaponWorld;
            nativeWeaponValid = true;
        } else if (snapshot.controllerWeaponValid) {
            nativeWeaponWorld = snapshot.controllerWeaponWorld;
            nativeWeaponValid = true;
        } else if (rockWeaponValid) {
            nativeWeaponWorld = rockWeaponWorld;
            nativeWeaponValid = true;
        }

        const bool rightGripValid = gripValid && hasGripFlag(
            gripState.flags,
            rock::api::weapon::GripFlag::RightHandInWeaponValid);
        const bool leftGripValid = gripValid && hasGripFlag(
            gripState.flags,
            rock::api::weapon::GripFlag::LeftHandInWeaponValid);
        const bool nativeAuthorityActive =
            snapshot.frameCaptureReady &&
            snapshot.runtime.effectiveFlags != 0;
        addHandVisualization(
            builder,
            "R",
            false,
            nativeAuthorityActive,
            snapshot.weaponFixedHandsExpected,
            snapshot.rightHand,
            nativeWeaponWorld,
            nativeWeaponValid,
            rockWeaponWorld,
            rockWeaponValid,
            api_transform::toNi(gripState.rightHandInWeapon),
            rightGripValid);
        addHandVisualization(
            builder,
            "L",
            true,
            nativeAuthorityActive,
            snapshot.weaponFixedHandsExpected,
            snapshot.leftHand,
            nativeWeaponWorld,
            nativeWeaponValid,
            rockWeaponWorld,
            rockWeaponValid,
            api_transform::toNi(gripState.leftHandInWeapon),
            leftGripValid);

        // VR text is anchored to the object it describes, never to the screen.
        if (nativeWeaponValid || rockWeaponValid) {
            builder.addWorldText(
                nativeWeaponValid ? nativeWeaponWorld.translate : rockWeaponWorld.translate,
                kTextColor,
                "PAPER %s cap=%llu flags=%X frame=%llu ready=%u applied=%u",
                modeName(snapshot),
                static_cast<unsigned long long>(snapshot.frameCaptureSequence),
                snapshot.runtime.effectiveFlags,
                static_cast<unsigned long long>(context.frameIndex),
                snapshot.frameCaptureReady ? 1u : 0u,
                snapshot.weaponFixedHandsApplied ? 1u : 0u);
        }

        if (builder.lineCount == 0 && builder.textCount == 0) {
            clear();
            return;
        }

        rpsui::api::world_draw::Publication publication{};
        publication.leaseFrames = kLeaseFrames;
        publication.lineCount = builder.lineCount;
        publication.textCount = builder.textCount;
        publication.lines = builder.lines.data();
        publication.texts = builder.textEntries.data();
        if (!s_worldDraw.submit(publication)) {
            // A rejected replacement must not leave the preceding successful
            // frame frozen in RPS UI's owner-scoped store.
            s_worldDraw.clear();
            if (!s_publishFailureReported) {
                PAPER_LOG_WARN(
                    Api,
                    "The PAPER debug publication was not drawn");
                s_publishFailureReported = true;
            }
            s_published = false;
            return;
        }
        s_published = true;
        s_publishFailureReported = false;
    }

    void clear()
    {
        if (s_published) {
            s_worldDraw.clear();
        }
        s_published = false;
        s_publishFailureReported = false;
    }
}
