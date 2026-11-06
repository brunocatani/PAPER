#pragma once

#include <cstdint>

namespace RE
{
    class Actor;
}

namespace rock::api::animation
{
    struct AnimationPhaseContext;
}

namespace paper
{
    class RockApiClient;
}

namespace paper::chamber_ejection
{
    enum class Kind
    {
        SpentCase,
        LiveRound,
    };

    // Checks the native code byte for byte and hooks it, once. False leaves the game
    // as it was: a spent case still ejects, a live round has nowhere to go.
    [[nodiscard]] bool install() noexcept;

    /*
     * What the bolt of the weapon in slot 0 throws out, through the game's own eject
     * function so it keeps its velocity and ROCK's casing grace.
     *
     * It leaves from the weapon's own P-Casing point on the visible first-person weapon,
     * found by the game's own search, with the game's throw turned to match.
     *
     * A spent case is the game's casing, built as ordinary world debris so it is seen
     * leaving the port. A live round is a real one-round ammo reference at the same place
     * with the same motion, which ROCK can grab and the game can pick up, and which then
     * leaves the inventory so no round is created. ROCK lets it out past the weapon and
     * the hands, as its casing grace does for a case. False leaves the inventory alone:
     * the round has gone back to the reserve instead.
     *
     * Call it in ROCK's AfterRock phase, once the weapon is placed for the frame.
     * `instance` is the slot's raw equipped instance, borrowed for the call.
     */
    [[nodiscard]] bool eject(RE::Actor* player, const void* instance, Kind kind) noexcept;

    // Once a frame in AfterRock: launches rounds whose physics have loaded. ROCK's frame
    // thread owns this and eject; the debris hook runs on any thread and only acts inside
    // an eject call on its own thread.
    void update(RockApiClient& rock, const rock::api::animation::AnimationPhaseContext& context) noexcept;

    // Forgets rounds waiting to launch. The rounds themselves stay in the world.
    void reset() noexcept;
}
