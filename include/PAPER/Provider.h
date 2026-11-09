#pragma once
#include "Abi.h"

// PAPER's main interface: consumer registration, runtime and configuration
// state, captured native transforms and hand poses, native animation
// authority, frame events and the native pose pipeline.
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

    // Requested at registration; granted capabilities gate every owner-scoped
    // call. Bits are local to this interface major. Bits 5 to 9 and 11 to 14
    // are retired (reload observation, animation evidence, reload stages,
    // weapon motion and development capture) and are never reused.
    enum class ConsumerCapability : std::uint32_t
    {
        None = 0,
        RuntimeState = 1u << 0,
        CapturedTransforms = 1u << 1,
        NativeHandPose = 1u << 2,
        AnimationAuthority = 1u << 3,
        FrameCallbacks = 1u << 4,
        NativePosePipeline = 1u << 10,
        All = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) |
            (1u << 10),
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

    // Values 4 to 10 are retired (weapon motion and manipulation events) and
    // are never reused.
    enum class EventKind : std::uint32_t
    {
        FrameComplete = 1,
        RuntimeReset = 2,
        ConfigReloaded = 3,
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
    static_assert(sizeof(ConfigState) == 48);
    static_assert(alignof(ConfigState) == 8);
    static_assert(std::is_standard_layout_v<ConfigState>);
    static_assert(std::is_trivially_copyable_v<ConfigState>);
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
    static_assert(sizeof(Event) == 136);
    static_assert(alignof(Event) == 8);
    static_assert(std::is_standard_layout_v<Event>);
    static_assert(std::is_trivially_copyable_v<Event>);
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
        Status(PAPER_CALL* getNativePoseFrameState)(
            OwnerToken ownerToken,
            NativePoseFrameState* outState);
        Status(PAPER_CALL* getNativeHandSolution)(
            OwnerToken ownerToken,
            Hand hand,
            NativeHandSolution* outSolution);
    };

    static_assert(sizeof(Api) == 128);
    static_assert(std::is_standard_layout_v<Api>);
    static_assert(std::is_trivially_copyable_v<Api>);
}
#pragma pack(pop)
