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
    inline constexpr std::uint32_t PAPER_MOD_VERSION = 100;
    inline constexpr std::uint32_t PAPER_MAX_CONSUMERS_V1 = 16;
    inline constexpr std::uint32_t PAPER_MAX_CALLBACKS_V1 = 16;
    inline constexpr std::uint32_t PAPER_MAX_CAPTURED_TRANSFORMS_V1 = 192;
    inline constexpr std::uint32_t PAPER_FINGER_TRANSFORM_COUNT_V1 = 15;
    inline constexpr std::uint32_t PAPER_TRANSFORM_NAME_CAPACITY_V1 = 64;
    inline constexpr std::uint32_t PAPER_MAX_AUTHORITY_LEASE_FRAMES_V1 = 1200;

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
    };

    enum class PaperConsumerCapabilityV1 : std::uint32_t
    {
        None = 0,
        RuntimeState = 1u << 0,
        CapturedTransforms = 1u << 1,
        NativeHandPose = 1u << 2,
        AnimationAuthority = 1u << 3,
        FrameCallbacks = 1u << 4,
        All = (1u << 5) - 1u,
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
    };

    inline constexpr std::uint32_t PAPER_PROVIDER_API_V1_TABLE_BYTES =
        static_cast<std::uint32_t>(sizeof(PaperProviderApiV1));

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
    static_assert(sizeof(PaperProviderApiV1) == 120);
    static_assert(alignof(PaperProviderApiV1) == 8);
    static_assert(std::is_standard_layout_v<PaperProviderApiV1>);
    static_assert(std::is_trivially_copyable_v<PaperProviderApiV1>);

    class PaperApi
    {
    public:
        inline static const PaperProviderApiV1* inst = nullptr;

        [[nodiscard]] static int initialize(
            const std::uint32_t minVersion = PAPER_API_VERSION)
        {
#if defined(_WIN32)
            inst = nullptr;
            const auto module = GetModuleHandleW(L"PAPER.dll");
            if (!module) {
                return 1;
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
            return 0;
#else
            (void)minVersion;
            return 1;
#endif
        }
    };
}

extern "C" PAPER_API
    const paper::api::PaperProviderApiV1* PAPER_CALL
    PAPERAPI_GetProviderApi(std::uint32_t requestedVersion);
