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
        // Game units the hand moves to take the bolt from closed to fully back,
        // the same on every rifle.
        float pullDistance{ 7.0f };
        // PAPER allows this weapon's cycle animation: the rifles this feature
        // serves are exactly those.
        bool cycleAllowed{ false };
        bool playerReloadActive{ false };
        // A PhysicalReload consumer owns the bolt.
        bool physicalReloadActive{ false };
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
     * Bolt: closed, lifted, back. The player grips it with the free hand and
     * moves that hand back along the bolt's own line: the first part of the pull
     * lifts it where the rifle's clips turn it, the rest slides it. It stays where
     * it is let go. Both end poses are read from the weapon's clips (bolt_hold),
     * and a rifle with no pose or no bolt collider keeps its native cycle, while
     * its rounds and chamber are still tracked.
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

    // Forgets the equipped weapon and releases what ROCK holds for it. Saved
    // rounds stay until the co-save replaces them.
    void reset(RockApiClient& rock);

    // Whether the hand owns the cycle of the weapon in slot 0, so the native
    // post-shot bolt action must not run. Any thread.
    [[nodiscard]] bool ownsCycle() noexcept;

    // At F4SE load: registers the co-save record.
    void installSerialization();
}
