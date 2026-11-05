#pragma once

namespace paper
{
    class RockApiClient;
}

namespace paper::manual_bolt
{
    struct FrameInput
    {
        // Every gun that fires cartridges keeps a chamber and its rounds.
        bool chamberEnabled{ false };
        // A hand works the bolt of a bolt-action rifle, which keeps a chamber and its
        // rounds for that whatever chamberEnabled says.
        bool boltEnabled{ false };
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
     * The round in the chamber of every gun that fires cartridges, on both weapon
     * slots, and the bolt of bolt-action rifles on slot 0, with no authored data.
     *
     * Which guns: the equipped ammo names a casing model, the game's own test for
     * throwing a case out, and the weapon has the game's ballistic keyword. Both are
     * read from the equipped weapon, because weapon mods swap ammo and keywords.
     * Energy ammo and the Gauss rifle's have no casing; the launchers and soakers
     * whose ammo has one lack the keyword. A gun is known from the moment it is drawn,
     * so nothing here waits for its colliders, except that ROCK keys slot 1 by them.
     *
     * Chamber: Empty, Live or Spent. A gun that cycles itself is Live whenever it has
     * rounds. On a rifle worked by hand a shot leaves it Spent; the trigger stays
     * held back until the bolt has gone all the way back, which throws the case
     * out, and home again, which feeds the next round. A native reload leaves it
     * Live, and when it started with a live round that round stays in the chamber
     * on top of the full magazine, as far as the reserve allows, except on a gun
     * with nowhere to keep it (bolt_hold_policy::familyKeepsChamberedRound). The
     * chamber is shown as a cartridge on ROCK's ammo counter, under a lease renewed
     * here.
     *
     * What the bolt throws out at the back of the stroke goes in chamber_ejection, in
     * presentHand, because the casing's place is read from the placed weapon.
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
     * While PAPER does not animate the rifle's cycle the hand stands down, the game
     * cycles the rifle and its rounds go on being counted.
     *
     * Rounds: the loaded count and the chamber of each inventory item survive
     * unequipping and saving, by ExtraUniqueID, in the co-save. The native count
     * is set after equip and never above what the inventory holds: loaded rounds
     * are counted in the inventory, so nothing is created. A live round the bolt
     * throws out becomes a real round in the world and leaves the inventory.
     *
     * Runs once a frame in ROCK's BeforeRock phase on its frame thread, which owns
     * everything here except the saved table, which also has F4SE's save and load
     * callbacks and takes a mutex.
     */
    void update(RockApiClient& rock, const FrameInput& input);

    // In ROCK's AfterRock phase, once the weapon is placed for this frame: what the bolt
    // throws out, and the hand on the bolt, when it is posed from the clip.
    void presentHand(RockApiClient& rock);

    // Forgets the equipped weapons and releases what ROCK holds for them. Saved
    // rounds stay until the co-save replaces them.
    void reset(RockApiClient& rock);

    // Whether the hand owns the cycle of the weapon in slot 0, so the native
    // post-shot bolt action must not run. Any thread.
    [[nodiscard]] bool ownsCycle() noexcept;

    // At F4SE load: registers the co-save record.
    void installSerialization();
}
