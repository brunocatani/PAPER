#pragma once

namespace paper
{
    class RockApiClient;
}

namespace paper::manual_bolt
{
    struct FrameInput
    {
        bool enabled{ false };
        // Game units of a full pull, the same on every rifle. Crossing half of it
        // commits the stroke.
        float pullDistance{ 7.0f };
        // The clip's hand takes the bolt at once, at the clip's point and in its pose.
        // Off, it takes ROCK's dynamic grip like any other hand, which needs colliders.
        bool handFromAnimation{ true };
        // The rifles PAPER animates the cycle of are the rifles this feature serves.
        bool cycleAllowed{ false };
        bool playerReloadActive{ false };
        // A PhysicalReload consumer owns the bolt.
        bool physicalReloadActive{ false };
        float handBlendSeconds{ 0.15f };
        float deltaSeconds{ 0.0f };
    };

    /*
     * Bolt-action rifles on weapon slot 0, with no authored data.
     *
     * Chamber: Empty, Live or Spent. A shot leaves it Spent; the trigger stays
     * held back until the bolt has gone all the way back, which throws the case
     * out, and home again, which feeds the next round. A native reload leaves it
     * Live, and when it started with a live round that round stays in the chamber
     * on top of the full magazine, as far as the reserve allows. The chamber is
     * shown as a cartridge on ROCK's ammo counter, under a lease renewed here.
     *
     * Bolt: two resting positions, closed and back, and the rifle's own cycle clip
     * between them (bolt_stroke). The hand the clip works the bolt with takes it at
     * the point the clip holds it, in the clip's pose, from the moment the rifle is
     * known: ROCK holds a grab point through its own node, with no collider. When
     * that is the firing hand, the other hand carries the gun first and ROCK frees
     * the firing hand. Any other hand takes the bolt's colliders with ROCK's dynamic
     * grip and finger posing, once they exist. A hand on the bolt is locked to it, so
     * the pull is read from that hand's controller: moving it past half the pull
     * distance along the bolt's line commits the stroke, which plays at the clip's
     * speed. Letting go never moves the bolt, so it can be left open. A rifle whose
     * clip shows no hand on the bolt keeps its native cycle, while its rounds and
     * chamber are still tracked.
     *
     * ROCK re-keys the weapon when its colliders arrive, which changes nothing here.
     *
     * Rounds: the loaded count and the chamber of each inventory item survive
     * unequipping and saving, by ExtraUniqueID, in the co-save. The native count
     * is set after equip and never above what the inventory holds: loaded rounds
     * are counted in the inventory, so nothing is created. For the same reason a
     * live round thrown out by the bolt just returns to the reserve.
     *
     * Runs once a frame in ROCK's BeforeRock phase on its frame thread, which owns
     * everything here except the saved table, which also has F4SE's save and load
     * callbacks and takes a mutex.
     */
    void update(RockApiClient& rock, const FrameInput& input);

    // In ROCK's AfterRock phase, once the weapon is placed for this frame: the hand
    // on the bolt, when it is posed from the clip.
    void presentHand(RockApiClient& rock);

    // Forgets the equipped weapon and releases what ROCK holds for it. Saved
    // rounds stay until the co-save replaces them.
    void reset(RockApiClient& rock);

    // Whether the hand owns the cycle of the weapon in slot 0, so the native
    // post-shot bolt action must not run. Any thread.
    [[nodiscard]] bool ownsCycle() noexcept;

    // At F4SE load: registers the co-save record.
    void installSerialization();
}
