#pragma once

namespace paper
{
    class RockApiClient;
}

namespace paper::bolt_hold
{
    struct FrameInput
    {
        float deltaSeconds{ 0.0f };
        float closeSeconds{ 0.15f };
        bool enabled{ false };
        // The player's graph is in a reload, which is slot 0's reload.
        bool playerReloadActive{ false };
        // A PhysicalReload consumer owns slot 0's bolt.
        bool physicalReloadActive{ false };
    };

    /*
     * Leaves an empty magazine-fed gun's action open, on both weapon slots,
     * until a reload starts or rounds arrive. Runs once a frame in ROCK's
     * BeforeRock phase on its frame thread, which owns all of this state.
     *
     * The open pose comes from the weapon's own clips, across the bolt and the
     * parts that move with it. A reload's dwell gives the animator's locked-back
     * pose, and until one has played the farthest frame of any clip stands in. A
     * gun whose firing leaves the bolt alone has no pose before its first
     * reload, so its first empty magazine stays closed.
     *
     * ROCK applies the pose under a two-frame lease, so a lost owner or a missed
     * frame simply lets the graph have the bones back.
     */
    void update(RockApiClient& rock, const FrameInput& input);

    // Forgets the equipped weapons and ends any hold. Learned poses stay: they
    // belong to weapon forms, which hold for the whole process.
    void reset(RockApiClient& rock);
}
