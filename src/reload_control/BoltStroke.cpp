#include "reload_control/BoltStroke.h"

#include "PaperLog.h"
#include "support/TransformMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace paper::bolt_stroke
{
    namespace
    {
        constexpr std::uint32_t kMaxFrames = 600;
        constexpr std::size_t kMaxKeys = 48;
        constexpr std::size_t kBolt = bolt_hold_policy::kBoltBone;
        // What one hand's names take in kArmBoneNames.
        constexpr std::size_t kHandNameCount = 1 + kFingerJointCount;
        // Joints in the finger order: the last two of the thumb and of the index.
        constexpr std::size_t kThumbMiddle = 1;
        constexpr std::size_t kThumbDistal = 2;
        constexpr std::size_t kIndexMiddle = 4;
        constexpr std::size_t kIndexDistal = 5;

        // An action bone still counts as at rest within this much of its first pose.
        constexpr float kRestGameUnits = 0.05f;
        constexpr float kRestDegrees = 0.5f;
        // A hand on the bolt keeps its distance to the bolt's origin while it slides,
        // and travels most of the way with it. The hand not working it does neither.
        constexpr float kRideRangeGameUnits = 3.0f;
        constexpr float kRideMoveFraction = 0.5f;
        constexpr float kSlideBeginsFraction = 0.15f;
        // How much of the pull lifts a bolt that turns, before it slides.
        constexpr float kLiftFraction = 0.3f;
        constexpr float kMinimumLiftDegrees = 10.0f;
        // A distal joint sits at the root of its fingertip; the pad is about half
        // that joint's length further along the finger.
        constexpr float kPadBeyondDistalJoint = 0.5f;

        [[nodiscard]] float turnDegrees(const RE::NiTransform& from, const RE::NiTransform& to)
        {
            double trace = 0.0;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    trace += static_cast<double>(from.rotate.entry[row][column]) * to.rotate.entry[row][column];
                }
            }
            return static_cast<float>(std::acos(std::clamp((trace - 1.0) * 0.5, -1.0, 1.0)) * 180.0 / std::numbers::pi);
        }

        [[nodiscard]] float boltTravel(const ClipFrame& rest, const ClipFrame& frame)
        {
            return rest.locals[kBolt].translate.GetDistance(frame.locals[kBolt].translate);
        }

        struct HandReading
        {
            bool complete{ false };
            float moved{ 0.0f };
            float rideRange{ 0.0f };
        };

        [[nodiscard]] HandReading readHand(
            const std::vector<ClipFrame>& frames,
            const std::size_t start,
            const std::size_t back,
            const std::size_t side,
            const float travel)
        {
            HandReading reading{};
            float nearest = 1e9f;
            float farthest = 0.0f;
            reading.complete = true;
            const auto& rest = frames.front();
            for (std::size_t index = start; index <= back; ++index) {
                const auto& frame = frames[index];
                const auto& hand = frame.hands[side];
                if (!hand.tracked) {
                    reading.complete = false;
                    return reading;
                }
                if (boltTravel(rest, frame) >= travel * kSlideBeginsFraction) {
                    const float distance = hand.inWeapon.translate.GetDistance(frame.inWeapon[kBolt].translate);
                    nearest = (std::min)(nearest, distance);
                    farthest = (std::max)(farthest, distance);
                }
            }
            reading.rideRange = farthest - nearest;
            reading.moved = frames[start].hands[side].inWeapon.translate.GetDistance(frames[back].hands[side].inWeapon.translate);
            return reading;
        }

        [[nodiscard]] RE::NiPoint3 fingerPad(const ClipHand& hand, const std::size_t middle, const std::size_t distal)
        {
            const auto& a = hand.fingerPoints[middle];
            const auto& b = hand.fingerPoints[distal];
            return {
                b.x + (b.x - a.x) * kPadBeyondDistalJoint,
                b.y + (b.y - a.y) * kPadBeyondDistalJoint,
                b.z + (b.z - a.z) * kPadBeyondDistalJoint,
            };
        }
    }

    void Recorder::begin(const std::uint32_t sampleCount) noexcept
    {
        try {
            _frames.clear();
            _frames.reserve((std::min)(sampleCount, kMaxFrames));
        } catch (...) {
            _frames = {};
            PAPER_LOG_ERROR(Reload, "MANUAL_BOLT no memory for a cycle clip of {} frames", sampleCount);
        }
    }

    void Recorder::clear() noexcept
    {
        _frames = {};
    }

    void Recorder::add(const exact_clip_preharvest::PoseFrame& source) noexcept
    {
        // Never grows: begin reserved what this clip may keep.
        if (_frames.size() == _frames.capacity()) {
            return;
        }
        auto& frame = _frames.emplace_back();
        frame.seconds = source.timeSeconds;
        for (std::uint32_t index = 0; index < source.boneCount; ++index) {
            const auto& bone = source.bones[index];
            if (bone.nameIndex < kActionBoneCount) {
                frame.tracked[bone.nameIndex] = true;
                frame.locals[bone.nameIndex] = bone.local;
                frame.inWeapon[bone.nameIndex] = bone.inWeapon;
                continue;
            }
            const std::size_t arm = bone.nameIndex - kActionBoneCount;
            if (arm >= kArmBoneNames.size()) {
                continue;
            }
            auto& hand = frame.hands[arm / kHandNameCount];
            const std::size_t slot = arm % kHandNameCount;
            if (slot == 0) {
                hand.tracked = true;
                hand.inWeapon = bone.inWeapon;
            } else {
                const std::size_t joint = slot - 1;
                hand.fingerMask = static_cast<std::uint16_t>(hand.fingerMask | (1u << joint));
                hand.fingerLocals[joint] = bone.local;
                hand.fingerPoints[joint] = bone.inWeapon.translate;
            }
        }
    }

    std::shared_ptr<const Stroke> analyse(
        const std::uint32_t weaponForm,
        const char* clipPath,
        const std::vector<ClipFrame>& frames)
    {
        const auto refuse = [&](const char* reason) {
            PAPER_LOG_INFO(
                Reload,
                "MANUAL_BOLT stroke weapon={:08X} clip='{}' frames={}: {}",
                weaponForm,
                clipPath,
                frames.size(),
                reason);
            return std::shared_ptr<const Stroke>{};
        };
        if (frames.size() < 2 || !frames.front().tracked[kBolt]) {
            return refuse("no bolt track");
        }
        const auto& rest = frames.front();
        std::size_t back = 0;
        float travel = 0.0f;
        for (std::size_t index = 0; index < frames.size(); ++index) {
            if (!frames[index].tracked[kBolt]) {
                return refuse("the bolt track has a gap");
            }
            const float moved = boltTravel(rest, frames[index]);
            if (moved > travel) {
                travel = moved;
                back = index;
            }
        }
        if (travel < bolt_hold_policy::kMinimumOpenTravelGameUnits) {
            return refuse("the bolt does not move");
        }

        // The last frame before the back position with every action bone at rest.
        std::size_t start = 0;
        for (std::size_t index = back; index > 0; --index) {
            bool atRest = true;
            for (std::size_t bone = 0; bone < kActionBoneCount && atRest; ++bone) {
                atRest = !frames[index].tracked[bone] || !rest.tracked[bone] ||
                    (rest.locals[bone].translate.GetDistance(frames[index].locals[bone].translate) <= kRestGameUnits &&
                        turnDegrees(rest.locals[bone], frames[index].locals[bone]) <= kRestDegrees);
            }
            if (atRest) {
                start = index;
                break;
            }
        }

        const auto left = readHand(frames, start, back, kLeftHand, travel);
        const auto right = readHand(frames, start, back, kRightHand, travel);
        const auto rides = [travel](const HandReading& hand) {
            return hand.complete && hand.rideRange <= kRideRangeGameUnits && hand.moved >= travel * kRideMoveFraction;
        };
        PAPER_LOG_INFO(
            Reload,
            "MANUAL_BOLT stroke weapon={:08X} clip='{}' start={:.2f}s back={:.2f}s travel={:.2f}gu left[moved={:.2f} rideRange={:.2f}] right[moved={:.2f} rideRange={:.2f}]",
            weaponForm,
            clipPath,
            frames[start].seconds,
            frames[back].seconds,
            travel,
            left.moved,
            left.rideRange,
            right.moved,
            right.rideRange);
        if (!rides(left) && !rides(right)) {
            return refuse("no hand rides the bolt");
        }
        const std::size_t side = rides(left) && (!rides(right) || left.rideRange < right.rideRange) ? kLeftHand : kRightHand;

        auto stroke = std::make_shared<Stroke>();
        stroke->hand = side;
        stroke->travelGameUnits = travel;

        // The fingers the hand keeps closed on the bolt for the whole stroke.
        std::uint16_t fingerMask = 0x7fff;
        for (std::size_t index = start; index <= back; ++index) {
            fingerMask = static_cast<std::uint16_t>(fingerMask & frames[index].hands[side].fingerMask);
        }
        constexpr std::uint16_t kGraspJoints = (1u << kThumbMiddle) | (1u << kThumbDistal) | (1u << kIndexMiddle) | (1u << kIndexDistal);
        if ((fingerMask & kGraspJoints) != kGraspJoints) {
            return refuse("the clip does not track the thumb and index finger");
        }
        stroke->fingerMask = fingerMask;

        // Which bones move, and which one turns the most.
        std::size_t turningBone = kBolt;
        for (std::size_t bone = 0; bone < kActionBoneCount; ++bone) {
            float largestMove = 0.0f;
            float largestTurn = 0.0f;
            for (std::size_t index = start; index <= back; ++index) {
                if (frames[index].tracked[bone] && rest.tracked[bone]) {
                    largestMove = (std::max)(largestMove, rest.locals[bone].translate.GetDistance(frames[index].locals[bone].translate));
                    largestTurn = (std::max)(largestTurn, turnDegrees(rest.locals[bone], frames[index].locals[bone]));
                }
            }
            stroke->moved[bone] = largestMove >= bolt_hold_policy::kPartMinimumMoveGameUnits ||
                largestTurn >= bolt_hold_policy::kPartMinimumTurnDegrees;
            if (largestTurn > stroke->turnDegrees) {
                stroke->turnDegrees = largestTurn;
                turningBone = bone;
            }
        }
        const float lift = stroke->turnDegrees >= kMinimumLiftDegrees ? kLiftFraction : 0.0f;

        const std::size_t span = back - start + 1;
        const std::size_t stride = (std::max<std::size_t>)(1, (span + kMaxKeys - 1) / kMaxKeys);
        float progress = 0.0f;
        for (std::size_t index = start;; index = (std::min)(index + stride, back)) {
            const auto& frame = frames[index];
            const float turned = lift > 0.0f ?
                turnDegrees(rest.locals[turningBone], frame.locals[turningBone]) / stroke->turnDegrees :
                0.0f;
            // A clip's small shake never walks the bolt backwards.
            progress = (std::max)(progress, lift * turned + (1.0f - lift) * (boltTravel(rest, frame) / travel));
            auto& key = stroke->keys.emplace_back();
            key.progress = index == start ? 0.0f : progress;
            for (std::size_t bone = 0; bone < kActionBoneCount; ++bone) {
                key.pose.bones[bone] = frame.locals[bone];
            }
            key.pose.handInWeapon = frame.hands[side].inWeapon;
            key.pose.fingers = frame.hands[side].fingerLocals;
            if (index == back) {
                break;
            }
        }
        stroke->keys.back().progress = 1.0f;

        // Halfway along the slide the hand is certainly closed on the bolt.
        std::size_t held = back;
        for (std::size_t index = start; index <= back; ++index) {
            if (boltTravel(rest, frames[index]) >= travel * 0.5f) {
                held = index;
                break;
            }
        }
        const auto& grip = frames[held];
        const auto thumb = fingerPad(grip.hands[side], kThumbMiddle, kThumbDistal);
        const auto index = fingerPad(grip.hands[side], kIndexMiddle, kIndexDistal);
        stroke->graspInBolt = transform_math::worldPointToLocal(
            grip.inWeapon[kBolt],
            RE::NiPoint3{ (thumb.x + index.x) * 0.5f, (thumb.y + index.y) * 0.5f, (thumb.z + index.z) * 0.5f });

        const auto& boltStart = frames[start].inWeapon[kBolt].translate;
        const auto& boltBack = frames[back].inWeapon[kBolt].translate;
        const RE::NiPoint3 slide{ boltBack.x - boltStart.x, boltBack.y - boltStart.y, boltBack.z - boltStart.z };
        const float length = std::sqrt(slide.x * slide.x + slide.y * slide.y + slide.z * slide.z);
        if (!std::isfinite(length) || length < bolt_hold_policy::kMinimumOpenTravelGameUnits) {
            return refuse("the bolt's slide has no direction");
        }
        stroke->backInWeapon = { slide.x / length, slide.y / length, slide.z / length };

        PAPER_LOG_INFO(
            Reload,
            "MANUAL_BOLT stroke weapon={:08X} ready hand={} keys={} turn={:.1f}deg lift={:.2f} fingers={:#x} grasp=({:.2f},{:.2f},{:.2f}) thumbToIndex={:.2f}gu back=({:.2f},{:.2f},{:.2f})",
            weaponForm,
            side == kLeftHand ? "left" : "right",
            stroke->keys.size(),
            stroke->turnDegrees,
            lift,
            stroke->fingerMask,
            stroke->graspInBolt.x,
            stroke->graspInBolt.y,
            stroke->graspInBolt.z,
            thumb.GetDistance(index),
            stroke->backInWeapon.x,
            stroke->backInWeapon.y,
            stroke->backInWeapon.z);
        return stroke;
    }

    void sample(const Stroke& stroke, const float pull, Pose& out)
    {
        const float wanted = std::clamp(pull, 0.0f, 1.0f);
        const auto& keys = stroke.keys;
        std::size_t upper = 1;
        while (upper + 1 < keys.size() && keys[upper].progress < wanted) {
            ++upper;
        }
        const auto& from = keys[upper - 1];
        const auto& to = keys[upper];
        const float width = to.progress - from.progress;
        const float alpha = width > 0.0001f ? std::clamp((wanted - from.progress) / width, 0.0f, 1.0f) : 1.0f;
        for (std::size_t bone = 0; bone < kActionBoneCount; ++bone) {
            if (stroke.moved[bone]) {
                out.bones[bone] = transform_math::interpolateTransforms(from.pose.bones[bone], to.pose.bones[bone], alpha);
            }
        }
        out.handInWeapon = transform_math::interpolateTransforms(from.pose.handInWeapon, to.pose.handInWeapon, alpha);
        for (std::size_t joint = 0; joint < kFingerJointCount; ++joint) {
            if ((stroke.fingerMask & (1u << joint)) != 0) {
                out.fingers[joint] = transform_math::interpolateTransforms(from.pose.fingers[joint], to.pose.fingers[joint], alpha);
            }
        }
    }
}
