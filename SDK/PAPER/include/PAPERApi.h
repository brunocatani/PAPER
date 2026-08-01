#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_WIN32)
#    include <Windows.h>
#endif

#if defined(_WIN32)
#    define PAPER_CALL __cdecl
#    if defined(PAPER_API_EXPORTS)
#        define PAPER_API __declspec(dllexport)
#    else
#        define PAPER_API
#    endif
#else
#    define PAPER_CALL
#    define PAPER_API
#endif

namespace paper::api
{
    inline constexpr std::uint32_t PAPER_API_VERSION = 1;
    inline constexpr std::uint32_t PAPER_MOD_VERSION = 300;
    inline constexpr std::uint32_t PAPER_MAX_CONSUMERS_V1 = 16;
    inline constexpr std::uint32_t PAPER_MAX_CALLBACKS_V1 = 16;
    inline constexpr std::uint32_t PAPER_MAX_CAPTURED_TRANSFORMS_V1 = 192;
    inline constexpr std::uint32_t PAPER_FINGER_TRANSFORM_COUNT_V1 = 15;
    inline constexpr std::uint32_t PAPER_TRANSFORM_NAME_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t PAPER_MAX_AUTHORITY_LEASE_FRAMES_V1 = 1200;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_CATALOG_NODES_V1 = 2048;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_EVIDENCE_V1 = 8;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1 = 128;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_NODE_OBSERVATIONS_V1 =
        PAPER_MAX_RELOAD_OBSERVATION_TARGETS_V1 * 2;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_EVIDENCE_POINTS_PER_DETAIL_V1 = 252;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_EVIDENCE_POINTS_V1 =
        PAPER_MAX_RELOAD_EVIDENCE_V1 *
        PAPER_MAX_RELOAD_EVIDENCE_POINTS_PER_DETAIL_V1;
    inline constexpr std::uint32_t PAPER_RELOAD_NODE_NAME_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t PAPER_RELOAD_NODE_PATH_CAPACITY_V1 = 256;
    inline constexpr std::uint32_t PAPER_FORM_PLUGIN_NAME_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t PAPER_FORM_EDITOR_ID_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t PAPER_FORM_DISPLAY_NAME_CAPACITY_V1 = 96;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_ANIMATION_CLIPS_V1 = 640;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_LIVE_ANIMATION_CLIPS_V1 = 128;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_EXACT_ANIMATION_CLIPS_V1 = 512;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_LIVE_TRACKS_PER_CLIP_V1 = 64;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_EXACT_TRACKS_PER_CLIP_V1 = 128;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_LIVE_SAMPLES_PER_TRACK_V1 = 64;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_EXACT_SAMPLES_PER_TRACK_V1 = 720;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_SKELETON_BONES_V1 = 768;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_CLIP_ANNOTATIONS_V1 = 128;
    inline constexpr std::uint32_t PAPER_MAX_RELOAD_CLIP_TRIGGERS_V1 = 128;
    inline constexpr std::uint32_t PAPER_RELOAD_ANIMATION_NAME_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t PAPER_RELOAD_ANIMATION_PATH_CAPACITY_V1 = 260;
    inline constexpr std::uint32_t PAPER_RELOAD_MARKER_TEXT_CAPACITY_V1 = 96;
    inline constexpr std::uint32_t PAPER_RELOAD_RESOLVE_POINT_CAPACITY_V1 = 32;
    inline constexpr std::uint64_t PAPER_RELOAD_ANIMATION_SAMPLE_BUDGET_BYTES_V1 =
        128ull * 1024ull * 1024ull;

    enum class PaperResultV1 : std::uint32_t
    {
        Ok = 0,
        InvalidArgument = 1,
        UnsupportedVersion = 2,
        NotReady = 3,
        CapacityReached = 4,
        UnknownOwner = 5,
        PermissionDenied = 6,
        NotFound = 7,
        WrongThread = 8,
        OwnerConflict = 9,
        InvalidSize = 10,
        StaleSnapshot = 11,
        OutOfRange = 12,
    };

    enum class PaperConsumerCapabilityV1 : std::uint32_t
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
        // Negotiating this bit must never start exact clip preharvest.
        ReloadAnimationTelemetry = 1u << 8,
        All = (1u << 9) - 1u,
    };

    enum class PaperProviderFeatureBitV1 : std::uint32_t
    {
        None = 0,
        ReloadObservations = 1u << 0,
        ReloadEvidenceGeometry = 1u << 1,
        ReloadAnimationEvidence = 1u << 2,
        ReloadAnimationTelemetry = 1u << 3,
    };

    enum class PaperReloadAnimationAcquisitionV1 : std::uint32_t
    {
        LoadedGraphBinding = 1,
        LiveClipActivation = 2,
        ExactWeaponPreharvest = 3,
    };

    enum class PaperReloadAnimationTrackSpaceV1 : std::uint32_t
    {
        RigBoneLocal = 1,
        WeaponRootLocal = 2,
    };

    enum class PaperReloadAnimationPreharvestStateV1 : std::uint32_t
    {
        Idle = 0,
        LoadingBaseGraphs = 1,
        LoadingWeaponSubgraph = 2,
        LoadingClip = 3,
        SamplingClip = 4,
        Completed = 5,
        Failed = 6,
    };

    enum class PaperReloadAnimationCatalogFlagV1 : std::uint32_t
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

    enum class PaperReloadAnimationClipFlagV1 : std::uint32_t
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

    enum class PaperReloadAnimationTrackFlagV1 : std::uint32_t
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

    enum class PaperReloadAnimationLiveFlagV1 : std::uint32_t
    {
        None = 0,
        Active = 1u << 0,
        AnimationNameValid = 1u << 1,
    };

    enum class PaperFormIdentityFlagV1 : std::uint32_t
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

    enum class PaperWeaponClassificationFlagV1 : std::uint32_t
    {
        None = 0,
        Available = 1u << 0,
        Valid = 1u << 1,
    };

    enum class PaperReloadCatalogStatusFlagV1 : std::uint32_t
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
    };

    enum class PaperReloadNodeFlagV1 : std::uint32_t
    {
        None = 0,
        IsNode = 1u << 0,
        LocalTransformValid = 1u << 1,
        WeaponLocalTransformValid = 1u << 2,
        NameTruncated = 1u << 3,
        PathTruncated = 1u << 4,
    };

    enum class PaperReloadEvidenceFlagV1 : std::uint32_t
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

    enum class PaperReloadFrameStatusFlagV1 : std::uint32_t
    {
        None = 0,
        Valid = 1u << 0,
        CatalogValid = 1u << 1,
        NativeGraphOutputCaptured = 1u << 2,
        PostRockCaptured = 1u << 3,
        ObservationsTruncated = 1u << 4,
        TopologyMismatch = 1u << 5,
    };

    enum class PaperReloadNodeObservationFlagV1 : std::uint32_t
    {
        None = 0,
        LocalTransformValid = 1u << 0,
        WeaponLocalTransformValid = 1u << 1,
    };

    enum class PaperReloadObservationPhaseV1 : std::uint32_t
    {
        NativeGraphOutput = 1,
        PostRock = 2,
    };

    enum class PaperNativeAnimationAuthorityFlagV1 : std::uint32_t
    {
        None = 0,
        Arms = 1u << 0,
        Hands = 1u << 1,
        Weapon = 1u << 2,
        ReloadPose = (1u << 0) | (1u << 1) | (1u << 2),
    };

    enum class PaperRuntimeStatusFlagV1 : std::uint32_t
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

    enum class PaperHandV1 : std::uint32_t
    {
        Right = 0,
        Left = 1,
    };

    enum class PaperEventKindV1 : std::uint32_t
    {
        FrameComplete = 1,
        RuntimeReset = 2,
        ConfigReloaded = 3,
    };

    struct PaperTransformV1
    {
        float rotate[3][3]{};
        float translate[3]{};
        float scale{ 1.0f };
    };

    struct PaperConsumerRegistrationV1
    {
        std::uint32_t size{ sizeof(PaperConsumerRegistrationV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        char modName[64]{};
        std::uint32_t requestedCapabilities{ 0 };
        std::uint32_t reserved[7]{};
    };

    struct PaperConsumerHandleV1
    {
        std::uint32_t size{ sizeof(PaperConsumerHandleV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint64_t ownerToken{ 0 };
        std::uint32_t grantedCapabilities{ 0 };
        std::uint32_t providerGeneration{ 0 };
        std::uint32_t reserved[6]{};
    };

    struct PaperAuthorityRequestV1
    {
        std::uint32_t size{ sizeof(PaperAuthorityRequestV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t flags{ 0 };
        // Zero is persistent until clear/unregister. Non-zero requests expire
        // after this many Paper frame callbacks.
        std::uint32_t leaseFrames{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct PaperConfigStateV1
    {
        std::uint32_t size{ sizeof(PaperConfigStateV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t enabled{ 0 };
        std::uint32_t nativeReloadAuthorityEnabled{ 0 };
        std::uint32_t partialReloadAuthorityEnabled{ 0 };
        std::int32_t logLevel{ 0 };
        std::uint64_t revision{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct PaperRuntimeStateV1
    {
        std::uint32_t size{ sizeof(PaperRuntimeStateV1) };
        std::uint32_t version{ PAPER_API_VERSION };
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

    struct PaperCapturedTransformV1
    {
        std::uint32_t size{ sizeof(PaperCapturedTransformV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        char name[PAPER_TRANSFORM_NAME_CAPACITY_V1]{};
        PaperTransformV1 local{};
        std::uint32_t authorityFlags{ 0 };
        std::int32_t sourceIndex{ -1 };
        std::int32_t destinationIndex{ -1 };
        std::uint32_t reserved[4]{};
    };

    struct PaperNativeHandPoseV1
    {
        std::uint32_t size{ sizeof(PaperNativeHandPoseV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        PaperHandV1 hand{ PaperHandV1::Right };
        std::uint16_t fingerLocalTransformMask{ 0 };
        std::uint16_t reserved0{ 0 };
        PaperTransformV1 handInWeapon{};
        PaperTransformV1 fingerLocalTransforms[
            PAPER_FINGER_TRANSFORM_COUNT_V1]{};
        std::uint64_t captureSequence{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct PaperEventV1
    {
        std::uint32_t size{ sizeof(PaperEventV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        PaperEventKindV1 kind{ PaperEventKindV1::FrameComplete };
        std::uint32_t reserved0{ 0 };
        PaperRuntimeStateV1 runtime{};
        std::uint32_t reserved[8]{};
    };

    /*
     * Reload observations are descriptive source data. PAPER does not infer
     * part motion, attachment, convergence, or reload stages from these
     * records. Scene transforms use Fallout game units. Weapon-local values
     * are lossless coordinate conversions relative to the observed weapon
     * root. Evidence bounds and points preserve ROCK's generated local
     * geometry coordinates unchanged.
     */
    struct PaperPoint3V1
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct PaperBounds3V1
    {
        PaperPoint3V1 min{};
        PaperPoint3V1 max{};
        std::uint32_t valid{ 0 };
        std::uint32_t reserved{ 0 };
    };

    struct PaperFormIdentityV1
    {
        std::uint32_t size{ sizeof(PaperFormIdentityV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t flags{ 0 };
        std::uint32_t runtimeFormId{ 0 };
        std::uint32_t localFormId{ 0 };
        std::uint32_t formType{ 0 };
        char pluginName[PAPER_FORM_PLUGIN_NAME_CAPACITY_V1]{};
        char editorId[PAPER_FORM_EDITOR_ID_CAPACITY_V1]{};
        char displayName[PAPER_FORM_DISPLAY_NAME_CAPACITY_V1]{};
        std::uint32_t reserved[4]{};
    };

    struct PaperWeaponClassificationV1
    {
        std::uint32_t size{ sizeof(PaperWeaponClassificationV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t flags{ 0 };
        std::uint32_t formId{ 0 };
        std::uint64_t keywordFlags{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        std::uint32_t sizeClass{ 0 };
        std::uint32_t source{ 0 };
        float confidence{ 0.0f };
        std::uint32_t provenanceFlags{ 0 };
        std::uint32_t reserved[4]{};
    };

    struct PaperReloadObservationLimitsV1
    {
        std::uint32_t size{ sizeof(PaperReloadObservationLimitsV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t featureBits{ 0 };
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

    struct PaperReloadCatalogStateV1
    {
        std::uint32_t size{ sizeof(PaperReloadCatalogStateV1) };
        std::uint32_t version{ PAPER_API_VERSION };
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
        PaperWeaponClassificationV1 classification{};
        PaperFormIdentityV1 weapon{};
        PaperTransformV1 weaponRootWorld{};
        std::uint32_t reserved[5]{};
    };

    struct PaperReloadNodeCatalogEntryV1
    {
        std::uint32_t size{ sizeof(PaperReloadNodeCatalogEntryV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t nodeId{ 0 };
        std::int32_t parentNodeId{ -1 };
        std::uint32_t childIndex{ 0 };
        std::uint32_t sameNameSiblingOrdinal{ 0 };
        std::uint32_t flags{ 0 };
        std::uint32_t childCount{ 0 };
        char name[PAPER_RELOAD_NODE_NAME_CAPACITY_V1]{};
        char rootRelativePath[PAPER_RELOAD_NODE_PATH_CAPACITY_V1]{};
        PaperTransformV1 baselineLocal{};
        PaperTransformV1 baselineWeaponLocal{};
        std::uint32_t reserved[4]{};
    };

    struct PaperReloadEvidenceV1
    {
        std::uint32_t size{ sizeof(PaperReloadEvidenceV1) };
        std::uint32_t version{ PAPER_API_VERSION };
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
        char sourceName[PAPER_TRANSFORM_NAME_CAPACITY_V1]{};
        PaperBounds3V1 localBoundsGame{};
        std::uint32_t providerPointCount{ 0 };
        std::uint32_t copiedPointCount{ 0 };
        PaperFormIdentityV1 omod{};
        PaperFormIdentityV1 attachPoint{};
        std::uint32_t reserved[4]{};
    };

    struct PaperReloadFrameStateV1
    {
        std::uint32_t size{ sizeof(PaperReloadFrameStateV1) };
        std::uint32_t version{ PAPER_API_VERSION };
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
        PaperTransformV1 weaponRootWorld{};
        std::uint32_t reserved[5]{};
    };

    struct PaperReloadNodeObservationV1
    {
        std::uint32_t size{ sizeof(PaperReloadNodeObservationV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t nodeId{ 0 };
        PaperReloadObservationPhaseV1 phase{
            PaperReloadObservationPhaseV1::NativeGraphOutput
        };
        std::uint32_t flags{ 0 };
        std::uint32_t reserved0{ 0 };
        std::uint64_t frameIndex{ 0 };
        PaperTransformV1 local{};
        PaperTransformV1 weaponLocal{};
        std::uint32_t reserved[4]{};
    };

    /*
     * Reload animation evidence is raw authored/runtime data. PAPER does not
     * classify clips, infer moving parts, choose reload stages, or correlate
     * marker text with scene-node motion. Consumers receive provenance,
     * hierarchy, timing, and transforms exactly as captured.
     *
     * PaperReloadQsTransformV1 stores translation xyz, quaternion xyzw, and
     * independent xyz scale. Exact samples are weapon-root-local; passive
     * live-binding samples are rig-bone-local.
     */
    struct PaperReloadQsTransformV1
    {
        float translate[3]{};
        float rotate[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
        float scale[3]{ 1.0f, 1.0f, 1.0f };
    };

    struct PaperReloadAnimationLimitsV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationLimitsV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t featureBits{ 0 };
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
        std::uint64_t sampleStorageBudgetBytes{ 0 };
        std::uint32_t reserved[6]{};
    };

    struct PaperReloadAnimationCatalogStateV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationCatalogStateV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t statusFlags{ 0 };
        PaperReloadAnimationPreharvestStateV1 exactPreharvestState{
            PaperReloadAnimationPreharvestStateV1::Idle
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
            PAPER_RELOAD_RESOLVE_POINT_CAPACITY_V1]{};
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

    struct PaperReloadAnimationLiveStateV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationLiveStateV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t flags{ 0 };
        std::uint32_t concurrentActivityCount{ 0 };
        std::uint64_t catalogSequence{ 0 };
        std::uint64_t activityId{ 0 };
        std::uint64_t frameIndex{ 0 };
        std::uint32_t weaponFormId{ 0 };
        std::uint32_t reserved0{ 0 };
        std::uint64_t weaponGenerationKey{ 0 };
        char animationName[PAPER_RELOAD_ANIMATION_NAME_CAPACITY_V1]{};
        float durationSeconds{ 0.0f };
        float cropStartSeconds{ 0.0f };
        float croppedDurationSeconds{ 0.0f };
        float localTimeSeconds{ 0.0f };
        float fraction{ 0.0f };
        std::uint32_t reserved[6]{};
    };

    struct PaperReloadAnimationClipV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationClipV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t clipId{ 0 };
        PaperReloadAnimationAcquisitionV1 acquisition{
            PaperReloadAnimationAcquisitionV1::LoadedGraphBinding
        };
        PaperReloadAnimationTrackSpaceV1 trackSpace{
            PaperReloadAnimationTrackSpaceV1::RigBoneLocal
        };
        std::uint32_t flags{ 0 };
        std::uint64_t activityId{ 0 };
        char animationName[PAPER_RELOAD_ANIMATION_NAME_CAPACITY_V1]{};
        char animationPath[PAPER_RELOAD_ANIMATION_PATH_CAPACITY_V1]{};
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

    struct PaperReloadAnimationTrackV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationTrackV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t clipId{ 0 };
        std::uint32_t trackId{ 0 };
        std::uint32_t flags{ 0 };
        std::int32_t boneIndex{ -1 };
        std::int32_t parentBoneIndex{ -1 };
        std::int32_t transformTrackIndex{ -1 };
        std::uint32_t chainDepth{ 0 };
        std::uint32_t sampleCount{ 0 };
        char boneName[PAPER_RELOAD_ANIMATION_NAME_CAPACITY_V1]{};
        PaperReloadQsTransformV1 referenceLocal{};
        std::uint32_t reserved[5]{};
    };

    struct PaperReloadAnimationSampleV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationSampleV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t clipId{ 0 };
        std::uint32_t trackId{ 0 };
        std::uint32_t sampleIndex{ 0 };
        float timeSeconds{ 0.0f };
        PaperReloadQsTransformV1 transform{};
        std::uint32_t reserved[4]{};
    };

    struct PaperReloadAnimationAnnotationV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationAnnotationV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t clipId{ 0 };
        std::uint32_t annotationId{ 0 };
        float timeSeconds{ 0.0f };
        char trackName[PAPER_RELOAD_ANIMATION_NAME_CAPACITY_V1]{};
        char text[PAPER_RELOAD_MARKER_TEXT_CAPACITY_V1]{};
        std::uint32_t reserved[4]{};
    };

    struct PaperReloadAnimationTriggerV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationTriggerV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t clipId{ 0 };
        std::uint32_t triggerId{ 0 };
        float localTimeSeconds{ 0.0f };
        std::int32_t eventId{ -1 };
        char eventName[PAPER_RELOAD_MARKER_TEXT_CAPACITY_V1]{};
        std::uint32_t reserved[4]{};
    };

    struct PaperReloadAnimationSkeletonBoneV1
    {
        std::uint32_t size{ sizeof(PaperReloadAnimationSkeletonBoneV1) };
        std::uint32_t version{ PAPER_API_VERSION };
        std::uint32_t flags{ 0 };
        std::int32_t boneIndex{ -1 };
        std::int32_t parentBoneIndex{ -1 };
        std::int32_t transformTrackIndex{ -1 };
        char boneName[PAPER_RELOAD_ANIMATION_NAME_CAPACITY_V1]{};
        PaperReloadQsTransformV1 referenceLocal{};
        std::uint32_t reserved[5]{};
    };


    using PaperEventCallbackV1 =
        void(PAPER_CALL*)(const PaperEventV1* eventData, void* userData);

    /*
     * All functions below except getVersion, getModVersion, and isReady are
     * game-thread only. Query functions fail closed on a wrong thread or when
     * ownerToken lacks the corresponding negotiated capability. Returned
     * snapshots are value-only; callback/event pointers are valid only for the
     * duration of the call.
     */
    struct PaperProviderApiV1
    {
        std::uint32_t(PAPER_CALL* getVersion)();
        std::uint32_t(PAPER_CALL* getModVersion)();
        bool(PAPER_CALL* isReady)();
        bool(PAPER_CALL* getRuntimeStateV1)(
            std::uint64_t ownerToken,
            PaperRuntimeStateV1* outState);
        bool(PAPER_CALL* getConfigStateV1)(
            std::uint64_t ownerToken,
            PaperConfigStateV1* outState);
        std::uint32_t(PAPER_CALL* getCapturedTransformCountV1)(
            std::uint64_t ownerToken);
        std::uint32_t(PAPER_CALL* copyCapturedTransformsV1)(
            std::uint64_t ownerToken,
            PaperCapturedTransformV1* outTransforms,
            std::uint32_t maxTransforms);
        bool(PAPER_CALL* getNativeHandPoseV1)(
            std::uint64_t ownerToken,
            PaperHandV1 hand,
            PaperNativeHandPoseV1* outPose);
        PaperResultV1(PAPER_CALL* registerConsumerV1)(
            const PaperConsumerRegistrationV1* registration,
            PaperConsumerHandleV1* outHandle);
        PaperResultV1(PAPER_CALL* unregisterConsumerV1)(
            std::uint64_t ownerToken);
        std::uint32_t(PAPER_CALL* getGrantedCapabilitiesV1)(
            std::uint64_t ownerToken);
        PaperResultV1(PAPER_CALL* setAnimationAuthorityV1)(
            std::uint64_t ownerToken,
            const PaperAuthorityRequestV1* request);
        PaperResultV1(PAPER_CALL* clearAnimationAuthorityV1)(
            std::uint64_t ownerToken);
        PaperResultV1(PAPER_CALL* registerEventCallbackV1)(
            std::uint64_t ownerToken,
            PaperEventCallbackV1 callback,
            void* userData,
            std::uint64_t* outCallbackToken);
        PaperResultV1(PAPER_CALL* unregisterEventCallbackV1)(
            std::uint64_t ownerToken,
            std::uint64_t callbackToken);
        PaperResultV1(PAPER_CALL* getReloadObservationLimitsV1)(
            std::uint64_t ownerToken,
            PaperReloadObservationLimitsV1* outLimits);
        PaperResultV1(PAPER_CALL* getReloadCatalogStateV1)(
            std::uint64_t ownerToken,
            PaperReloadCatalogStateV1* outState);
        PaperResultV1(PAPER_CALL* copyReloadCatalogNodesV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstNode,
            PaperReloadNodeCatalogEntryV1* outNodes,
            std::uint32_t maxNodes,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadEvidenceV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstEvidence,
            PaperReloadEvidenceV1* outEvidence,
            std::uint32_t maxEvidence,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadEvidencePointsV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t evidenceId,
            std::uint32_t firstPoint,
            PaperPoint3V1* outPoints,
            std::uint32_t maxPoints,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* getReloadFrameStateV1)(
            std::uint64_t ownerToken,
            PaperReloadFrameStateV1* outState);
        PaperResultV1(PAPER_CALL* copyReloadNodeObservationsV1)(
            std::uint64_t ownerToken,
            std::uint64_t snapshotSequence,
            std::uint32_t firstObservation,
            PaperReloadNodeObservationV1* outObservations,
            std::uint32_t maxObservations,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* getReloadAnimationLimitsV1)(
            std::uint64_t ownerToken,
            PaperReloadAnimationLimitsV1* outLimits);
        PaperResultV1(PAPER_CALL* getReloadAnimationCatalogStateV1)(
            std::uint64_t ownerToken,
            PaperReloadAnimationCatalogStateV1* outState);
        PaperResultV1(PAPER_CALL* getReloadAnimationLiveStateV1)(
            std::uint64_t ownerToken,
            PaperReloadAnimationLiveStateV1* outState);
        PaperResultV1(PAPER_CALL* copyReloadAnimationClipsV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstClip,
            PaperReloadAnimationClipV1* outClips,
            std::uint32_t maxClips,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadAnimationTracksV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t firstTrack,
            PaperReloadAnimationTrackV1* outTracks,
            std::uint32_t maxTracks,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadAnimationSamplesV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t trackId,
            std::uint32_t firstSample,
            PaperReloadAnimationSampleV1* outSamples,
            std::uint32_t maxSamples,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadAnimationAnnotationsV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t firstAnnotation,
            PaperReloadAnimationAnnotationV1* outAnnotations,
            std::uint32_t maxAnnotations,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadAnimationTriggersV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t clipId,
            std::uint32_t firstTrigger,
            PaperReloadAnimationTriggerV1* outTriggers,
            std::uint32_t maxTriggers,
            std::uint32_t* outCopied);
        PaperResultV1(PAPER_CALL* copyReloadAnimationSkeletonV1)(
            std::uint64_t ownerToken,
            std::uint64_t catalogSequence,
            std::uint32_t firstBone,
            PaperReloadAnimationSkeletonBoneV1* outBones,
            std::uint32_t maxBones,
            std::uint32_t* outCopied);
    };

    inline constexpr std::uint32_t PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES =
        static_cast<std::uint32_t>(
            offsetof(PaperProviderApiV1, getReloadObservationLimitsV1));
    inline constexpr std::uint32_t PAPER_PROVIDER_API_V1_TABLE_BYTES =
        static_cast<std::uint32_t>(sizeof(PaperProviderApiV1));
    inline constexpr std::uint32_t
        PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES =
            static_cast<std::uint32_t>(
                offsetof(
                    PaperProviderApiV1,
                    copyReloadNodeObservationsV1) +
                sizeof(decltype(
                    PaperProviderApiV1::copyReloadNodeObservationsV1)));
    inline constexpr std::uint32_t
        PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES =
            static_cast<std::uint32_t>(
                offsetof(
                    PaperProviderApiV1,
                    copyReloadAnimationSkeletonV1) +
                sizeof(decltype(
                    PaperProviderApiV1::copyReloadAnimationSkeletonV1)));
    inline constexpr std::uint32_t PAPER_PROVIDER_FEATURE_BITS_V1 =
        static_cast<std::uint32_t>(
            PaperProviderFeatureBitV1::ReloadObservations) |
        static_cast<std::uint32_t>(
            PaperProviderFeatureBitV1::ReloadEvidenceGeometry) |
        static_cast<std::uint32_t>(
            PaperProviderFeatureBitV1::ReloadAnimationEvidence) |
        static_cast<std::uint32_t>(
            PaperProviderFeatureBitV1::ReloadAnimationTelemetry);

    struct PaperProviderDescriptorV1
    {
        std::uint32_t size{ sizeof(PaperProviderDescriptorV1) };
        std::uint32_t apiVersion{ PAPER_API_VERSION };
        std::uint32_t tableBytes{ PAPER_PROVIDER_API_V1_TABLE_BYTES };
        std::uint32_t featureBits{ PAPER_PROVIDER_FEATURE_BITS_V1 };
        const PaperProviderApiV1* api{ nullptr };
        std::uint32_t reserved[6]{};
    };

    static_assert(sizeof(PaperTransformV1) == 52);
    static_assert(alignof(PaperTransformV1) == 4);
    static_assert(std::is_standard_layout_v<PaperTransformV1>);
    static_assert(std::is_trivially_copyable_v<PaperTransformV1>);
    static_assert(sizeof(PaperConsumerRegistrationV1) == 104);
    static_assert(alignof(PaperConsumerRegistrationV1) == 4);
    static_assert(std::is_standard_layout_v<PaperConsumerRegistrationV1>);
    static_assert(std::is_trivially_copyable_v<PaperConsumerRegistrationV1>);
    static_assert(sizeof(PaperConsumerHandleV1) == 48);
    static_assert(alignof(PaperConsumerHandleV1) == 8);
    static_assert(std::is_standard_layout_v<PaperConsumerHandleV1>);
    static_assert(std::is_trivially_copyable_v<PaperConsumerHandleV1>);
    static_assert(sizeof(PaperAuthorityRequestV1) == 48);
    static_assert(alignof(PaperAuthorityRequestV1) == 4);
    static_assert(std::is_standard_layout_v<PaperAuthorityRequestV1>);
    static_assert(std::is_trivially_copyable_v<PaperAuthorityRequestV1>);
    static_assert(sizeof(PaperConfigStateV1) == 64);
    static_assert(alignof(PaperConfigStateV1) == 8);
    static_assert(std::is_standard_layout_v<PaperConfigStateV1>);
    static_assert(std::is_trivially_copyable_v<PaperConfigStateV1>);
    static_assert(sizeof(PaperRuntimeStateV1) == 120);
    static_assert(alignof(PaperRuntimeStateV1) == 8);
    static_assert(std::is_standard_layout_v<PaperRuntimeStateV1>);
    static_assert(std::is_trivially_copyable_v<PaperRuntimeStateV1>);
    static_assert(sizeof(PaperCapturedTransformV1) == 152);
    static_assert(alignof(PaperCapturedTransformV1) == 4);
    static_assert(std::is_standard_layout_v<PaperCapturedTransformV1>);
    static_assert(std::is_trivially_copyable_v<PaperCapturedTransformV1>);
    static_assert(sizeof(PaperNativeHandPoseV1) == 888);
    static_assert(alignof(PaperNativeHandPoseV1) == 8);
    static_assert(std::is_standard_layout_v<PaperNativeHandPoseV1>);
    static_assert(std::is_trivially_copyable_v<PaperNativeHandPoseV1>);
    static_assert(sizeof(PaperEventV1) == 168);
    static_assert(alignof(PaperEventV1) == 8);
    static_assert(std::is_standard_layout_v<PaperEventV1>);
    static_assert(std::is_trivially_copyable_v<PaperEventV1>);
    static_assert(sizeof(PaperPoint3V1) == 12);
    static_assert(sizeof(PaperBounds3V1) == 32);
    static_assert(sizeof(PaperFormIdentityV1) == 264);
    static_assert(sizeof(PaperWeaponClassificationV1) == 64);
    static_assert(sizeof(PaperReloadObservationLimitsV1) == 88);
    static_assert(sizeof(PaperReloadCatalogStateV1) == 480);
    static_assert(sizeof(PaperReloadNodeCatalogEntryV1) == 472);
    static_assert(sizeof(PaperReloadEvidenceV1) == 704);
    static_assert(sizeof(PaperReloadFrameStateV1) == 160);
    static_assert(sizeof(PaperReloadNodeObservationV1) == 152);
    static_assert(sizeof(PaperReloadQsTransformV1) == 40);
    static_assert(std::is_standard_layout_v<PaperReloadAnimationCatalogStateV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadAnimationCatalogStateV1>);
    static_assert(std::is_standard_layout_v<PaperReloadAnimationClipV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadAnimationClipV1>);
    static_assert(std::is_standard_layout_v<PaperReloadAnimationTrackV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadAnimationTrackV1>);
    static_assert(std::is_standard_layout_v<PaperReloadAnimationSampleV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadAnimationSampleV1>);
    static_assert(std::is_standard_layout_v<PaperReloadCatalogStateV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadCatalogStateV1>);
    static_assert(std::is_standard_layout_v<PaperReloadNodeCatalogEntryV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadNodeCatalogEntryV1>);
    static_assert(std::is_standard_layout_v<PaperReloadEvidenceV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadEvidenceV1>);
    static_assert(std::is_standard_layout_v<PaperReloadFrameStateV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadFrameStateV1>);
    static_assert(std::is_standard_layout_v<PaperReloadNodeObservationV1>);
    static_assert(std::is_trivially_copyable_v<PaperReloadNodeObservationV1>);
    static_assert(sizeof(PaperProviderApiV1) == 248);
    static_assert(alignof(PaperProviderApiV1) == 8);
    static_assert(std::is_standard_layout_v<PaperProviderApiV1>);
    static_assert(std::is_trivially_copyable_v<PaperProviderApiV1>);
    static_assert(PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES == 120);
    static_assert(PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES == 176);
    static_assert(PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES == 248);
    static_assert(sizeof(PaperProviderDescriptorV1) == 48);
    static_assert(alignof(PaperProviderDescriptorV1) == 8);
    static_assert(std::is_standard_layout_v<PaperProviderDescriptorV1>);
    static_assert(std::is_trivially_copyable_v<PaperProviderDescriptorV1>);

    class PaperApi
    {
    public:
        inline static const PaperProviderApiV1* inst = nullptr;
        inline static std::uint32_t negotiatedTableBytes = 0;
        inline static std::uint32_t negotiatedFeatureBits = 0;

        [[nodiscard]] static int initialize(
            const std::uint32_t minVersion = PAPER_API_VERSION,
            const std::uint32_t minTableBytes =
                PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES)
        {
#if defined(_WIN32)
            inst = nullptr;
            negotiatedTableBytes = 0;
            negotiatedFeatureBits = 0;
            const auto module = GetModuleHandleW(L"PAPER.dll");
            if (!module) {
                return 1;
            }
            using GetProviderDescriptorFn =
                const PaperProviderDescriptorV1*(PAPER_CALL*)();
            const auto getDescriptor =
                reinterpret_cast<GetProviderDescriptorFn>(
                    GetProcAddress(
                        module,
                        "PAPERAPI_GetProviderDescriptorV1"));
            if (getDescriptor) {
                const auto* descriptor = getDescriptor();
                if (!descriptor ||
                    descriptor->size < sizeof(PaperProviderDescriptorV1) ||
                    descriptor->apiVersion < minVersion ||
                    descriptor->tableBytes < minTableBytes ||
                    !descriptor->api) {
                    return 5;
                }
                inst = descriptor->api;
                if (!inst->getVersion || inst->getVersion() < minVersion) {
                    inst = nullptr;
                    return 4;
                }
                negotiatedTableBytes = descriptor->tableBytes;
                negotiatedFeatureBits = descriptor->featureBits;
                return 0;
            }
            if (minTableBytes > PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES) {
                return 5;
            }
            using GetProviderApiFn =
                const PaperProviderApiV1*(PAPER_CALL*)(std::uint32_t);
            const auto getApi = reinterpret_cast<GetProviderApiFn>(
                GetProcAddress(module, "PAPERAPI_GetProviderApi"));
            if (!getApi) {
                return 2;
            }
            inst = getApi(minVersion);
            if (!inst) {
                return 3;
            }
            if (!inst->getVersion || inst->getVersion() < minVersion) {
                inst = nullptr;
                return 4;
            }
            negotiatedTableBytes = PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES;
            return 0;
#else
            (void)minVersion;
            (void)minTableBytes;
            return 1;
#endif
        }
    };

    [[nodiscard]] inline bool supportsReloadAnimationEvidenceV1()
    {
        return PaperApi::inst &&
               PaperApi::negotiatedTableBytes >=
                   PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES &&
               (PaperApi::negotiatedFeatureBits &
                   static_cast<std::uint32_t>(
                       PaperProviderFeatureBitV1::
                           ReloadAnimationEvidence)) != 0 &&
               PaperApi::inst->getReloadAnimationLimitsV1 &&
               PaperApi::inst->getReloadAnimationCatalogStateV1 &&
               PaperApi::inst->getReloadAnimationLiveStateV1 &&
               PaperApi::inst->copyReloadAnimationClipsV1 &&
               PaperApi::inst->copyReloadAnimationTracksV1 &&
               PaperApi::inst->copyReloadAnimationSamplesV1 &&
               PaperApi::inst->copyReloadAnimationAnnotationsV1 &&
               PaperApi::inst->copyReloadAnimationTriggersV1 &&
               PaperApi::inst->copyReloadAnimationSkeletonV1;
    }

    [[nodiscard]] inline bool supportsReloadAnimationTelemetryV1()
    {
        return PaperApi::inst &&
               PaperApi::negotiatedTableBytes >=
                   PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES &&
               (PaperApi::negotiatedFeatureBits &
                   static_cast<std::uint32_t>(
                       PaperProviderFeatureBitV1::
                           ReloadAnimationTelemetry)) != 0 &&
               PaperApi::inst->getReloadAnimationLimitsV1 &&
               PaperApi::inst->getReloadAnimationCatalogStateV1 &&
               PaperApi::inst->getReloadAnimationLiveStateV1 &&
               PaperApi::inst->copyReloadAnimationClipsV1 &&
               PaperApi::inst->copyReloadAnimationTracksV1 &&
               PaperApi::inst->copyReloadAnimationSamplesV1 &&
               PaperApi::inst->copyReloadAnimationAnnotationsV1 &&
               PaperApi::inst->copyReloadAnimationTriggersV1 &&
               PaperApi::inst->copyReloadAnimationSkeletonV1;
    }
}

extern "C" PAPER_API
    const paper::api::PaperProviderApiV1* PAPER_CALL
    PAPERAPI_GetProviderApi(std::uint32_t requestedVersion);

extern "C" PAPER_API
    const paper::api::PaperProviderDescriptorV1* PAPER_CALL
    PAPERAPI_GetProviderDescriptorV1();
