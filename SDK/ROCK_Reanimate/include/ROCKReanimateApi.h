#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_WIN32)
#    include <Windows.h>
#endif

#if defined(_WIN32)
#    define ROCK_REANIMATE_CALL __cdecl
#    if defined(ROCK_REANIMATE_API_EXPORTS)
#        define ROCK_REANIMATE_API __declspec(dllexport)
#    else
#        define ROCK_REANIMATE_API
#    endif
#else
#    define ROCK_REANIMATE_CALL
#    define ROCK_REANIMATE_API
#endif

namespace rock_reanimate::api
{
    inline constexpr std::uint32_t ROCK_REANIMATE_API_VERSION = 1;
    inline constexpr std::uint32_t ROCK_REANIMATE_MOD_VERSION = 100;
    inline constexpr std::uint32_t ROCK_REANIMATE_MAX_CONSUMERS_V1 = 16;
    inline constexpr std::uint32_t ROCK_REANIMATE_MAX_CALLBACKS_V1 = 16;
    inline constexpr std::uint32_t ROCK_REANIMATE_MAX_CAPTURED_TRANSFORMS_V1 = 192;
    inline constexpr std::uint32_t ROCK_REANIMATE_FINGER_TRANSFORM_COUNT_V1 = 15;
    inline constexpr std::uint32_t ROCK_REANIMATE_TRANSFORM_NAME_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t ROCK_REANIMATE_MAX_AUTHORITY_LEASE_FRAMES_V1 = 1200;

    enum class ReanimateResultV1 : std::uint32_t
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
    };

    enum class ReanimateConsumerCapabilityV1 : std::uint32_t
    {
        None = 0,
        RuntimeState = 1u << 0,
        CapturedTransforms = 1u << 1,
        NativeHandPose = 1u << 2,
        AnimationAuthority = 1u << 3,
        FrameCallbacks = 1u << 4,
        All = (1u << 5) - 1u,
    };

    enum class ReanimateNativeAnimationAuthorityFlagV1 : std::uint32_t
    {
        None = 0,
        Arms = 1u << 0,
        Hands = 1u << 1,
        Weapon = 1u << 2,
        ReloadPose = (1u << 0) | (1u << 1) | (1u << 2),
    };

    enum class ReanimateRuntimeStatusFlagV1 : std::uint32_t
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

    enum class ReanimateHandV1 : std::uint32_t
    {
        Right = 0,
        Left = 1,
    };

    enum class ReanimateEventKindV1 : std::uint32_t
    {
        FrameComplete = 1,
        RuntimeReset = 2,
        ConfigReloaded = 3,
    };

    struct ReanimateTransformV1
    {
        float rotate[3][3]{};
        float translate[3]{};
        float scale{ 1.0f };
    };

    struct ReanimateConsumerRegistrationV1
    {
        std::uint32_t size{ sizeof(ReanimateConsumerRegistrationV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        char modName[64]{};
        std::uint32_t requestedCapabilities{ 0 };
        std::uint32_t reserved[7]{};
    };

    struct ReanimateConsumerHandleV1
    {
        std::uint32_t size{ sizeof(ReanimateConsumerHandleV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        std::uint64_t ownerToken{ 0 };
        std::uint32_t grantedCapabilities{ 0 };
        std::uint32_t providerGeneration{ 0 };
        std::uint32_t reserved[6]{};
    };

    struct ReanimateAuthorityRequestV1
    {
        std::uint32_t size{ sizeof(ReanimateAuthorityRequestV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        std::uint32_t flags{ 0 };
        // Zero is persistent until clear/unregister. Non-zero requests expire
        // after this many Reanimate frame callbacks.
        std::uint32_t leaseFrames{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ReanimateConfigStateV1
    {
        std::uint32_t size{ sizeof(ReanimateConfigStateV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        std::uint32_t enabled{ 0 };
        std::uint32_t nativeReloadAuthorityEnabled{ 0 };
        std::uint32_t partialReloadAuthorityEnabled{ 0 };
        std::int32_t logLevel{ 0 };
        std::uint64_t revision{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ReanimateRuntimeStateV1
    {
        std::uint32_t size{ sizeof(ReanimateRuntimeStateV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
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
        std::uint32_t reanimateProviderGeneration{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ReanimateCapturedTransformV1
    {
        std::uint32_t size{ sizeof(ReanimateCapturedTransformV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        char name[ROCK_REANIMATE_TRANSFORM_NAME_CAPACITY_V1]{};
        ReanimateTransformV1 local{};
        std::uint32_t authorityFlags{ 0 };
        std::int32_t sourceIndex{ -1 };
        std::int32_t destinationIndex{ -1 };
        std::uint32_t reserved[4]{};
    };

    struct ReanimateNativeHandPoseV1
    {
        std::uint32_t size{ sizeof(ReanimateNativeHandPoseV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        ReanimateHandV1 hand{ ReanimateHandV1::Right };
        std::uint16_t fingerLocalTransformMask{ 0 };
        std::uint16_t reserved0{ 0 };
        ReanimateTransformV1 handInWeapon{};
        ReanimateTransformV1 fingerLocalTransforms[
            ROCK_REANIMATE_FINGER_TRANSFORM_COUNT_V1]{};
        std::uint64_t captureSequence{ 0 };
        std::uint32_t reserved[8]{};
    };

    struct ReanimateEventV1
    {
        std::uint32_t size{ sizeof(ReanimateEventV1) };
        std::uint32_t version{ ROCK_REANIMATE_API_VERSION };
        ReanimateEventKindV1 kind{ ReanimateEventKindV1::FrameComplete };
        std::uint32_t reserved0{ 0 };
        ReanimateRuntimeStateV1 runtime{};
        std::uint32_t reserved[8]{};
    };

    using ReanimateEventCallbackV1 =
        void(ROCK_REANIMATE_CALL*)(const ReanimateEventV1* eventData, void* userData);

    /*
     * All functions below except getVersion, getModVersion, and isReady are
     * game-thread only. Query functions fail closed on a wrong thread or when
     * ownerToken lacks the corresponding negotiated capability. Returned
     * snapshots are value-only; callback/event pointers are valid only for the
     * duration of the call.
     */
    struct ReanimateProviderApiV1
    {
        std::uint32_t(ROCK_REANIMATE_CALL* getVersion)();
        std::uint32_t(ROCK_REANIMATE_CALL* getModVersion)();
        bool(ROCK_REANIMATE_CALL* isReady)();
        bool(ROCK_REANIMATE_CALL* getRuntimeStateV1)(
            std::uint64_t ownerToken,
            ReanimateRuntimeStateV1* outState);
        bool(ROCK_REANIMATE_CALL* getConfigStateV1)(
            std::uint64_t ownerToken,
            ReanimateConfigStateV1* outState);
        std::uint32_t(ROCK_REANIMATE_CALL* getCapturedTransformCountV1)(
            std::uint64_t ownerToken);
        std::uint32_t(ROCK_REANIMATE_CALL* copyCapturedTransformsV1)(
            std::uint64_t ownerToken,
            ReanimateCapturedTransformV1* outTransforms,
            std::uint32_t maxTransforms);
        bool(ROCK_REANIMATE_CALL* getNativeHandPoseV1)(
            std::uint64_t ownerToken,
            ReanimateHandV1 hand,
            ReanimateNativeHandPoseV1* outPose);
        ReanimateResultV1(ROCK_REANIMATE_CALL* registerConsumerV1)(
            const ReanimateConsumerRegistrationV1* registration,
            ReanimateConsumerHandleV1* outHandle);
        ReanimateResultV1(ROCK_REANIMATE_CALL* unregisterConsumerV1)(
            std::uint64_t ownerToken);
        std::uint32_t(ROCK_REANIMATE_CALL* getGrantedCapabilitiesV1)(
            std::uint64_t ownerToken);
        ReanimateResultV1(ROCK_REANIMATE_CALL* setAnimationAuthorityV1)(
            std::uint64_t ownerToken,
            const ReanimateAuthorityRequestV1* request);
        ReanimateResultV1(ROCK_REANIMATE_CALL* clearAnimationAuthorityV1)(
            std::uint64_t ownerToken);
        ReanimateResultV1(ROCK_REANIMATE_CALL* registerEventCallbackV1)(
            std::uint64_t ownerToken,
            ReanimateEventCallbackV1 callback,
            void* userData,
            std::uint64_t* outCallbackToken);
        ReanimateResultV1(ROCK_REANIMATE_CALL* unregisterEventCallbackV1)(
            std::uint64_t ownerToken,
            std::uint64_t callbackToken);
    };

    inline constexpr std::uint32_t ROCK_REANIMATE_PROVIDER_API_V1_TABLE_BYTES =
        static_cast<std::uint32_t>(sizeof(ReanimateProviderApiV1));

    static_assert(sizeof(ReanimateTransformV1) == 52);
    static_assert(alignof(ReanimateTransformV1) == 4);
    static_assert(std::is_standard_layout_v<ReanimateTransformV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateTransformV1>);
    static_assert(sizeof(ReanimateConsumerRegistrationV1) == 104);
    static_assert(alignof(ReanimateConsumerRegistrationV1) == 4);
    static_assert(std::is_standard_layout_v<ReanimateConsumerRegistrationV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateConsumerRegistrationV1>);
    static_assert(sizeof(ReanimateConsumerHandleV1) == 48);
    static_assert(alignof(ReanimateConsumerHandleV1) == 8);
    static_assert(std::is_standard_layout_v<ReanimateConsumerHandleV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateConsumerHandleV1>);
    static_assert(sizeof(ReanimateAuthorityRequestV1) == 48);
    static_assert(alignof(ReanimateAuthorityRequestV1) == 4);
    static_assert(std::is_standard_layout_v<ReanimateAuthorityRequestV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateAuthorityRequestV1>);
    static_assert(sizeof(ReanimateConfigStateV1) == 64);
    static_assert(alignof(ReanimateConfigStateV1) == 8);
    static_assert(std::is_standard_layout_v<ReanimateConfigStateV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateConfigStateV1>);
    static_assert(sizeof(ReanimateRuntimeStateV1) == 120);
    static_assert(alignof(ReanimateRuntimeStateV1) == 8);
    static_assert(std::is_standard_layout_v<ReanimateRuntimeStateV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateRuntimeStateV1>);
    static_assert(sizeof(ReanimateCapturedTransformV1) == 152);
    static_assert(alignof(ReanimateCapturedTransformV1) == 4);
    static_assert(std::is_standard_layout_v<ReanimateCapturedTransformV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateCapturedTransformV1>);
    static_assert(sizeof(ReanimateNativeHandPoseV1) == 888);
    static_assert(alignof(ReanimateNativeHandPoseV1) == 8);
    static_assert(std::is_standard_layout_v<ReanimateNativeHandPoseV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateNativeHandPoseV1>);
    static_assert(sizeof(ReanimateEventV1) == 168);
    static_assert(alignof(ReanimateEventV1) == 8);
    static_assert(std::is_standard_layout_v<ReanimateEventV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateEventV1>);
    static_assert(sizeof(ReanimateProviderApiV1) == 120);
    static_assert(alignof(ReanimateProviderApiV1) == 8);
    static_assert(std::is_standard_layout_v<ReanimateProviderApiV1>);
    static_assert(std::is_trivially_copyable_v<ReanimateProviderApiV1>);

    class ReanimateApi
    {
    public:
        inline static const ReanimateProviderApiV1* inst = nullptr;

        [[nodiscard]] static int initialize(
            const std::uint32_t minVersion = ROCK_REANIMATE_API_VERSION)
        {
#if defined(_WIN32)
            inst = nullptr;
            const auto module = GetModuleHandleW(L"ROCK_Reanimate.dll");
            if (!module) {
                return 1;
            }
            using GetProviderApiFn =
                const ReanimateProviderApiV1*(ROCK_REANIMATE_CALL*)(std::uint32_t);
            const auto getApi = reinterpret_cast<GetProviderApiFn>(
                GetProcAddress(module, "ROCKREANIMATEAPI_GetProviderApi"));
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
            return 0;
#else
            (void)minVersion;
            return 1;
#endif
        }
    };
}

extern "C" ROCK_REANIMATE_API
    const rock_reanimate::api::ReanimateProviderApiV1* ROCK_REANIMATE_CALL
    ROCKREANIMATEAPI_GetProviderApi(std::uint32_t requestedVersion);
