#pragma once
#include "Abi.h"

// PAPER's main interface: consumer registration, runtime and configuration
// state, captured native transforms and hand poses, native animation
// authority, frame events, reload observation and animation evidence, reload
// stages, the native pose pipeline, weapon motion and development capture.
// Records are value copies; no engine pointer crosses this boundary.
#pragma pack(push, 8)
namespace paper::api
{
    inline constexpr std::uint32_t kMaxConsumers = 16;
    inline constexpr std::uint32_t kMaxCallbacks = 16;
    inline constexpr std::uint32_t kMaxCapturedTransforms = 192;
    inline constexpr std::uint32_t kFingerTransformCount = 15;
    inline constexpr std::uint32_t kTransformNameCapacity = 64;
    inline constexpr std::uint32_t kMaxAuthorityLeaseFrames = 1200;
    inline constexpr std::uint32_t
        kMaxDevelopmentCaptureLeaseFrames = 1200;
    inline constexpr std::uint32_t kMaxReloadCatalogNodes = 2048;
    inline constexpr std::uint32_t kMaxReloadEvidence = 100;
    inline constexpr std::uint32_t kMaxReloadObservationTargets = 128;
    inline constexpr std::uint32_t kMaxReloadNodeObservations =
        kMaxReloadObservationTargets * 2;
    inline constexpr std::uint32_t kMaxReloadStageParts =
        kMaxReloadEvidence;
    inline constexpr std::uint32_t kMaxReloadEvidencePointsPerDetail = 252;
    inline constexpr std::uint32_t kMaxReloadEvidencePoints =
        kMaxReloadEvidence *
        kMaxReloadEvidencePointsPerDetail;
    inline constexpr std::uint32_t kReloadNodeNameCapacity = 64;
    inline constexpr std::uint32_t kReloadNodePathCapacity = 256;
    inline constexpr std::uint32_t kFormPluginNameCapacity = 64;
    inline constexpr std::uint32_t kFormEditorIdCapacity = 64;
    inline constexpr std::uint32_t kFormDisplayNameCapacity = 96;
    inline constexpr std::uint32_t kMaxReloadAnimationClips = 640;
    inline constexpr std::uint32_t kMaxReloadLiveAnimationClips = 128;
    inline constexpr std::uint32_t kMaxReloadExactAnimationClips = 512;
    inline constexpr std::uint32_t kMaxReloadLiveTracksPerClip = 64;
    inline constexpr std::uint32_t kMaxReloadExactTracksPerClip = 128;
    inline constexpr std::uint32_t kMaxReloadLiveSamplesPerTrack = 64;
    inline constexpr std::uint32_t kMaxReloadExactSamplesPerTrack = 720;
    inline constexpr std::uint32_t kMaxReloadSkeletonBones = 768;
    inline constexpr std::uint32_t kMaxReloadClipAnnotations = 128;
    inline constexpr std::uint32_t kMaxReloadClipTriggers = 128;
    inline constexpr std::uint32_t kReloadAnimationNameCapacity = 64;
    inline constexpr std::uint32_t kReloadAnimationPathCapacity = 260;
    inline constexpr std::uint32_t kReloadMarkerTextCapacity = 96;
    inline constexpr std::uint32_t kReloadResolvePointCapacity = 32;
    inline constexpr std::uint64_t kReloadAnimationSampleBudgetBytes =
        128ull * 1024ull * 1024ull;
    inline constexpr std::uint32_t kMaxWeaponMotionParts = 256;
    inline constexpr std::uint32_t kMaxWeaponMotionStages =
        kMaxWeaponMotionParts * 3;
    inline constexpr std::uint32_t kWeaponMotionKeyCount = 24;
    inline constexpr std::uint32_t kMaxWeaponMotionFollowers = 10;
    inline constexpr std::uint32_t kMaxWeaponMotionRecorders = 128;
    inline constexpr std::uint32_t kMaxWeaponMotionEvents =
        kMaxWeaponMotionRecorders + 16;
    inline constexpr std::uint32_t kWeaponMotionNameCapacity = 64;

    // Requested at registration; granted capabilities gate every owner-scoped
    // call. Bits are local to this interface major.
    enum class ConsumerCapability : std::uint32_t
    {
        None = 0,
        RuntimeState = 1u << 0,
        CapturedTransforms = 1u << 1,
        NativeHandPose = 1u << 2,
        AnimationAuthority = 1u << 3,
        FrameCallbacks = 1u << 4,
        ReloadObservations = 1u << 5,
        ReloadEvidenceGeometry = 1u << 6,
        // Full animation evidence retains the exact off-screen weapon-clip
        // preharvest contract in addition to passive/live telemetry.
        ReloadAnimationEvidence = 1u << 7,
        // Passive graph bindings, live activity, tracks, and raw markers only.
        // Requesting this bit never starts exact clip preharvest.
        ReloadAnimationTelemetry = 1u << 8,
        ReloadStageIdentification = 1u << 9,
        NativePosePipeline = 1u << 10,
        WeaponMotionCatalog = 1u << 11,
        WeaponMotionDiagnostics = 1u << 12,
        WeaponManipulationTelemetry = 1u << 13,
        DevelopmentCaptureControl = 1u << 14,
        All = (1u << 15) - 1u,
    };

    enum class DevelopmentCaptureMode : std::uint32_t
    {
        User = 0,
        Observe = 1,
        Harvest = 2,
        Capture = 3,
    };

    enum class WeaponMotionCacheAccess : std::uint32_t
    {
        Off = 0,
        ReadOnly = 1,
        ReadWrite = 2,
    };

    enum class DevelopmentCaptureScope : std::uint32_t
    {
        None = 0,
        PassiveObservation = 1u << 0,
        ExactAnimationHarvest = 1u << 1,
        WeaponMotionCompilation = 1u << 2,
        CompiledCacheRead = 1u << 3,
        CompiledCacheWrite = 1u << 4,
        LiveMotionLearning = 1u << 5,
        All = (1u << 6) - 1u,
    };

    enum class DevelopmentCaptureConfigFlag : std::uint32_t
    {
        None = 0,
        AutoStart = 1u << 0,
        AllowApiActivation = 1u << 1,
        HotReloadEnabled = 1u << 2,
    };

    enum class DevelopmentCaptureStateFlag : std::uint32_t
    {
        None = 0,
        AutoStartActive = 1u << 0,
        ApiActivationAllowed = 1u << 1,
        OwnerLeaseActive = 1u << 2,
        AggregateLeaseActive = 1u << 3,
        CacheLookupPending = 1u << 4,
        SessionCacheHit = 1u << 5,
        PersistentCacheHit = 1u << 6,
        HarvestCompleted = 1u << 7,
        HarvestFailed = 1u << 8,
    };

    enum class ReloadAnimationAcquisition : std::uint32_t
    {
        LoadedGraphBinding = 1,
        LiveClipActivation = 2,
        ExactWeaponPreharvest = 3,
    };

    enum class ReloadAnimationTrackSpace : std::uint32_t
    {
        RigBoneLocal = 1,
        WeaponRootLocal = 2,
    };

    enum class ReloadAnimationPreharvestState : std::uint32_t
    {
        Idle = 0,
        LoadingBaseGraphs = 1,
        LoadingWeaponSubgraph = 2,
        LoadingClip = 3,
        SamplingClip = 4,
        Completed = 5,
        Failed = 6,
    };

    enum class ReloadAnimationCatalogFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        PassiveCaptureActive = 1u << 1,
        PassiveWalkCompleted = 1u << 2,
        ExactPreharvestCompleted = 1u << 3,
        ExactPreharvestFailed = 1u << 4,
        ClipCapacityTruncated = 1u << 5,
        SampleStorageTruncated = 1u << 6,
        SkeletonAvailable = 1u << 7,
        PassiveCaptureDropped = 1u << 8,
        SceneNameCapacityTruncated = 1u << 9,
        PassiveWalkUnavailable = 1u << 10,
        PassiveHooksInstalled = 1u << 11,
        ExactLoadedGraphPathFallback = 1u << 12,
        PassiveHookInstallFailed = 1u << 13,
    };

    enum class ReloadAnimationClipFlag : std::uint32_t
    {
        None = 0,
        TracksTruncated = 1u << 0,
        SamplesTruncated = 1u << 1,
        AnnotationsTruncated = 1u << 2,
        TriggersTruncated = 1u << 3,
        AnimationNameTruncated = 1u << 4,
        AnimationPathTruncated = 1u << 5,
        HasActivityId = 1u << 6,
        HasAnnotations = 1u << 7,
        HasTriggers = 1u << 8,
        HasTargetTracks = 1u << 9,
    };

    enum class ReloadAnimationTrackFlag : std::uint32_t
    {
        None = 0,
        BoneNameValid = 1u << 0,
        BoneNameTruncated = 1u << 1,
        BoneIndexValid = 1u << 2,
        ParentBoneIndexValid = 1u << 3,
        TransformTrackIndexValid = 1u << 4,
        ReferenceTransformValid = 1u << 5,
        SamplesAvailable = 1u << 6,
    };

    enum class ReloadAnimationLiveFlag : std::uint32_t
    {
        None = 0,
        Active = 1u << 0,
        AnimationNameValid = 1u << 1,
    };

    enum class FormIdentityFlag : std::uint32_t
    {
        None = 0,
        Resolved = 1u << 0,
        PluginIdentityValid = 1u << 1,
        EditorIdValid = 1u << 2,
        DisplayNameValid = 1u << 3,
        PluginNameTruncated = 1u << 4,
        EditorIdTruncated = 1u << 5,
        DisplayNameTruncated = 1u << 6,
    };

    enum class WeaponClassificationFlag : std::uint32_t
    {
        None = 0,
        Available = 1u << 0,
        Valid = 1u << 1,
        FamilyAvailable = 1u << 2,
        PrimaryFamilyValid = 1u << 3,
    };

    /*
     * PAPER-owned mirrors of ROCK's raw classification vocabulary. The raw
     * fields stay numeric in WeaponClassification; these enums let consumers
     * interpret them without ROCK's headers.
     */
    enum class WeaponSizeClass : std::uint32_t
    {
        Melee = 0,
        Pistol = 1,
        Rifle = 2,
        Heavy = 3,
    };

    enum class WeaponClassificationSource : std::uint32_t
    {
        None = 0,
        Keyword = 1,
        WeaponData = 2,
        EquipSlot = 3,
    };

    enum class WeaponKeywordFlag : std::uint64_t
    {
        None = 0,
        Pistol = 1ull << 0,
        Rifle = 1ull << 1,
        Shotgun = 1ull << 2,
        AssaultRifle = 1ull << 3,
        Sniper = 1ull << 4,
        GaussRifle = 1ull << 5,
        LaserMusket = 1ull << 6,
        HeavyGun = 1ull << 7,
        HandToHand = 1ull << 8,
        Melee1H = 1ull << 9,
        Melee2H = 1ull << 10,
        Unarmed = 1ull << 11,
        Minigun = 1ull << 12,
        Fatman = 1ull << 13,
        MissileLauncher = 1ull << 14,
        GatlingLaser = 1ull << 15,
        Flamer = 1ull << 16,
        Cryolater = 1ull << 17,
        JunkJet = 1ull << 18,
        RailwayRifle = 1ull << 19,
        Broadsider = 1ull << 20,
        Syringer = 1ull << 21,
        FlareGun = 1ull << 22,
        GammaGun = 1ull << 23,
        AlienBlaster = 1ull << 24,
        Ripper = 1ull << 25,
        Shishkebab = 1ull << 26,
        Laser = 1ull << 27,
        Plasma = 1ull << 28,
        Ballistic = 1ull << 29,
        Thrown = 1ull << 30,
        Grenade = 1ull << 31,
        Mine = 1ull << 32,
        Explosive = 1ull << 33,
        Automatic = 1ull << 34,
    };

    enum class WeaponPrimaryFamily : std::uint32_t
    {
        Unknown = 0,
        Pistol = 1,
        Revolver = 2,
        Rifle = 3,
        AssaultRifle = 4,
        AKPattern = 5,
        ARPattern = 6,
        SubmachineGun = 7,
        Shotgun = 8,
        SniperRifle = 9,
        BoltActionWithMagazine = 10,
        BoltActionWithoutMagazine = 11,
        BoltAction = 12,
        LeverAction = 13,
        PumpAction = 14,
        BreakAction = 15,
        HeavyWeapon = 16,
        MachineGun = 17,
        LightMachineGun = 18,
        Minigun = 19,
        Launcher = 20,
        EnergyWeapon = 21,
        MeleeWeapon = 22,
        UnarmedWeapon = 23,
        ThrownWeapon = 24,
        ExplosiveWeapon = 25,
    };

    /*
     * Multi-label taxonomy: family, operating mechanism, feed evidence, and
     * the specific weapon families already represented by ROCK's raw keyword
     * mask may coexist. A primary family is supplied only as a convenient UI
     * summary; consumers should retain these flags for fine-grained policy.
     */
    enum class WeaponFamilyFlag : std::uint64_t
    {
        None = 0,
        Pistol = 1ull << 0,
        Revolver = 1ull << 1,
        Rifle = 1ull << 2,
        AssaultRifle = 1ull << 3,
        AKPattern = 1ull << 4,
        ARPattern = 1ull << 5,
        SubmachineGun = 1ull << 6,
        Shotgun = 1ull << 7,
        SniperRifle = 1ull << 8,
        BoltAction = 1ull << 9,
        BoltActionWithMagazine = 1ull << 10,
        BoltActionWithoutMagazine = 1ull << 11,
        LeverAction = 1ull << 12,
        PumpAction = 1ull << 13,
        BreakAction = 1ull << 14,
        HeavyWeapon = 1ull << 15,
        MachineGun = 1ull << 16,
        LightMachineGun = 1ull << 17,
        Minigun = 1ull << 18,
        Launcher = 1ull << 19,
        EnergyWeapon = 1ull << 20,
        BallisticWeapon = 1ull << 21,
        MeleeWeapon = 1ull << 22,
        UnarmedWeapon = 1ull << 23,
        ThrownWeapon = 1ull << 24,
        ExplosiveWeapon = 1ull << 25,
        MagazineFed = 1ull << 26,
        CylinderFed = 1ull << 27,
        SlideOperated = 1ull << 28,
        LaserCellFed = 1ull << 29,
        ShellFed = 1ull << 30,
        LooseRoundFed = 1ull << 31,
        Automatic = 1ull << 32,
        SemiAutomatic = 1ull << 33,
        ManualCycle = 1ull << 34,
        InternalFeed = 1ull << 35,
        SingleLoad = 1ull << 36,
        TubeFed = 1ull << 37,
        BeltFed = 1ull << 38,
        DrumFed = 1ull << 39,
        BullpupPattern = 1ull << 40,
        GaussRifle = 1ull << 41,
        LaserMusket = 1ull << 42,
        RailwayRifle = 1ull << 43,
        GatlingLaser = 1ull << 44,
        MissileLauncher = 1ull << 45,
        FatMan = 1ull << 46,
        Flamer = 1ull << 47,
        Cryolator = 1ull << 48,
        JunkJet = 1ull << 49,
        Broadsider = 1ull << 50,
        Syringer = 1ull << 51,
        FlareGun = 1ull << 52,
        GammaGun = 1ull << 53,
        AlienBlaster = 1ull << 54,
        LaserWeapon = 1ull << 55,
        PlasmaWeapon = 1ull << 56,
        Grenade = 1ull << 57,
        Mine = 1ull << 58,
        OneHandedMelee = 1ull << 59,
        TwoHandedMelee = 1ull << 60,
        HandToHand = 1ull << 61,
        Ripper = 1ull << 62,
        Shishkebab = 1ull << 63,
    };

    enum class WeaponFamilyEvidenceFlag : std::uint32_t
    {
        None = 0,
        RockKeyword = 1u << 0,
        RockSizeClass = 1u << 1,
        WeaponFlags = 1u << 2,
        PartTopology = 1u << 3,
        AnimationKeyword = 1u << 4,
        FormIdentityText = 1u << 5,
        PartEvidenceIncomplete = 1u << 6,
    };

    enum class ReloadCatalogStatusFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        NodeCatalogTruncated = 1u << 1,
        EvidenceTruncated = 1u << 2,
        GeometryComplete = 1u << 3,
        GeometryIncomplete = 1u << 4,
        ClassificationAvailable = 1u << 5,
        ClassificationValid = 1u << 6,
        EvidenceUnavailable = 1u << 7,
        FamilyClassificationAvailable = 1u << 8,
    };

    enum class ReloadNodeFlag : std::uint32_t
    {
        None = 0,
        IsNode = 1u << 0,
        LocalTransformValid = 1u << 1,
        WeaponLocalTransformValid = 1u << 2,
        NameTruncated = 1u << 3,
        PathTruncated = 1u << 4,
    };

    enum class ReloadEvidenceFlag : std::uint32_t
    {
        None = 0,
        SourceNodeResolved = 1u << 0,
        InteractionNodeResolved = 1u << 1,
        LocalBoundsValid = 1u << 2,
        GeometryPresent = 1u << 3,
        GeometryComplete = 1u << 4,
        GeometryTruncated = 1u << 5,
        SourceNameTruncated = 1u << 6,
    };

    enum class ReloadFrameStatusFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        CatalogValid = 1u << 1,
        NativeGraphOutputCaptured = 1u << 2,
        PostRockCaptured = 1u << 3,
        ObservationsTruncated = 1u << 4,
        TopologyMismatch = 1u << 5,
    };

    enum class ReloadNodeObservationFlag : std::uint32_t
    {
        None = 0,
        LocalTransformValid = 1u << 0,
        WeaponLocalTransformValid = 1u << 1,
    };

    enum class ReloadObservationPhase : std::uint32_t
    {
        NativeGraphOutput = 1,
        PostRock = 2,
    };

    enum class ReloadStageFlag : std::uint32_t
    {
        None = 0,
        Rest = 1u << 0,
        Fire = 1u << 1,
        SlideBack = 1u << 2,
        MagazineIn = 1u << 3,
        MagazineOut = 1u << 4,
        SlideForward = 1u << 5,
        BoltForward = 1u << 6,
        BoltBack = 1u << 7,
    };

    enum class ReloadStageStatusFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        Pistol = 1u << 1,
        ClassificationAvailable = 1u << 2,
        ClassificationValid = 1u << 3,
        NativeGraphOutputAvailable = 1u << 4,
        WeaponFireHookReady = 1u << 5,
        FireEventObserved = 1u << 6,
        FireCorrelationPending = 1u << 7,
        FireActivityCorrelated = 1u << 8,
        MagazineObserved = 1u << 9,
        SlideObserved = 1u << 10,
        PartDataIncomplete = 1u << 11,
        PartCapacityTruncated = 1u << 12,
        BoltObserved = 1u << 13,
    };

    enum class ReloadStagePartFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        BaselineValid = 1u << 1,
        CurrentValid = 1u << 2,
        SourceNodeSelected = 1u << 3,
        InteractionNodeFallback = 1u << 4,
        ContributesToAggregate = 1u << 5,
        AtRest = 1u << 6,
        Displaced = 1u << 7,
        Magazine = 1u << 8,
        Slide = 1u << 9,
        MagazineIn = 1u << 10,
        MagazineOut = 1u << 11,
        SlideBack = 1u << 12,
        SlideForward = 1u << 13,
        Bolt = 1u << 14,
        BoltForward = 1u << 15,
        BoltBack = 1u << 16,
    };

    enum class ReloadFireCorrelation : std::uint32_t
    {
        None = 0,
        EventOnly = 1,
        PendingActivity = 2,
        ActivityBound = 3,
    };

    enum class NativeAnimationAuthorityFlag : std::uint32_t
    {
        None = 0,
        Arms = 1u << 0,
        Hands = 1u << 1,
        Weapon = 1u << 2,
        ReloadPose = (1u << 0) | (1u << 1) | (1u << 2),
    };

    enum class RuntimeStatusFlag : std::uint32_t
    {
        None = 0,
        ProviderReady = 1u << 0,
        RockConnected = 1u << 1,
        RockSkeletonReady = 1u << 2,
        HookInstalled = 1u << 3,
        RuntimeEnabled = 1u << 4,
        CaptureValid = 1u << 5,
        ReloadEventActive = 1u << 6,
        ManualCycleActive = 1u << 7,
        HookInstallFailed = 1u << 8,
        ThreadMismatch = 1u << 9,
        CaptureFault = 1u << 10,
    };

    enum class Hand : std::uint32_t
    {
        Right = 0,
        Left = 1,
    };

    enum class NativePoseMode : std::uint32_t
    {
        Inactive = 0,
        FullReload = 1,
        PartialReload = 2,
        ManualCycle = 3,
        ConsumerAuthority = 4,
    };

    enum class NativePoseCompatibilityReason : std::uint32_t
    {
        None = 0,
        HandlingStateUnavailable = 1,
        LeftFiringHand = 2,
        PartCarry = 3,
        WeaponUnavailable = 4,
        WeaponIdentityChanged = 5,
        AnimationRequestResetRequired = 6,
    };

    enum class NativePoseApplicationResult : std::uint32_t
    {
        Inactive = 0,
        WaitingForCapture = 1,
        Applied = 2,
        CompatibilityRejected = 3,
        ApplyFailed = 4,
        RuntimeUnavailable = 5,
    };

    enum class NativePoseFrameFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        AuthorityRequested = 1u << 1,
        RuntimeOperational = 1u << 2,
        CapturePrepared = 1u << 3,
        BeforeRockApplied = 1u << 4,
        AfterRockApplied = 1u << 5,
        FullPose = 1u << 6,
        WeaponFixedHands = 1u << 7,
        PartialReload = 1u << 8,
        ManualCycle = 1u << 9,
        CompatibilityRejected = 1u << 10,
        ApplicationFailed = 1u << 11,
        RockWeaponWorldValid = 1u << 12,
        NativeWeaponBaselineWorldValid = 1u << 13,
        ResolvedWeaponWorldValid = 1u << 14,
        PresentedReadbackAvailable = 1u << 15,
    };

    enum class NativeHandRole : std::uint32_t
    {
        Primary = 0,
        Support = 1,
    };

    enum class NativeHandTargetMode : std::uint32_t
    {
        None = 0,
        LiveGripDelta = 1,
        NativeWeaponRelative = 2,
    };

    enum class NativeHandSolutionFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        NativeHandInWeaponValid = 1u << 1,
        NativeBaselineHandInWeaponValid = 1u << 2,
        RockBaselineHandInWeaponValid = 1u << 3,
        ResolvedTargetInWeaponValid = 1u << 4,
        ResolvedTargetWorldValid = 1u << 5,
        PresentedHandWorldValid = 1u << 6,
        NativeFingerLocalsValid = 1u << 7,
        PresentedFingerLocalsValid = 1u << 8,
        MotionGateApplicable = 1u << 9,
        MotionQualified = 1u << 10,
        TargetPublished = 1u << 11,
        TargetSuppressed = 1u << 12,
        ResidualValid = 1u << 13,
        PresentedPoseCoherent = 1u << 14,
    };

    enum class WeaponMotionSource : std::uint32_t
    {
        None = 0,
        ExactAuthored = 1,
        Learned = 2,
    };

    enum class WeaponMotionStageKind : std::uint32_t
    {
        Primary = 0,
        Return = 1,
    };

    enum class WeaponMotionRecorderPhase : std::uint32_t
    {
        WaitingForRest = 0,
        Armed = 1,
        Recording = 2,
    };

    enum class WeaponMotionServingDecision : std::uint32_t
    {
        None = 0,
        RejectedBelowNoise = 1,
        RejectedInvalidPath = 2,
        RejectedSmallerPrimary = 3,
        RejectedSmallerReturn = 4,
        NoServingSlot = 5,
        StoredPrimary = 6,
        ReplacedPrimary = 7,
        StoredReturn = 8,
        ReplacedReturn = 9,
    };

    enum class WeaponMotionStrokeTermination : std::uint32_t
    {
        None = 0,
        Settled = 1,
        ObservationLost = 2,
        SampleCapacity = 3,
        WeaponChanged = 4,
        DemandEnded = 5,
        RuntimeReset = 6,
    };

    enum class WeaponManipulationEndReason : std::uint32_t
    {
        None = 0,
        GripEnded = 1,
        WeaponChanged = 2,
        PathUnavailable = 3,
        ProviderReset = 4,
        DemandEnded = 5,
    };

    enum class WeaponMotionCatalogFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        BuildingAuthoredPaths = 1u << 1,
        ExactEvidenceAvailable = 1u << 2,
        LearningActive = 1u << 3,
        LearnedPathsAvailable = 1u << 4,
        AuthoredPathsAvailable = 1u << 5,
        PartsTruncated = 1u << 6,
        StagesTruncated = 1u << 7,
        FollowersTruncated = 1u << 8,
    };

    enum class WeaponMotionPartFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        BaselineValid = 1u << 1,
        CurrentValid = 1u << 2,
        SourceNodeSelected = 1u << 3,
        InteractionNodeFallback = 1u << 4,
        ExactAuthoredAvailable = 1u << 5,
        LearnedPrimaryAvailable = 1u << 6,
        LearnedReturnAvailable = 1u << 7,
        RecorderActive = 1u << 8,
    };

    enum class WeaponMotionStageFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        ExactAuthored = 1u << 1,
        Learned = 1u << 2,
        ReturnStage = 1u << 3,
        TravelExtremeValid = 1u << 4,
        FollowersTruncated = 1u << 5,
        LiveRebased = 1u << 6,
        HydratedFromCache = 1u << 7,
    };

    enum class WeaponMotionFollowerFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        Rigid = 1u << 1,
        CoTimed = 1u << 2,
        EvidenceMapped = 1u << 3,
        LiveRebased = 1u << 4,
        HydratedFromCache = 1u << 5,
    };

    enum class WeaponMotionLearningFlag : std::uint32_t
    {
        None = 0,
        Active = 1u << 0,
        NativeGraphObservations = 1u << 1,
        RecorderCapacityReached = 1u << 2,
        SampleCapacityReached = 1u << 3,
    };

    enum class WeaponMotionRecorderFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        ObservationValid = 1u << 1,
        CandidateValid = 1u << 2,
        ReturnCandidate = 1u << 3,
    };

    enum class WeaponManipulationFrameFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        RockGripStateAvailable = 1u << 1,
        AnyGripActive = 1u << 2,
        AnyMappedManipulation = 1u << 3,
    };

    enum class WeaponManipulationHandFlag : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        GripActive = 1u << 1,
        AttachOnly = 1u << 2,
        PartMapped = 1u << 3,
        PathAvailable = 1u << 4,
        ProgressValid = 1u << 5,
        AtRest = 1u << 6,
        AtMaximumTravel = 1u << 7,
        ReturnStage = 1u << 8,
    };

    enum class WeaponMotionStoreFlag : std::uint32_t
    {
        None = 0,
        Active = 1u << 0,
        InMemoryCatalog = 1u << 1,
        PersistentStorageAvailable = 1u << 2,
        RawObservationEvidenceAvailable = 1u << 3,
        RawAnimationEvidenceAvailable = 1u << 4,
        CompiledStageCacheEnabled = 1u << 5,
        CacheLookupPending = 1u << 6,
        SessionCacheHit = 1u << 7,
        PersistentCacheHit = 1u << 8,
        CompiledStageCacheWriteEnabled = 1u << 9,
    };

    enum class EventKind : std::uint32_t
    {
        FrameComplete = 1,
        RuntimeReset = 2,
        ConfigReloaded = 3,
        WeaponMotionCatalogChanged = 4,
        WeaponManipulationStarted = 5,
        WeaponManipulationStageChanged = 6,
        WeaponManipulationReachedMaximum = 7,
        WeaponManipulationReachedRest = 8,
        WeaponManipulationEnded = 9,
        WeaponMotionCandidateCompleted = 10,
        // Final native-pose readback is published. FrameComplete remains the
        // control/lease boundary; this observation does not advance leases.
        PresentationComplete = 11,
    };

    struct Transform
    {
        float rotate[3][3]{};
        float translate[3]{};
        float scale{ 1.0f };
    };

    struct ConsumerRegistration
    {
        std::uint32_t size{ sizeof(ConsumerRegistration) };
        std::uint32_t version{ kRecordVersion };
        char modName[64]{};
        std::uint32_t requestedCapabilities{ 0 };
        std::uint32_t reserved[7]{};
    };

    struct ConsumerHandle
    {
        std::uint32_t size{ sizeof(ConsumerHandle) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t ownerToken{ 0 };
        std::uint32_t grantedCapabilities{ 0 };
        std::uint32_t providerGeneration{ 0 };
        std::uint32_t reserved[6]{};
    };

    struct AuthorityRequest
    {
        std::uint32_t size{ sizeof(AuthorityRequest) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        // Zero is persistent until clear/unregister. Non-zero requests expire
        // after this many Paper frame callbacks.
        std::uint32_t leaseFrames{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ConfigState
    {
        std::uint32_t size{ sizeof(ConfigState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t enabled{ 0 };
        std::uint32_t nativeReloadAuthorityEnabled{ 0 };
        std::uint32_t partialReloadAuthorityEnabled{ 0 };
        std::int32_t logLevel{ 0 };
        std::uint64_t revision{ 0 };
        DevelopmentCaptureMode developmentCaptureMode{
            DevelopmentCaptureMode::User
        };
        WeaponMotionCacheAccess weaponMotionCacheAccess{
            WeaponMotionCacheAccess::Off
        };
        std::uint32_t developmentCaptureFlags{ 0 };
        std::uint32_t developmentCaptureAllowedScopes{ 0 };
        std::uint32_t reserved[4]{};
    };

    struct DevelopmentCaptureRequest
    {
        std::uint32_t size{ sizeof(DevelopmentCaptureRequest) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t scopes{ 0 };
        // Development capture is intentionally lease-only. A request must use
        // 1..kMaxDevelopmentCaptureLeaseFrames and be renewed by
        // its owner while capture remains desired.
        std::uint32_t leaseFrames{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct DevelopmentCaptureState
    {
        std::uint32_t size{ sizeof(DevelopmentCaptureState) };
        std::uint32_t version{ kRecordVersion };
        DevelopmentCaptureMode mode{
            DevelopmentCaptureMode::User
        };
        WeaponMotionCacheAccess cacheAccess{
            WeaponMotionCacheAccess::Off
        };
        std::uint32_t allowedScopes{ 0 };
        std::uint32_t autoStartScopes{ 0 };
        std::uint32_t ownerRequestedScopes{ 0 };
        std::uint32_t aggregateRequestedScopes{ 0 };
        std::uint32_t activeScopes{ 0 };
        std::uint32_t deniedScopes{ 0 };
        std::uint32_t remainingLeaseFrames{ 0 };
        std::uint32_t stateFlags{ 0 };
        std::uint64_t configRevision{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t storedSampleBytes{ 0 };
        std::uint64_t sampleStorageBudgetBytes{ 0 };
        std::uint32_t weaponFormId{ 0 };
        ReloadAnimationPreharvestState exactPreharvestState{
            ReloadAnimationPreharvestState::Idle
        };
        std::uint32_t animationStatusFlags{ 0 };
        std::uint32_t motionStoreFlags{ 0 };
        std::uint32_t animationClipCount{ 0 };
        std::uint32_t exactAnimationClipCount{ 0 };
        std::uint32_t exactClipsSampled{ 0 };
        std::uint32_t exactClipsRejected{ 0 };
        std::uint32_t motionPartCount{ 0 };
        std::uint32_t motionStageCount{ 0 };
        std::uint32_t persistentRecordCount{ 0 };
        std::uint32_t pendingWriteCount{ 0 };
        std::uint32_t reserved[4]{};
    };

    struct RuntimeState
    {
        std::uint32_t size{ sizeof(RuntimeState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t statusFlags{ 0 };
        std::uint32_t activeAuthorityFlags{ 0 };
        std::uint32_t localAuthorityFlags{ 0 };
        std::uint32_t consumerAuthorityFlags{ 0 };
        std::uint32_t capturedTransformCount{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t captureSequence{ 0 };
        std::uint64_t reloadStartSequence{ 0 };
        std::uint64_t reloadEndSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct CapturedTransform
    {
        std::uint32_t size{ sizeof(CapturedTransform) };
        std::uint32_t version{ kRecordVersion };
        char name[kTransformNameCapacity]{};
        Transform local{};
        std::uint32_t authorityFlags{ 0 };
        std::int32_t sourceIndex{ -1 };
        std::int32_t destinationIndex{ -1 };
        std::uint32_t reserved[4]{};
    };

    struct NativeHandPose
    {
        std::uint32_t size{ sizeof(NativeHandPose) };
        std::uint32_t version{ kRecordVersion };
        Hand hand{ Hand::Right };
        std::uint16_t fingerLocalTransformMask{ 0 };
        std::uint16_t reserved0{ 0 };
        Transform handInWeapon{};
        Transform fingerLocalTransforms[
            kFingerTransformCount]{};
        std::uint64_t captureSequence{ 0 };
        std::uint32_t reserved[8]{};
    };

    /*
     * Native-pose pipeline records are observational. They describe PAPER's
     * native animation decision and ROCK's final presented pose for the same
     * completed frame; they do not grant pose-write or IK authority.
     */
    struct NativePoseFrameState
    {
        std::uint32_t size{ sizeof(NativePoseFrameState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        NativePoseMode mode{ NativePoseMode::Inactive };
        NativePoseCompatibilityReason compatibilityReason{
            NativePoseCompatibilityReason::None
        };
        NativePoseApplicationResult applicationResult{
            NativePoseApplicationResult::Inactive
        };
        std::uint32_t activeAuthorityFlags{ 0 };
        std::uint32_t localAuthorityFlags{ 0 };
        std::uint32_t consumerAuthorityFlags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t captureSequence{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        Transform rockWeaponWorld{};
        Transform nativeWeaponBaselineWorld{};
        Transform resolvedWeaponWorld{};
        std::uint32_t reserved[5]{};
    };

    struct NativeHandSolution
    {
        std::uint32_t size{ sizeof(NativeHandSolution) };
        std::uint32_t version{ kRecordVersion };
        Hand hand{ Hand::Right };
        std::uint32_t flags{ 0 };
        NativeHandRole role{ NativeHandRole::Primary };
        NativeHandTargetMode targetMode{
            NativeHandTargetMode::None
        };
        std::uint16_t nativeFingerLocalTransformMask{ 0 };
        std::uint16_t presentedFingerLocalTransformMask{ 0 };
        std::uint32_t reserved0{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t captureSequence{ 0 };
        std::uint64_t presentationSequence{ 0 };
        Transform nativeHandInWeapon{};
        Transform nativeBaselineHandInWeapon{};
        Transform rockBaselineHandInWeapon{};
        Transform resolvedTargetInWeapon{};
        Transform resolvedTargetWorld{};
        Transform presentedHandWorld{};
        Transform nativeFingerLocalTransforms[
            kFingerTransformCount]{};
        Transform presentedFingerLocalTransforms[
            kFingerTransformCount]{};
        float motionTranslationGameUnits{ 0.0f };
        float motionRotationDegrees{ 0.0f };
        float residualTranslationGameUnits{ 0.0f };
        float residualRotationDegrees{ 0.0f };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct Event
    {
        std::uint32_t size{ sizeof(Event) };
        std::uint32_t version{ kRecordVersion };
        EventKind kind{ EventKind::FrameComplete };
        std::uint32_t reserved0{ 0 };
        RuntimeState runtime{};
        // Motion events identify a published snapshot and part. Other event
        // kinds leave these fields zeroed.
        std::uint64_t relatedSnapshotSequence{ 0 };
        std::uint64_t relatedCatalogSequence{ 0 };
        std::uint32_t relatedPartId{ 0xFFFF'FFFFu };
        Hand relatedHand{ Hand::Right };
        std::uint32_t relatedMotionFlags{ 0 };
        float relatedNormalizedProgress{ 0.0f };
    };

    /*
     * Reload observations are descriptive source data. PAPER does not infer
     * part motion, attachment, convergence, or reload stages from these
     * records. Scene transforms use Fallout game units. Weapon-local values
     * are lossless coordinate conversions relative to the observed weapon
     * root. Evidence bounds and points preserve ROCK's generated local
     * geometry coordinates unchanged.
     */
    struct Point3
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct Bounds3
    {
        Point3 min{};
        Point3 max{};
        std::uint32_t valid{ 0 };
        std::uint32_t reserved{ 0 };
    };

    struct FormIdentity
    {
        std::uint32_t size{ sizeof(FormIdentity) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t runtimeFormId{ 0 };
        std::uint32_t localFormId{ 0 };
        std::uint32_t formType{ 0 };
        char pluginName[kFormPluginNameCapacity]{};
        char editorId[kFormEditorIdCapacity]{};
        char displayName[kFormDisplayNameCapacity]{};
        std::uint32_t reserved[4]{};
    };

    struct WeaponClassification
    {
        std::uint32_t size{ sizeof(WeaponClassification) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t formId{ 0 };
        std::uint64_t keywordFlags{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint32_t sizeClass{ 0 };
        std::uint32_t source{ 0 };
        float confidence{ 0.0f };
        std::uint32_t provenanceFlags{ 0 };
        std::uint64_t familyFlags{ 0 };
        WeaponPrimaryFamily primaryFamily{
            WeaponPrimaryFamily::Unknown
        };
        std::uint32_t familyEvidenceFlags{ 0 };
    };

    struct ReloadObservationLimits
    {
        std::uint32_t size{ sizeof(ReloadObservationLimits) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t maxCatalogNodes{ 0 };
        std::uint32_t maxEvidenceRecords{ 0 };
        std::uint32_t maxObservationTargets{ 0 };
        std::uint32_t maxNodeObservations{ 0 };
        std::uint32_t maxEvidencePointsPerDetail{ 0 };
        std::uint32_t maxEvidencePointsTotal{ 0 };
        std::uint32_t nodeNameCapacity{ 0 };
        std::uint32_t nodePathCapacity{ 0 };
        std::uint32_t pluginNameCapacity{ 0 };
        std::uint32_t editorIdCapacity{ 0 };
        std::uint32_t displayNameCapacity{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ReloadCatalogState
    {
        std::uint32_t size{ sizeof(ReloadCatalogState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t statusFlags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t capturedFrameIndex{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint32_t discoveredNodeCount{ 0 };
        std::uint32_t nodeCount{ 0 };
        std::uint32_t omittedNodeCount{ 0 };
        std::uint32_t reportedEvidenceCount{ 0 };
        std::uint32_t evidenceCount{ 0 };
        std::uint32_t copiedGeometryPointCount{ 0 };
        WeaponClassification classification{};
        FormIdentity weapon{};
        Transform weaponRootWorld{};
        std::uint32_t reserved[5]{};
    };

    struct ReloadNodeCatalogEntry
    {
        std::uint32_t size{ sizeof(ReloadNodeCatalogEntry) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t nodeId{ 0 };
        std::int32_t parentNodeId{ -1 };
        std::uint32_t childIndex{ 0 };
        std::uint32_t sameNameSiblingOrdinal{ 0 };
        std::uint32_t flags{ 0 };
        std::uint32_t childCount{ 0 };
        char name[kReloadNodeNameCapacity]{};
        char rootRelativePath[kReloadNodePathCapacity]{};
        Transform baselineLocal{};
        Transform baselineWeaponLocal{};
        std::uint32_t reserved[4]{};
    };

    struct ReloadEvidence
    {
        std::uint32_t size{ sizeof(ReloadEvidence) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t evidenceId{ 0 };
        std::uint32_t bodyId{ 0x7FFF'FFFF };
        std::int32_t sourceNodeId{ -1 };
        std::int32_t interactionNodeId{ -1 };
        std::uint32_t flags{ 0 };
        std::uint32_t partKind{ 0 };
        std::uint32_t reloadRole{ 0 };
        std::uint32_t supportRole{ 0 };
        std::uint32_t socketRole{ 0 };
        std::uint32_t actionRole{ 0 };
        std::uint32_t fallbackGripPose{ 0 };
        std::uint32_t classificationSource{ 0 };
        char sourceName[kTransformNameCapacity]{};
        Bounds3 localBoundsGame{};
        std::uint32_t providerPointCount{ 0 };
        std::uint32_t copiedPointCount{ 0 };
        FormIdentity omod{};
        FormIdentity attachPoint{};
        std::uint32_t reserved[4]{};
    };

    struct ReloadFrameState
    {
        std::uint32_t size{ sizeof(ReloadFrameState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t statusFlags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        float deltaSeconds{ 0.0f };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint32_t observationTargetCount{ 0 };
        std::uint32_t observationCount{ 0 };
        std::uint32_t nativeGraphOutputCount{ 0 };
        std::uint32_t postRockCount{ 0 };
        std::uint32_t omittedObservationTargetCount{ 0 };
        Transform weaponRootWorld{};
        std::uint32_t reserved[5]{};
    };

    struct ReloadNodeObservation
    {
        std::uint32_t size{ sizeof(ReloadNodeObservation) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t nodeId{ 0 };
        ReloadObservationPhase phase{
            ReloadObservationPhase::NativeGraphOutput
        };
        std::uint32_t flags{ 0 };
        std::uint32_t reserved0{ 0 };
        std::uint64_t frameIndex{ 0 };
        Transform local{};
        Transform weaponLocal{};
        std::uint32_t reserved[4]{};
    };

    /*
     * Reload animation evidence is raw authored/runtime data. PAPER does not
     * classify clips, infer moving parts, choose reload stages, or correlate
     * marker text with scene-node motion. Consumers receive provenance,
     * hierarchy, timing, and transforms exactly as captured.
     *
     * ReloadQsTransform stores translation xyz, quaternion xyzw, and
     * independent xyz scale. Exact samples are weapon-root-local; passive
     * live-binding samples are rig-bone-local.
     */
    struct ReloadQsTransform
    {
        float translate[3]{};
        float rotate[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
        float scale[3]{ 1.0f, 1.0f, 1.0f };
    };

    /*
     * Weapon-motion records are PAPER-owned, demand-gated derived data. Exact
     * stages come from PAPER's immutable weapon-local animation evidence;
     * learned stages come only from NativeGraphOutput observations. No record
     * grants scene, animation, or drive authority.
     */
    struct WeaponMotionLimits
    {
        std::uint32_t size{ sizeof(WeaponMotionLimits) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t maxParts{ 0 };
        std::uint32_t maxStages{ 0 };
        std::uint32_t keysPerStage{ 0 };
        std::uint32_t maxFollowersPerStage{ 0 };
        std::uint32_t maxRecorders{ 0 };
        std::uint32_t nameCapacity{ 0 };
        std::uint32_t maxPendingEvents{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct WeaponMotionCatalogState
    {
        std::uint32_t size{ sizeof(WeaponMotionCatalogState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t observationCatalogSequence{ 0 };
        std::uint64_t observationSnapshotSequence{ 0 };
        std::uint64_t animationCatalogSequence{ 0 };
        std::uint64_t animationCatalogRevision{ 0 };
        std::uint64_t learningRevision{ 0 };
        std::uint64_t authoredRevision{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint32_t partCount{ 0 };
        std::uint32_t stageCount{ 0 };
        std::uint32_t followerCount{ 0 };
        std::uint32_t activeRecorderCount{ 0 };
        std::uint32_t processedExactClipCount{ 0 };
        std::uint32_t pendingExactClipCount{ 0 };
        std::uint32_t omittedPartCount{ 0 };
        std::uint32_t omittedStageCount{ 0 };
        std::uint32_t omittedFollowerCount{ 0 };
        std::uint32_t reserved[7]{};
    };

    struct WeaponMotionPart
    {
        std::uint32_t size{ sizeof(WeaponMotionPart) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t partId{ 0xFFFF'FFFFu };
        std::uint32_t evidenceId{ 0xFFFF'FFFFu };
        std::uint32_t bodyId{ 0x7FFF'FFFFu };
        std::int32_t sourceNodeId{ -1 };
        std::int32_t interactionNodeId{ -1 };
        std::int32_t selectedNodeId{ -1 };
        std::uint32_t flags{ 0 };
        std::uint32_t partKind{ 0 };
        std::uint32_t reloadRole{ 0 };
        std::uint32_t supportRole{ 0 };
        std::uint32_t socketRole{ 0 };
        std::uint32_t actionRole{ 0 };
        std::uint32_t fallbackGripPose{ 0 };
        std::uint32_t classificationSource{ 0 };
        std::uint32_t stageCount{ 0 };
        char sourceName[kWeaponMotionNameCapacity]{};
        FormIdentity omod{};
        Transform baselineWeaponLocal{};
        Transform currentWeaponLocal{};
        std::uint32_t reserved[6]{};
    };

    struct WeaponMotionStage
    {
        std::uint32_t size{ sizeof(WeaponMotionStage) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t stageId{ 0xFFFF'FFFFu };
        std::uint32_t partId{ 0xFFFF'FFFFu };
        WeaponMotionSource source{ WeaponMotionSource::None };
        WeaponMotionStageKind kind{
            WeaponMotionStageKind::Primary
        };
        std::uint32_t flags{ 0 };
        std::uint32_t sourceClipId{ 0xFFFF'FFFFu };
        // Source-sample index at the primary/return travel extreme.
        std::uint32_t sourceBoundary{ 0 };
        std::uint32_t keyCount{ 0 };
        std::uint32_t followerCount{ 0 };
        // Arc distance is the PAPER pose metric: translation in game units
        // plus three game-unit equivalents per radian of rotation.
        float totalArcLengthGameUnits{ 0.0f };
        float maximumTravelArcPosition{ 0.0f };
        float restArcPosition{ 0.0f };
        float peakDeltaGameUnits{ 0.0f };
        float restDeltaGameUnits{ 0.0f };
        ReloadQsTransform start{};
        ReloadQsTransform end{};
        ReloadQsTransform restReference{};
        std::uint32_t reserved[6]{};
    };

    struct WeaponMotionFollower
    {
        std::uint32_t size{ sizeof(WeaponMotionFollower) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t stageId{ 0xFFFF'FFFFu };
        std::uint32_t followerIndex{ 0 };
        std::uint32_t partId{ 0xFFFF'FFFFu };
        std::uint32_t evidenceId{ 0xFFFF'FFFFu };
        std::uint32_t bodyId{ 0x7FFF'FFFFu };
        std::uint32_t flags{ 0 };
        float restScale{ 1.0f };
        std::uint32_t keyCount{ 0 };
        char sourceName[kWeaponMotionNameCapacity]{};
        std::uint32_t reserved[6]{};
    };

    struct WeaponMotionLearningState
    {
        std::uint32_t size{ sizeof(WeaponMotionLearningState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t learningRevision{ 0 };
        std::uint32_t recorderCount{ 0 };
        std::uint32_t activeRecorderCount{ 0 };
        std::uint64_t observationCount{ 0 };
        std::uint64_t completedStrokeCount{ 0 };
        std::uint64_t acceptedStrokeCount{ 0 };
        std::uint64_t rejectedStrokeCount{ 0 };
        std::uint64_t interruptedStrokeCount{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct WeaponMotionRecorder
    {
        std::uint32_t size{ sizeof(WeaponMotionRecorder) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t recorderId{ 0xFFFF'FFFFu };
        std::uint32_t partId{ 0xFFFF'FFFFu };
        std::uint32_t flags{ 0 };
        WeaponMotionRecorderPhase phase{
            WeaponMotionRecorderPhase::WaitingForRest
        };
        std::uint32_t sampleCount{ 0 };
        std::uint32_t stableFrameCount{ 0 };
        std::uint64_t startFrameIndex{ 0 };
        std::uint64_t lastObservedFrameIndex{ 0 };
        float peakExcursionGameUnits{ 0.0f };
        float recordedArcLengthGameUnits{ 0.0f };
        WeaponMotionStrokeTermination lastTermination{
            WeaponMotionStrokeTermination::None
        };
        WeaponMotionServingDecision lastDecision{
            WeaponMotionServingDecision::None
        };
        ReloadQsTransform restPose{};
        ReloadQsTransform currentPose{};
        std::uint32_t reserved[6]{};
    };

    struct WeaponManipulationFrameState
    {
        std::uint32_t size{ sizeof(WeaponManipulationFrameState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t eventSequence{ 0 };
        std::uint32_t activeHandCount{ 0 };
        std::uint32_t mappedHandCount{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct WeaponManipulationHandState
    {
        std::uint32_t size{ sizeof(WeaponManipulationHandState) };
        std::uint32_t version{ kRecordVersion };
        Hand hand{ Hand::Right };
        std::uint32_t flags{ 0 };
        WeaponMotionSource source{ WeaponMotionSource::None };
        WeaponMotionStageKind stageKind{
            WeaponMotionStageKind::Primary
        };
        WeaponManipulationEndReason lastEndReason{
            WeaponManipulationEndReason::None
        };
        std::uint32_t rockGripKind{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t gripSequence{ 0 };
        std::uint32_t partId{ 0xFFFF'FFFFu };
        std::uint32_t evidenceId{ 0xFFFF'FFFFu };
        std::uint32_t bodyId{ 0x7FFF'FFFFu };
        std::uint32_t stageId{ 0xFFFF'FFFFu };
        float arcPositionGameUnits{ 0.0f };
        float normalizedProgress{ 0.0f };
        float projectionResidualGameUnits{ 0.0f };
        float maximumTravelArcPosition{ 0.0f };
        float restArcPosition{ 0.0f };
        ReloadQsTransform currentPartWeaponLocal{};
        ReloadQsTransform projectedPartWeaponLocal{};
        std::uint32_t reserved[8]{};
    };

    struct WeaponMotionStoreState
    {
        std::uint32_t size{ sizeof(WeaponMotionStoreState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t learningRevision{ 0 };
        std::uint64_t authoredRevision{ 0 };
        std::uint64_t rawObservationSnapshotSequence{ 0 };
        std::uint64_t rawAnimationCatalogSequence{ 0 };
        std::uint64_t rawAnimationCatalogRevision{ 0 };
        std::uint32_t inMemoryPartCount{ 0 };
        std::uint32_t inMemoryStageCount{ 0 };
        std::uint32_t persistentRecordCount{ 0 };
        std::uint32_t pendingWriteCount{ 0 };
        std::uint64_t droppedCaptureCount{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ReloadAnimationLimits
    {
        std::uint32_t size{ sizeof(ReloadAnimationLimits) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t sampleStorageBudgetBytes{ 0 };
        std::uint32_t maxClips{ 0 };
        std::uint32_t maxLiveClips{ 0 };
        std::uint32_t maxExactClips{ 0 };
        std::uint32_t maxLiveTracksPerClip{ 0 };
        std::uint32_t maxExactTracksPerClip{ 0 };
        std::uint32_t maxLiveSamplesPerTrack{ 0 };
        std::uint32_t maxExactSamplesPerTrack{ 0 };
        std::uint32_t maxSkeletonBones{ 0 };
        std::uint32_t maxAnnotationsPerClip{ 0 };
        std::uint32_t maxTriggersPerClip{ 0 };
        std::uint32_t animationNameCapacity{ 0 };
        std::uint32_t animationPathCapacity{ 0 };
        std::uint32_t markerTextCapacity{ 0 };
        std::uint32_t resolvePointCapacity{ 0 };
        std::uint32_t reserved[6]{};
    };

    struct ReloadAnimationCatalogState
    {
        std::uint32_t size{ sizeof(ReloadAnimationCatalogState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t statusFlags{ 0 };
        ReloadAnimationPreharvestState exactPreharvestState{
            ReloadAnimationPreharvestState::Idle
        };
        std::uint32_t weaponFormId{ 0 };
        std::uint32_t paperProviderGeneration{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t reloadCatalogSequence{ 0 };
        std::uint64_t catalogRevision{ 0 };
        std::uint64_t updatedFrameIndex{ 0 };
        std::uint32_t worldGeneration{ 0 };
        std::uint32_t skeletonGeneration{ 0 };
        std::uint32_t rockProviderGeneration{ 0 };
        std::uint32_t clipCount{ 0 };
        std::uint32_t liveClipCount{ 0 };
        std::uint32_t exactClipCount{ 0 };
        std::uint32_t omittedClipCount{ 0 };
        std::uint32_t skeletonBoneCount{ 0 };
        std::uint32_t exactAnimationFileCount{ 0 };
        std::uint32_t exactClipsSampled{ 0 };
        std::uint32_t exactClipsRejected{ 0 };
        std::uint32_t exactTargetTruncationCount{ 0 };
        std::uint32_t passiveWalkAttempts{ 0 };
        std::uint32_t passiveCandidateManagerCount{ 0 };
        std::uint32_t passiveBindingManagerAvailable{ 0 };
        char passiveLastResolvePoint[
            kReloadResolvePointCapacity]{};
        std::uint64_t storedSampleBytes{ 0 };
        std::uint64_t sampleStorageBudgetBytes{ 0 };
        std::uint64_t passiveCaptureDropCount{ 0 };
        // Passive hook/walk counters are process-cumulative diagnostics;
        // weapon/catalog identity above defines the records in this catalog.
        std::uint64_t passiveBindingsSeen{ 0 };
        std::uint64_t passiveBindingsCaptured{ 0 };
        std::uint64_t passiveBindingsWithoutTargets{ 0 };
        std::uint64_t passiveSkippedNonSpline{ 0 };
        std::uint64_t passiveWalksCompleted{ 0 };
        std::uint64_t passiveRejectedAnimationPointer{ 0 };
        std::uint64_t passiveRejectedClipParameters{ 0 };
        std::uint64_t passiveRejectedTrackMap{ 0 };
        std::uint64_t passiveRejectedBoneCount{ 0 };
        std::uint64_t passiveRejectedSampler{ 0 };
        std::uint64_t passiveRejectedSplineData{ 0 };
        std::uint64_t passiveHookCalls{ 0 };
        std::uint64_t passiveHookMatches{ 0 };
        std::uint32_t reserved[6]{};
    };

    struct ReloadAnimationLiveState
    {
        std::uint32_t size{ sizeof(ReloadAnimationLiveState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::uint32_t concurrentActivityCount{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t activityId{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint32_t reserved0{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        char animationName[kReloadAnimationNameCapacity]{};
        float durationSeconds{ 0.0f };
        float cropStartSeconds{ 0.0f };
        float croppedDurationSeconds{ 0.0f };
        float localTimeSeconds{ 0.0f };
        float fraction{ 0.0f };
        std::uint32_t reserved[6]{};
    };

    struct ReloadAnimationClip
    {
        std::uint32_t size{ sizeof(ReloadAnimationClip) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t clipId{ 0 };
        ReloadAnimationAcquisition acquisition{
            ReloadAnimationAcquisition::LoadedGraphBinding
        };
        ReloadAnimationTrackSpace trackSpace{
            ReloadAnimationTrackSpace::RigBoneLocal
        };
        std::uint32_t flags{ 0 };
        std::uint64_t activityId{ 0 };
        char animationName[kReloadAnimationNameCapacity]{};
        char animationPath[kReloadAnimationPathCapacity]{};
        float durationSeconds{ 0.0f };
        std::int32_t animationType{ -1 };
        std::uint32_t rawTransformTrackCount{ 0 };
        std::int32_t rawFloatTrackCount{ -1 };
        std::uint32_t sampleCount{ 0 };
        std::uint32_t trackCount{ 0 };
        std::int32_t rawAnnotationTrackCount{ -1 };
        std::int32_t rawTriggerCount{ -1 };
        std::int32_t graphEventNameCount{ -1 };
        std::uint32_t annotationCount{ 0 };
        std::uint32_t triggerCount{ 0 };
        std::uint32_t reserved[5]{};
    };

    struct ReloadAnimationTrack
    {
        std::uint32_t size{ sizeof(ReloadAnimationTrack) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t clipId{ 0 };
        std::uint32_t trackId{ 0 };
        std::uint32_t flags{ 0 };
        std::int32_t boneIndex{ -1 };
        std::int32_t parentBoneIndex{ -1 };
        std::int32_t transformTrackIndex{ -1 };
        std::uint32_t chainDepth{ 0 };
        std::uint32_t sampleCount{ 0 };
        char boneName[kReloadAnimationNameCapacity]{};
        ReloadQsTransform referenceLocal{};
        std::uint32_t reserved[5]{};
    };

    struct ReloadAnimationSample
    {
        std::uint32_t size{ sizeof(ReloadAnimationSample) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t clipId{ 0 };
        std::uint32_t trackId{ 0 };
        std::uint32_t sampleIndex{ 0 };
        float timeSeconds{ 0.0f };
        ReloadQsTransform transform{};
        std::uint32_t reserved[4]{};
    };

    struct ReloadAnimationAnnotation
    {
        std::uint32_t size{ sizeof(ReloadAnimationAnnotation) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t clipId{ 0 };
        std::uint32_t annotationId{ 0 };
        float timeSeconds{ 0.0f };
        char trackName[kReloadAnimationNameCapacity]{};
        char text[kReloadMarkerTextCapacity]{};
        std::uint32_t reserved[4]{};
    };

    struct ReloadAnimationTrigger
    {
        std::uint32_t size{ sizeof(ReloadAnimationTrigger) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t clipId{ 0 };
        std::uint32_t triggerId{ 0 };
        float localTimeSeconds{ 0.0f };
        std::int32_t eventId{ -1 };
        char eventName[kReloadMarkerTextCapacity]{};
        std::uint32_t reserved[4]{};
    };

    struct ReloadAnimationSkeletonBone
    {
        std::uint32_t size{ sizeof(ReloadAnimationSkeletonBone) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t flags{ 0 };
        std::int32_t boneIndex{ -1 };
        std::int32_t parentBoneIndex{ -1 };
        std::int32_t transformTrackIndex{ -1 };
        char boneName[kReloadAnimationNameCapacity]{};
        ReloadQsTransform referenceLocal{};
        std::uint32_t reserved[5]{};
    };

    /*
     * Provisional pistol stage identification is derived separately from the
     * raw observation and animation records above. Flags are independent and
     * may coexist. Magazine, slide, and bolt flags require every unique tracked
     * member of their classified group to match; incomplete members keep the
     * group inactive. Per-part records independently retain the
     * baseline/current transforms and exact deltas that caused each aggregate
     * decision.
     */
    struct ReloadStageState
    {
        std::uint32_t size{ sizeof(ReloadStageState) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t statusFlags{ 0 };
        std::uint32_t stageFlags{ 0 };
        ReloadFireCorrelation fireCorrelation{
            ReloadFireCorrelation::None
        };
        std::uint32_t weaponFormId{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t snapshotSequence{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint64_t fireSequence{ 0 };
        std::uint64_t fireActivityOrderAtEvent{ 0 };
        std::uint64_t lastObservedActivityOrder{ 0 };
        std::uint32_t partCount{ 0 };
        std::uint32_t aggregatePartCount{ 0 };
        std::uint32_t atRestPartCount{ 0 };
        std::uint32_t displacedPartCount{ 0 };
        std::uint32_t magazinePartCount{ 0 };
        std::uint32_t magazineInCount{ 0 };
        std::uint32_t magazineOutCount{ 0 };
        std::uint32_t slidePartCount{ 0 };
        std::uint32_t slideBackCount{ 0 };
        std::uint32_t activeAnimationActivityCount{ 0 };
        std::uint32_t correlatedFireActivityCount{ 0 };
        std::uint32_t fireAdmissionFramesRemaining{ 0 };
        float restTranslationToleranceGameUnits{ 0.0f };
        float restRotationToleranceDegrees{ 0.0f };
        float restScaleTolerance{ 0.0f };
        float maximumTranslationDeltaGameUnits{ 0.0f };
        float maximumRotationDeltaDegrees{ 0.0f };
        float maximumScaleDelta{ 0.0f };
        std::uint32_t slideForwardCount{ 0 };
        std::uint32_t boltPartCount{ 0 };
        std::uint32_t boltForwardCount{ 0 };
        std::uint32_t boltBackCount{ 0 };
        std::uint32_t reserved[2]{};
    };

    struct ReloadStagePart
    {
        std::uint32_t size{ sizeof(ReloadStagePart) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t evidenceId{ 0 };
        std::uint32_t bodyId{ 0x7FFF'FFFF };
        std::int32_t sourceNodeId{ -1 };
        std::int32_t interactionNodeId{ -1 };
        std::int32_t selectedNodeId{ -1 };
        std::uint32_t flags{ 0 };
        std::uint32_t partKind{ 0 };
        std::uint32_t actionRole{ 0 };
        std::uint64_t frameIndex{ 0 };
        Transform baselineWeaponLocal{};
        Transform currentWeaponLocal{};
        float translationDeltaGameUnits{ 0.0f };
        float rotationDeltaDegrees{ 0.0f };
        float scaleDelta{ 0.0f };
        std::uint32_t reserved[4]{};
    };

    // Game thread, inside PAPER's frame. eventData is borrowed for the call.
    using EventCallback =
        void(PAPER_CALL*)(const Event* eventData, void* userData);

    static_assert(sizeof(Transform) == 52);
    static_assert(alignof(Transform) == 4);
    static_assert(std::is_standard_layout_v<Transform>);
    static_assert(std::is_trivially_copyable_v<Transform>);
    static_assert(sizeof(ConsumerRegistration) == 104);
    static_assert(alignof(ConsumerRegistration) == 4);
    static_assert(std::is_standard_layout_v<ConsumerRegistration>);
    static_assert(std::is_trivially_copyable_v<ConsumerRegistration>);
    static_assert(sizeof(ConsumerHandle) == 48);
    static_assert(alignof(ConsumerHandle) == 8);
    static_assert(std::is_standard_layout_v<ConsumerHandle>);
    static_assert(std::is_trivially_copyable_v<ConsumerHandle>);
    static_assert(sizeof(AuthorityRequest) == 48);
    static_assert(alignof(AuthorityRequest) == 4);
    static_assert(std::is_standard_layout_v<AuthorityRequest>);
    static_assert(std::is_trivially_copyable_v<AuthorityRequest>);
    static_assert(sizeof(ConfigState) == 64);
    static_assert(alignof(ConfigState) == 8);
    static_assert(std::is_standard_layout_v<ConfigState>);
    static_assert(std::is_trivially_copyable_v<ConfigState>);
    static_assert(sizeof(DevelopmentCaptureRequest) == 48);
    static_assert(alignof(DevelopmentCaptureRequest) == 4);
    static_assert(std::is_standard_layout_v<DevelopmentCaptureRequest>);
    static_assert(std::is_trivially_copyable_v<DevelopmentCaptureRequest>);
    static_assert(sizeof(DevelopmentCaptureState) == 144);
    static_assert(alignof(DevelopmentCaptureState) == 8);
    static_assert(std::is_standard_layout_v<DevelopmentCaptureState>);
    static_assert(std::is_trivially_copyable_v<DevelopmentCaptureState>);
    static_assert(sizeof(RuntimeState) == 120);
    static_assert(alignof(RuntimeState) == 8);
    static_assert(std::is_standard_layout_v<RuntimeState>);
    static_assert(std::is_trivially_copyable_v<RuntimeState>);
    static_assert(sizeof(CapturedTransform) == 152);
    static_assert(alignof(CapturedTransform) == 4);
    static_assert(std::is_standard_layout_v<CapturedTransform>);
    static_assert(std::is_trivially_copyable_v<CapturedTransform>);
    static_assert(sizeof(NativeHandPose) == 888);
    static_assert(alignof(NativeHandPose) == 8);
    static_assert(std::is_standard_layout_v<NativeHandPose>);
    static_assert(std::is_trivially_copyable_v<NativeHandPose>);
    static_assert(sizeof(NativePoseFrameState) == 264);
    static_assert(alignof(NativePoseFrameState) == 8);
    static_assert(std::is_standard_layout_v<NativePoseFrameState>);
    static_assert(std::is_trivially_copyable_v<NativePoseFrameState>);
    static_assert(sizeof(NativeHandSolution) == 2000);
    static_assert(alignof(NativeHandSolution) == 8);
    static_assert(std::is_standard_layout_v<NativeHandSolution>);
    static_assert(std::is_trivially_copyable_v<NativeHandSolution>);
    static_assert(sizeof(Event) == 168);
    static_assert(alignof(Event) == 8);
    static_assert(std::is_standard_layout_v<Event>);
    static_assert(std::is_trivially_copyable_v<Event>);
    static_assert(sizeof(Point3) == 12);
    static_assert(sizeof(Bounds3) == 32);
    static_assert(sizeof(FormIdentity) == 264);
    static_assert(sizeof(WeaponClassification) == 64);
    static_assert(sizeof(ReloadObservationLimits) == 84);
    static_assert(sizeof(ReloadCatalogState) == 480);
    static_assert(sizeof(ReloadNodeCatalogEntry) == 472);
    static_assert(sizeof(ReloadEvidence) == 704);
    static_assert(sizeof(ReloadFrameState) == 160);
    static_assert(sizeof(ReloadNodeObservation) == 152);
    static_assert(sizeof(ReloadStageState) == 176);
    static_assert(std::is_standard_layout_v<ReloadStageState>);
    static_assert(std::is_trivially_copyable_v<ReloadStageState>);
    static_assert(sizeof(ReloadStagePart) == 184);
    static_assert(std::is_standard_layout_v<ReloadStagePart>);
    static_assert(std::is_trivially_copyable_v<ReloadStagePart>);
    static_assert(sizeof(ReloadQsTransform) == 40);
    static_assert(sizeof(WeaponMotionLimits) == 68);
    static_assert(sizeof(WeaponMotionCatalogState) == 168);
    static_assert(sizeof(WeaponMotionPart) == 524);
    static_assert(sizeof(WeaponMotionStage) == 208);
    static_assert(sizeof(WeaponMotionFollower) == 128);
    static_assert(sizeof(WeaponMotionLearningState) == 128);
    static_assert(sizeof(WeaponMotionRecorder) == 168);
    static_assert(sizeof(WeaponManipulationFrameState) == 112);
    static_assert(sizeof(WeaponManipulationHandState) == 208);
    static_assert(sizeof(WeaponMotionStoreState) == 136);
    static_assert(std::is_standard_layout_v<WeaponMotionLimits>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionLimits>);
    static_assert(std::is_standard_layout_v<WeaponMotionCatalogState>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionCatalogState>);
    static_assert(std::is_standard_layout_v<WeaponMotionPart>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionPart>);
    static_assert(std::is_standard_layout_v<WeaponMotionStage>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionStage>);
    static_assert(std::is_standard_layout_v<WeaponMotionFollower>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionFollower>);
    static_assert(std::is_standard_layout_v<WeaponMotionLearningState>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionLearningState>);
    static_assert(std::is_standard_layout_v<WeaponMotionRecorder>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionRecorder>);
    static_assert(std::is_standard_layout_v<WeaponManipulationFrameState>);
    static_assert(std::is_trivially_copyable_v<WeaponManipulationFrameState>);
    static_assert(std::is_standard_layout_v<WeaponManipulationHandState>);
    static_assert(std::is_trivially_copyable_v<WeaponManipulationHandState>);
    static_assert(std::is_standard_layout_v<WeaponMotionStoreState>);
    static_assert(std::is_trivially_copyable_v<WeaponMotionStoreState>);
    static_assert(std::is_standard_layout_v<ReloadAnimationCatalogState>);
    static_assert(std::is_trivially_copyable_v<ReloadAnimationCatalogState>);
    static_assert(std::is_standard_layout_v<ReloadAnimationClip>);
    static_assert(std::is_trivially_copyable_v<ReloadAnimationClip>);
    static_assert(std::is_standard_layout_v<ReloadAnimationTrack>);
    static_assert(std::is_trivially_copyable_v<ReloadAnimationTrack>);
    static_assert(std::is_standard_layout_v<ReloadAnimationSample>);
    static_assert(std::is_trivially_copyable_v<ReloadAnimationSample>);
    static_assert(std::is_standard_layout_v<ReloadCatalogState>);
    static_assert(std::is_trivially_copyable_v<ReloadCatalogState>);
    static_assert(std::is_standard_layout_v<ReloadNodeCatalogEntry>);
    static_assert(std::is_trivially_copyable_v<ReloadNodeCatalogEntry>);
    static_assert(std::is_standard_layout_v<ReloadEvidence>);
    static_assert(std::is_trivially_copyable_v<ReloadEvidence>);
    static_assert(std::is_standard_layout_v<ReloadFrameState>);
    static_assert(std::is_trivially_copyable_v<ReloadFrameState>);
    static_assert(std::is_standard_layout_v<ReloadNodeObservation>);
    static_assert(std::is_trivially_copyable_v<ReloadNodeObservation>);
    static_assert(sizeof(ReloadAnimationLimits) == 96);
}

namespace paper::api::provider
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::Provider;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    /*
     * getModVersion and isReady run on any thread. Every other call is
     * game-thread only and fails closed on a wrong thread or when ownerToken
     * lacks the matching granted capability. Returned records are value
     * copies; event pointers are valid only for the callback.
     */
    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;
        // PAPER's own version string (for example "1.2.0"), valid for the
        // process lifetime.
        const char*(PAPER_CALL* getModVersion)();
        bool(PAPER_CALL* isReady)();
        bool(PAPER_CALL* getRuntimeState)(
            OwnerToken ownerToken,
            RuntimeState* outState);
        bool(PAPER_CALL* getConfigState)(
            OwnerToken ownerToken,
            ConfigState* outState);
        std::uint32_t(PAPER_CALL* getCapturedTransformCount)(
            OwnerToken ownerToken);
        std::uint32_t(PAPER_CALL* copyCapturedTransforms)(
            OwnerToken ownerToken,
            CapturedTransform* outTransforms,
            std::uint32_t maxTransforms);
        bool(PAPER_CALL* getNativeHandPose)(
            OwnerToken ownerToken,
            Hand hand,
            NativeHandPose* outPose);
        Status(PAPER_CALL* registerConsumer)(
            const ConsumerRegistration* registration,
            ConsumerHandle* outHandle);
        Status(PAPER_CALL* unregisterConsumer)(
            OwnerToken ownerToken);
        std::uint32_t(PAPER_CALL* getGrantedCapabilities)(
            OwnerToken ownerToken);
        Status(PAPER_CALL* setAnimationAuthority)(
            OwnerToken ownerToken,
            const AuthorityRequest* request);
        Status(PAPER_CALL* clearAnimationAuthority)(
            OwnerToken ownerToken);
        Status(PAPER_CALL* registerEventCallback)(
            OwnerToken ownerToken,
            EventCallback callback,
            void* userData,
            std::uint64_t* outCallbackToken);
        Status(PAPER_CALL* unregisterEventCallback)(
            OwnerToken ownerToken,
            std::uint64_t callbackToken);
        Status(PAPER_CALL* getReloadObservationLimits)(
            OwnerToken ownerToken,
            ReloadObservationLimits* outLimits);
        Status(PAPER_CALL* getReloadCatalogState)(
            OwnerToken ownerToken,
            ReloadCatalogState* outState);
        Status(PAPER_CALL* copyReloadCatalogNodes)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstNode,
            ReloadNodeCatalogEntry* outNodes,
            std::uint32_t maxNodes,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadEvidence)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstEvidence,
            ReloadEvidence* outEvidence,
            std::uint32_t maxEvidence,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadEvidencePoints)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t evidenceId,
            std::uint32_t firstPoint,
            Point3* outPoints,
            std::uint32_t maxPoints,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* getReloadFrameState)(
            OwnerToken ownerToken,
            ReloadFrameState* outState);
        Status(PAPER_CALL* copyReloadNodeObservations)(
            OwnerToken ownerToken,
            std::uint64_t snapshotSequence,
            std::uint32_t firstObservation,
            ReloadNodeObservation* outObservations,
            std::uint32_t maxObservations,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* getReloadAnimationLimits)(
            OwnerToken ownerToken,
            ReloadAnimationLimits* outLimits);
        Status(PAPER_CALL* getReloadAnimationCatalogState)(
            OwnerToken ownerToken,
            ReloadAnimationCatalogState* outState);
        Status(PAPER_CALL* getReloadAnimationLiveState)(
            OwnerToken ownerToken,
            ReloadAnimationLiveState* outState);
        Status(PAPER_CALL* copyReloadAnimationClips)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstClip,
            ReloadAnimationClip* outClips,
            std::uint32_t maxClips,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadAnimationTracks)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t firstTrack,
            ReloadAnimationTrack* outTracks,
            std::uint32_t maxTracks,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadAnimationSamples)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t trackId,
            std::uint32_t firstSample,
            ReloadAnimationSample* outSamples,
            std::uint32_t maxSamples,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadAnimationAnnotations)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t firstAnnotation,
            ReloadAnimationAnnotation* outAnnotations,
            std::uint32_t maxAnnotations,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadAnimationTriggers)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t firstTrigger,
            ReloadAnimationTrigger* outTriggers,
            std::uint32_t maxTriggers,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyReloadAnimationSkeleton)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstBone,
            ReloadAnimationSkeletonBone* outBones,
            std::uint32_t maxBones,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* getReloadStageState)(
            OwnerToken ownerToken,
            ReloadStageState* outState);
        Status(PAPER_CALL* copyReloadStageParts)(
            OwnerToken ownerToken,
            std::uint64_t snapshotSequence,
            std::uint32_t firstPart,
            ReloadStagePart* outParts,
            std::uint32_t maxParts,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* getNativePoseFrameState)(
            OwnerToken ownerToken,
            NativePoseFrameState* outState);
        Status(PAPER_CALL* getNativeHandSolution)(
            OwnerToken ownerToken,
            Hand hand,
            NativeHandSolution* outSolution);
        Status(PAPER_CALL* getWeaponMotionLimits)(
            OwnerToken ownerToken,
            WeaponMotionLimits* outLimits);
        Status(PAPER_CALL* getWeaponMotionCatalogState)(
            OwnerToken ownerToken,
            WeaponMotionCatalogState* outState);
        Status(PAPER_CALL* copyWeaponMotionParts)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstPart,
            WeaponMotionPart* outParts,
            std::uint32_t maxParts,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyWeaponMotionStages)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstStage,
            WeaponMotionStage* outStages,
            std::uint32_t maxStages,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyWeaponMotionStageKeys)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t stageId,
            std::uint32_t firstKey,
            ReloadQsTransform* outKeys,
            std::uint32_t maxKeys,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyWeaponMotionFollowers)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t stageId,
            std::uint32_t firstFollower,
            WeaponMotionFollower* outFollowers,
            std::uint32_t maxFollowers,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* copyWeaponMotionFollowerKeys)(
            OwnerToken ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t stageId,
            std::uint32_t followerIndex,
            std::uint32_t firstKey,
            ReloadQsTransform* outKeys,
            std::uint32_t maxKeys,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* getWeaponMotionLearningState)(
            OwnerToken ownerToken,
            WeaponMotionLearningState* outState);
        Status(PAPER_CALL* copyWeaponMotionRecorders)(
            OwnerToken ownerToken,
            std::uint64_t snapshotSequence,
            std::uint32_t firstRecorder,
            WeaponMotionRecorder* outRecorders,
            std::uint32_t maxRecorders,
            std::uint32_t* outCopied);
        Status(PAPER_CALL* getWeaponManipulationFrameState)(
            OwnerToken ownerToken,
            WeaponManipulationFrameState* outState);
        Status(PAPER_CALL* getWeaponManipulationHandState)(
            OwnerToken ownerToken,
            Hand hand,
            WeaponManipulationHandState* outState);
        Status(PAPER_CALL* getWeaponMotionStoreState)(
            OwnerToken ownerToken,
            WeaponMotionStoreState* outState);
        Status(PAPER_CALL* setDevelopmentCapture)(
            OwnerToken ownerToken,
            const DevelopmentCaptureRequest* request);
        Status(PAPER_CALL* clearDevelopmentCapture)(
            OwnerToken ownerToken);
        Status(PAPER_CALL* getDevelopmentCaptureState)(
            OwnerToken ownerToken,
            DevelopmentCaptureState* outState);
    };

    static_assert(sizeof(Api) == 392);
    static_assert(std::is_standard_layout_v<Api>);
    static_assert(std::is_trivially_copyable_v<Api>);
}
#pragma pack(pop)
