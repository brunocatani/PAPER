#pragma once

#include "animation_evidence/ExactClipPreharvest.h"
#include "reload_control/BoltHoldPolicy.h"

#include <RE/NetImmerse/NiPoint.h>
#include <RE/NetImmerse/NiTransform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace paper::bolt_stroke
{
    inline constexpr std::size_t kActionBoneCount = bolt_hold_policy::kActionBoneNames.size();
    inline constexpr std::size_t kFingerJointCount = 15;
    inline constexpr std::size_t kLeftHand = 0;
    inline constexpr std::size_t kRightHand = 1;

    // Thumb, index, middle, ring, pinky; three joints each. Left, then right.
    inline constexpr std::array<const char*, 32> kArmBoneNames{
        "LArm_Hand",
        "LArm_Finger11", "LArm_Finger12", "LArm_Finger13",
        "LArm_Finger21", "LArm_Finger22", "LArm_Finger23",
        "LArm_Finger31", "LArm_Finger32", "LArm_Finger33",
        "LArm_Finger41", "LArm_Finger42", "LArm_Finger43",
        "LArm_Finger51", "LArm_Finger52", "LArm_Finger53",
        "RArm_Hand",
        "RArm_Finger11", "RArm_Finger12", "RArm_Finger13",
        "RArm_Finger21", "RArm_Finger22", "RArm_Finger23",
        "RArm_Finger31", "RArm_Finger32", "RArm_Finger33",
        "RArm_Finger41", "RArm_Finger42", "RArm_Finger43",
        "RArm_Finger51", "RArm_Finger52", "RArm_Finger53",
    };

    struct ClipHand
    {
        bool tracked{ false };
        RE::NiTransform inWeapon{};
        std::uint16_t fingerMask{ 0 };
        std::array<RE::NiTransform, kFingerJointCount> fingerLocals{};
        std::array<RE::NiPoint3, kFingerJointCount> fingerPoints{};
    };

    struct ClipFrame
    {
        float seconds{ 0.0f };
        std::array<bool, kActionBoneCount> tracked{};
        std::array<RE::NiTransform, kActionBoneCount> locals{};
        std::array<RE::NiTransform, kActionBoneCount> inWeapon{};
        std::array<ClipHand, 2> hands{};
    };

    // Takes one clip's sampled frames. Off screen and once per weapon form.
    class Recorder
    {
    public:
        void begin(std::uint32_t sampleCount) noexcept;
        void add(const exact_clip_preharvest::PoseFrame& frame) noexcept;
        [[nodiscard]] const std::vector<ClipFrame>& frames() const noexcept { return _frames; }
        void clear() noexcept;

    private:
        std::vector<ClipFrame> _frames;
    };

    // What the action and the hand working it do at one point of the pull.
    struct Pose
    {
        std::array<RE::NiTransform, kActionBoneCount> bones{};
        RE::NiTransform handInWeapon{};
        std::array<RE::NiTransform, kFingerJointCount> fingers{};
    };

    struct Key
    {
        // 0 is the action closed and 1 the bolt fully back, in the clip's own time.
        float progress{ 0.0f };
        Pose pose{};
    };

    // One sound of the stroke, played as the bolt passes this point of the pull.
    struct SoundCue
    {
        std::uint32_t form{ 0 };
        float pull{ 0.0f };
        // On the way home, rather than on the way back.
        bool closing{ false };
    };

    /*
     * The first half of a bolt rifle's cycle clip, from the action's first movement
     * to the bolt fully back, played in either direction once the pull commits. It is
     * what the animator made: the bolt's path and timing, the parts that move with it,
     * the wrist and fingers of whichever hand rides the bolt, and the sounds the clip
     * marks for the whole cycle. A clip that marks none has the game's bolt sounds at
     * the start of each way.
     */
    struct Stroke
    {
        std::size_t hand{ kRightHand };
        // The bones this clip moves through the stroke.
        std::array<bool, kActionBoneCount> moved{};
        std::uint16_t fingerMask{ 0 };
        // Midway between the thumb and index finger pads where the hand holds the
        // bolt, in the bolt bone's frame, so it stays on the part it was read from.
        RE::NiPoint3 graspInBolt{};
        // The direction the bolt slides back, unit length, in the weapon's frame.
        RE::NiPoint3 backInWeapon{};
        float travelGameUnits{ 0.0f };
        float turnDegrees{ 0.0f };
        // How long the clip takes from the first movement to fully back.
        float seconds{ 0.0f };
        std::vector<Key> keys;
        std::vector<SoundCue> sounds;
    };

    // Null, with the reason logged, unless the clip shows one hand riding the bolt
    // all the way back. A clip with no fingers or an additive one cannot be used.
    [[nodiscard]] std::shared_ptr<const Stroke> analyse(
        std::uint32_t weaponForm,
        const char* clipPath,
        const std::vector<ClipFrame>& frames,
        std::span<const exact_clip_preharvest::PoseMarker> markers);

    // `pull` is clamped to 0..1.
    void sample(const Stroke& stroke, float pull, Pose& out);
}
