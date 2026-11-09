#include "animation_evidence/ClipTelemetry.h"
#include "compat/ReloadSelectionTrace.h"

#include "PaperLog.h"
#include "support/NativeMemory.h"
#include "support/AcquiredAnimationGraphManager.h"
#include "support/TransformMath.h"

#include <Windows.h>
#include <RPS/Runtime/HookChain.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string_view>

namespace paper::clip_telemetry
{
    namespace
    {
        /*
         * Offsets verified against the FO4VR binary (Ghidra decompiles plus
         * the VR address library). Chain: WeaponAnimationGraphManagerHolder ->
         * BSAnimationGraphManager -> BShkbAnimationGraph -> inline hkbCharacter
         * -> hkbCharacterSetup -> hkaSkeleton.
         */

        // WeaponAnimationGraphManagerHolder (ctor disassembly, 0x140812c20):
        // +0x00 IAnimationGraphManagerHolder vtable, +0x08 second base
        // vtable, +0x10 refcount, +0x18 =
        // BSTSmartPointer<BSAnimationGraphManager>. (+0x8 is a vtable
        // pointer - plausible-looking, which is why a wrong read here fails
        // one hop later at the graphs array.)
        constexpr std::uintptr_t kHolderManagerOffset = 0x18;

        // BSAnimationGraphManager (ctor decompile, 0x14168f4f0): graphs are a
        // BSTSmallArray of BSTSmartPointer<BShkbAnimationGraph> - capacity
        // dword at +0x40 (bit31 set = inline storage), storage at +0x48
        // (inline entry or heap pointer).
        constexpr std::uintptr_t kManagerGraphsCapacityOffset = 0x40;
        constexpr std::uintptr_t kManagerGraphsStorageOffset = 0x48;
        constexpr std::uint32_t kGraphsInlineStorageFlag = 0x8000'0000u;
        // Graph slots read per manager; bounded far above any real manager
        // (the player carries two graphs: first- and third-person).
        constexpr std::uint32_t kMaxGraphs = 8;

        // BShkbAnimationGraph: hkbCharacter is INLINE at +0x1C8 - the graph
        // ctor (0x1416a3150) constructs it at this[1].field_0x50 with a
        // 0x178-byte Ghidra struct (0x178 + 0x50), matching the binding-set
        // builder receiving graph+0x1C8 as its owner (hkBaseObject_data base
        // is +0x8, so its field_0x1c0 is absolute 0x1C8).
        constexpr std::uintptr_t kGraphCharacterOffset = 0x1C8;

        // hkbCharacter: +0x78 hkbCharacterSetup*.
        constexpr std::uintptr_t kCharacterSetupOffset = 0x78;

        // hkbCharacterSetup: +0x20 m_animationSkeleton (the skeleton the clip
        // tracks map to).
        constexpr std::uintptr_t kSetupAnimationSkeletonOffset = 0x20;

        // hkaAnimationBinding members, checked against the engine's own clip
        // validator (hkbClipGenerator vtable slot 14, 0x14192d7a0): +0x18
        // animation (duration read at anim+0x14 confirms), +0x20/+0x28
        // transform track-to-bone shorts (the validator requires this array to
        // be identity when partitions are used), +0x40/+0x48 the partition
        // indices array.
        constexpr std::uintptr_t kBindingAnimationOffset = 0x18;
        constexpr std::uintptr_t kBindingTrackToBoneDataOffset = 0x20;
        constexpr std::uintptr_t kBindingTrackToBoneCountOffset = 0x28;
        // Partition-mapped clips (the weapon clips: empty track map, one
        // partition): tracks cover the bones of the listed skeleton
        // partitions in order. Engine validator 0x141a0c4e0: binding+0x40
        // partition indices (ushort) / +0x48 count, checked against the
        // skeleton's partition count at skeleton+0x80 (partitions hkArray
        // data at +0x78). Partition entry = { name*, int16 startBone,
        // int16 numBones }, 0x10 stride.
        constexpr std::uintptr_t kBindingPartitionIndicesOffset = 0x40;
        constexpr std::uintptr_t kBindingPartitionCountOffset = 0x48;
        constexpr std::uintptr_t kSkeletonPartitionsDataOffset = 0x78;
        constexpr std::uintptr_t kSkeletonPartitionsCountOffset = 0x80;
        constexpr std::uintptr_t kSkeletonPartitionStride = 0x10;
        constexpr std::uintptr_t kPartitionStartBoneOffset = 0x8;
        constexpr std::uintptr_t kPartitionNumBonesOffset = 0xA;

        // hkaSkeleton members (bones array of 0x10-byte hkaBone entries with
        // the name char* at +0; low pointer bit is an engine flag).
        // parentIndices (int16 per bone) sits at +0x18/+0x20, consistent
        // with the verified bones array at +0x28.
        constexpr std::uintptr_t kSkeletonParentIndicesOffset = 0x18;
        constexpr std::uintptr_t kSkeletonParentIndicesCountOffset = 0x20;
        // Reference locals, also consumed by ExactClipPreharvest.
        constexpr std::uintptr_t kSkeletonReferencePoseOffset = 0x38;
        constexpr std::uintptr_t kSkeletonReferencePoseCountOffset = 0x40;
        constexpr std::uintptr_t kSkeletonBonesDataOffset = 0x28;
        constexpr std::uintptr_t kSkeletonBonesCountOffset = 0x30;
        constexpr std::uintptr_t kSkeletonBoneStride = 0x10;

        // hkaAnimation members.
        constexpr std::uintptr_t kAnimationDurationOffset = 0x14;
        constexpr std::uintptr_t kAnimationTrackCountOffset = 0x18;

        // hkaSplineCompressedAnimation internals (playback sampler
        // disassembly 0x141F71B70, block resolver 0x142051280): +0x3C
        // numBlocks, +0x40 maxFramesPerBlock (the sampler divides by
        // maxFramesPerBlock-1 unguarded), hkArrays m_blockOffsets (+0x58
        // data / +0x60 count), m_floatBlockOffsets (+0x68 / +0x70 - read
        // unconditionally by the sampler even with zero float tracks) and
        // m_data (+0x98 / +0xA0). The sampler guards none of these - it
        // assumes the clip is loaded because it only samples playing clips.
        // Binding-set STUBS carry token allocations here (m_data ~0x20
        // bytes), so residency is gated on the array SIZES: a real clip's
        // mask stream needs at least one byte per track.
        constexpr std::uintptr_t kSplineNumBlocksOffset = 0x3C;
        constexpr std::uintptr_t kSplineMaxFramesPerBlockOffset = 0x40;
        constexpr std::uintptr_t kSplineBlockOffsetsOffset = 0x58;
        constexpr std::uintptr_t kSplineBlockOffsetsCountOffset = 0x60;
        constexpr std::uintptr_t kSplineFloatBlockOffsetsOffset = 0x68;
        constexpr std::uintptr_t kSplineFloatBlockOffsetsCountOffset = 0x70;
        constexpr std::uintptr_t kSplineDataBaseOffset = 0x98;
        constexpr std::uintptr_t kSplineDataSizeOffset = 0xA0;
        // Full-pose decode buffer: covers the 95-bone player rigs with
        // slack; clips with more tracks are not traced.
        constexpr std::uint32_t kSampleBufferTracks = 160;

        /*
         * hkbClipGenerator (0x160 bytes; clone at 0x14192d950 allocates it).
         * Lifecycle slots, raw-disassembly verified (vtable read from the
         * unpacked exe + body disasm of each target):
         *  - +0x38 activate   (0x14192CA40): backs up m_triggers +0x98 into
         *    +0xD8, constructs the hkaDefaultAnimationControl and stores it
         *    at +0xD0, caches the binding at +0xE8 - after it returns the
         *    payload is resident (control+0x38 = hkaAnimationBinding);
         *  - +0x50 deactivate (0x14192D510): decrefs and NULLS +0xD0,
         *    restores +0x98 from the +0xD8 backup.
         * The trace hooks ACTIVATE with an atomic pointer swap and runs after
         * the original, while the binding is resident. A hook on +0x50 would
         * read [clip+0xD0] after deactivate nulled it.
         */
        constexpr std::uintptr_t kClipGeneratorVtableModuleOffset = 0x2E0FB38;
        constexpr std::uintptr_t kClipGeneratorActivateSlotOffset = 0x38;
        constexpr std::uintptr_t kClipGeneratorDeactivateSlotOffset = 0x50;
        constexpr std::uintptr_t kClipGeneratorActivateFunctionOffset =
            0x192CA40;
        constexpr std::uintptr_t kClipGeneratorDeactivateFunctionOffset =
            0x192D510;
        constexpr std::uintptr_t kClipGeneratorLoadedBindingOffset = 0xD0;
        // Read-only clip timing fields.
        constexpr std::uintptr_t kClipGeneratorCropStartOffset = 0xA4;
        constexpr std::uintptr_t kClipGeneratorCropEndOffset = 0xA8;
        constexpr std::uintptr_t kClipGeneratorLocalTimeOffset = 0x140;
        // hkbClipGenerator::m_animationName (hkStringPtr - mask low bit).
        // Raw disassembly 0x141939911: [clip+0x90] & ~1 formatted into
        // "Animation loaded directly from clip's animationName".
        constexpr std::uintptr_t kClipGeneratorAnimationNameOffset = 0x90;
        constexpr std::uintptr_t kLoadedBindingWrapperBindingOffset = 0x38;
        // hkaAnimation vtable slot 5 - the engine's own playback sampler
        // ("TtSampleSpline" profile marker, 0x141F71B70): sampleTracks(
        //   float time, uint32 transformTracks, hkQsTransform* out,
        //   uint32 floatTracks, float* floatsOut). Decodes tracks
        // 0..transformTracks-1 sequentially from the per-block mask streams
        // inside m_data. Slot 6 (per-track sampling through
        // m_transformOffsets) is dead code in FO4: that array is never
        // populated, loaded or stub.
        constexpr std::size_t kSampleTracksSlot = 5;

        /*
         * Annotation/trigger offsets, verified via the destructor chain and
         * trigger walker (two raw-disassembly sources):
         *  - ~hkaAnimation (0x141A31580) walks m_annotationTracks at
         *    +0x28 (data) / +0x30 (count) with stride 0x18 and frees
         *    count*0x18 bytes; +0x20 is the refcount-released
         *    extractedMotion, consistent with the +0x14/+0x18 members
         *    verified earlier.
         *  - ~hkaAnnotationTrack (0x141A31650) releases the hkStringPtr
         *    trackName at +0x0, walks annotations at +0x8/+0x10 with
         *    stride 0x10, and releases each annotation's hkStringPtr text
         *    at element+0x8 (element+0x0 is the float time - never
         *    destructed).
         *  - The hkbClipGenerator trigger walker (0x14192F160, called from
         *    update/vfunction9) reads m_triggers at clip+0x98 (refcounted
         *    object with hkArray at +0x10 data / +0x18 count) and fires
         *    triggers of stride 0x20: +0x0 float localTime, +0x8 int32
         *    eventId (graph-local), +0x10 payload.
         * The reflection name pool independently confirms the member names
         * ("annotationTracks", "triggers", "hkaAnnotationTrackAnnotation").
         */
        constexpr std::uintptr_t kAnimationAnnotationTracksDataOffset = 0x28;
        constexpr std::uintptr_t kAnimationAnnotationTracksCountOffset = 0x30;
        constexpr std::uintptr_t kAnnotationTrackStride = 0x18;
        constexpr std::uintptr_t kAnnotationTrackNameOffset = 0x0;
        constexpr std::uintptr_t kAnnotationTrackAnnotationsDataOffset = 0x8;
        constexpr std::uintptr_t kAnnotationTrackAnnotationsCountOffset = 0x10;
        constexpr std::uintptr_t kAnnotationStride = 0x10;
        constexpr std::uintptr_t kAnnotationTimeOffset = 0x0;
        constexpr std::uintptr_t kAnnotationTextOffset = 0x8;
        constexpr std::uintptr_t kClipGeneratorTriggersOffset = 0x98;
        constexpr std::uintptr_t kTriggerArrayDataOffset = 0x10;
        constexpr std::uintptr_t kTriggerArrayCountOffset = 0x18;
        constexpr std::uintptr_t kTriggerStride = 0x20;
        constexpr std::uintptr_t kTriggerLocalTimeOffset = 0x0;
        constexpr std::uintptr_t kTriggerEventIdOffset = 0x8;
        /*
         * eventId -> name chain, raw-disassembly verified (two agreeing
         * sources per hop):
         *  - hkbContext+0x10 = hkbBehaviorGraph* (the engine's own trigger
         *    walker 0x14192f160 reads [ctx+0x10]+0xC8 and indexes its
         *    +0x40/+0x48 arrays with the trigger eventId = exactly
         *    hkbBehaviorGraphData::eventInfos); gated by an exact vtable
         *    match, so a context-layout surprise degrades into "no name".
         *  - graph+0xC8 = m_data (finish-ctor 0x1417c3c80 preserves it as a
         *    serialized ref between pseudoRandomGenerator +0xB8, seed 0x529,
         *    and the reset runtime template +0xD0).
         *  - data+0x68 = m_stringData (two static hkClassMember arrays,
         *    0x142e26f50 / 0x142e27040-4*0x28, registration objectSize 0x70).
         *  - stringData+0x10/+0x18 = m_eventNames hkArray<hkStringPtr>
         *    (reflection record 0x142e41000 + ctor 0x141937eb0).
         * eventIds are GRAPH-LOCAL indices into this array.
         */
        constexpr std::uintptr_t kContextBehaviorGraphOffset = 0x10;
        constexpr std::uintptr_t kBehaviorGraphVtableModuleOffset = 0x2E04848;
        constexpr std::uintptr_t kBehaviorGraphDataOffset = 0xC8;
        constexpr std::uintptr_t kGraphDataStringDataOffset = 0x68;
        constexpr std::uintptr_t kStringDataEventNamesDataOffset = 0x10;
        constexpr std::uintptr_t kStringDataEventNamesCountOffset = 0x18;
        constexpr std::int32_t kMaxPlausibleEventNames = 4096;
        // Annotation tracks are one-per-transform-track (per bone), so the
        // cap must cover full first-person rigs (real clips report 94..123).
        constexpr std::int32_t kMaxPlausibleAnnotationTracks = 512;
        constexpr std::int32_t kMaxPlausibleAnnotations = 256;
        constexpr std::int32_t kMaxPlausibleTriggers = 128;
        // Per-target line budget for raw marker diagnostics, reset when the
        // trace target changes.
        constexpr std::uint32_t kMarkerLogBudgetPerGeneration = 160;

        constexpr float kMinClipDurationSeconds = 0.01f;
        constexpr float kMaxClipDurationSeconds = 300.0f;
        constexpr std::int32_t kMaxPlausibleTrackCount = 512;
        constexpr std::int32_t kMaxPlausibleBoneCount = 4096;
        constexpr std::size_t kClipNameCapacity = 260;

        // hkQsTransform: vec4 translate, quaternion (x,y,z,w), vec4 scale.
        struct HkQsTransform
        {
            float translate[4];
            float rotate[4];
            float scale[4];
        };
        static_assert(sizeof(HkQsTransform) == 48);

        // sampleTracks(this, time, transformTrackCount, transformsOut,
        // floatTrackCount, floatsOut) - MSVC x64: time lands in XMM1.
        using SampleTracks_t = void (*)(void*, float, std::uint32_t, HkQsTransform*, std::uint32_t, float*);

        // Every activate-shim entry; the trace summary reports the calls
        // since its target was set.
        std::atomic<std::uint64_t> s_hookCalls{ 0 };

        // The hooks fire on the engine's graph update thread for every actor's
        // clip generators; the trace target is guarded by s_hookMutex (writer:
        // main thread, rare; readers: the hooks, sparse).
        constexpr std::size_t kMaxHookCharacters = 8;
        std::mutex s_hookMutex;

        // Diagnostic ownership: target identities and budgets use s_hookMutex.
        // Stored engine addresses are comparison keys only, never dereferenced
        // outside the activation/deactivation callback that supplies them.
        struct HandActionTrace
        {
            struct SampledBinding
            {
                std::uintptr_t binding{ 0 };
                std::uintptr_t skeleton{ 0 };
            };
            std::shared_ptr<spdlog::logger> log;
            std::array<std::uintptr_t, kMaxHookCharacters> characters{};
            std::array<SampledBinding, 32> sampledBindings{};
            std::array<std::uintptr_t, 8> mappedSkeletons{};
            std::array<std::uintptr_t, 16> activeClips{};
            std::uintptr_t weaponNode{ 0 };
            std::uint64_t generation{ 0 };
            std::uint32_t formId{ 0 };
            std::uint32_t samplesUsed{ 0 };
            std::uint32_t eventsRemaining{ 512 };
            std::uint64_t hookCallsAtTarget{ 0 };
            std::uint32_t activationMatches{ 0 };
            std::uint32_t unmatchedContextsLogged{ 0 };
            std::uint32_t framesSinceSummary{ 0 };
            bool faulted{ false };
        };
        HandActionTrace s_handActionTrace;
        // Guarded by s_hookMutex like the trace above.
        std::uint32_t s_markerLogBudget = kMarkerLogBudgetPerGeneration;

        // Coarse pointer plausibility gate for values read out of engine
        // objects; rejects null, refcount headers, and other small integers
        // before they are dereferenced.
        [[nodiscard]] bool plausiblePointer(std::uintptr_t value)
        {
            return value > 0x10000 && value < 0x0000'8000'0000'0000ull;
        }

        [[nodiscard]] std::uintptr_t moduleRelative(std::uintptr_t address)
        {
            const auto base = REL::Module::get().base();
            return address >= base ? address - base : address;
        }

        // Module-relative vtable of a heap object (0 when the pointer is
        // implausible); a rebased value can be looked up directly in the
        // binary to identify the object's real runtime type.
        [[nodiscard]] std::uintptr_t objectVtableRel(std::uintptr_t object)
        {
            return plausiblePointer(object) ? moduleRelative(*reinterpret_cast<const std::uintptr_t*>(object)) : 0;
        }

        const char* skeletonBoneName(std::uintptr_t skeleton, std::int32_t boneIndex)
        {
            const auto bonesData = *reinterpret_cast<std::uintptr_t*>(skeleton + kSkeletonBonesDataOffset);
            if (!plausiblePointer(bonesData)) {
                return nullptr;
            }
            const auto entry = bonesData + static_cast<std::uintptr_t>(boneIndex) * kSkeletonBoneStride;
            const auto namePtr = *reinterpret_cast<std::uintptr_t*>(entry) & ~static_cast<std::uintptr_t>(1);
            return plausiblePointer(namePtr) ? reinterpret_cast<const char*>(namePtr) : nullptr;
        }

        // Copies a printable hkStringPtr (low-bit owned-flag convention)
        // into out; returns the length, 0 on any implausible byte.
        std::size_t copyHkStringPtr(std::uintptr_t stringField, char* out, std::size_t capacity)
        {
            out[0] = '\0';
            const auto pointer = stringField & ~static_cast<std::uintptr_t>(1);
            if (!plausiblePointer(pointer)) {
                return 0;
            }
            const char* chars = reinterpret_cast<const char*>(pointer);
            std::size_t length = 0;
            while (length < capacity - 1 && chars[length] != '\0') {
                const unsigned char c = static_cast<unsigned char>(chars[length]);
                if (c < 0x20 || c > 0x7E) {
                    out[0] = '\0';
                    return 0;
                }
                out[length] = chars[length];
                ++length;
            }
            out[length] = '\0';
            return length;
        }

        /*
         * Resolves a graph-local trigger eventId to its authored name via
         * graph->m_data->m_stringData->m_eventNames[id]. Every hop fails
         * closed into "no name"; the caller already vtable-gated the graph.
         */
        [[nodiscard]] std::int32_t behaviorGraphEventNameCount(std::uintptr_t behaviorGraph)
        {
            if (behaviorGraph == 0) {
                return -1;
            }
            const auto data = *reinterpret_cast<std::uintptr_t*>(behaviorGraph + kBehaviorGraphDataOffset);
            if (!plausiblePointer(data)) {
                return -1;
            }
            const auto stringData = *reinterpret_cast<std::uintptr_t*>(data + kGraphDataStringDataOffset);
            if (!plausiblePointer(stringData)) {
                return -1;
            }
            const auto count =
                *reinterpret_cast<std::int32_t*>(stringData + kStringDataEventNamesCountOffset);
            return (count >= 0 && count <= kMaxPlausibleEventNames) ? count : -1;
        }

        bool resolveGraphEventName(
            std::uintptr_t behaviorGraph,
            std::int32_t eventId,
            char* out,
            std::size_t capacity)
        {
            out[0] = '\0';
            if (behaviorGraph == 0 || eventId < 0) {
                return false;
            }
            const auto data = *reinterpret_cast<std::uintptr_t*>(behaviorGraph + kBehaviorGraphDataOffset);
            if (!plausiblePointer(data)) {
                return false;
            }
            const auto stringData = *reinterpret_cast<std::uintptr_t*>(data + kGraphDataStringDataOffset);
            if (!plausiblePointer(stringData)) {
                return false;
            }
            const auto count =
                *reinterpret_cast<std::int32_t*>(stringData + kStringDataEventNamesCountOffset);
            if (count <= 0 || count > kMaxPlausibleEventNames || eventId >= count) {
                return false;
            }
            const auto names =
                *reinterpret_cast<std::uintptr_t*>(stringData + kStringDataEventNamesDataOffset);
            if (!plausiblePointer(names)) {
                return false;
            }
            return copyHkStringPtr(
                       *reinterpret_cast<std::uintptr_t*>(names + static_cast<std::uintptr_t>(eventId) * 8),
                       out,
                       capacity) > 0;
        }

        /*
         * RAW ANIMATION MARKERS: the engine's authored timing records are
         * logged without interpretation. Two kinds:
         *  - hkaAnimation annotation tracks: authored {time, text} markers
         *    baked into the clip (real names at exact times);
         *  - hkbClipGenerator triggers: {localTime, eventId} pairs the game
         *    itself fires as anim events (SoundPlay, EjectShellCasing,
         *    reloadComplete). Event ids are graph-local; each is resolved
         *    through the clip's own graph
         *    (hkbBehaviorGraphStringData::eventNames).
         * Every hop is plausibility-gated and degrades into a logged skip,
         * and every logged clip emits exactly one DEBUG summary line with raw
         * structure counts, so "the clip carries no markers" and "a gate
         * rejected everything" are distinguishable from the log alone. Hard
         * capped by s_markerLogBudget per trace target.
         */
        void logClipMarkers(
            std::uintptr_t clipGenerator,
            std::uintptr_t binding,
            const char* clipName,
            std::uintptr_t behaviorGraph)
        {
            if (s_markerLogBudget == 0) {
                return;
            }
            const auto animation = *reinterpret_cast<std::uintptr_t*>(binding + kBindingAnimationOffset);
            if (!plausiblePointer(animation)) {
                --s_markerLogBudget;
                PAPER_LOG_DEBUG(Weapon,
                    "ANIMATION-MARKER summary clip='{}': skipped, animation pointer implausible", clipName);
                return;
            }
            const float duration = *reinterpret_cast<float*>(animation + kAnimationDurationOffset);
            if (!std::isfinite(duration) || duration <= 0.0f || duration > kMaxClipDurationSeconds) {
                --s_markerLogBudget;
                PAPER_LOG_DEBUG(Weapon,
                    "ANIMATION-MARKER summary clip='{}': skipped, duration {:.3f} implausible", clipName, duration);
                return;
            }

            const auto trackCount =
                *reinterpret_cast<std::int32_t*>(animation + kAnimationAnnotationTracksCountOffset);
            const auto trackData =
                *reinterpret_cast<std::uintptr_t*>(animation + kAnimationAnnotationTracksDataOffset);
            std::uint32_t annotationLines = 0;
            if (trackCount > 0 && trackCount <= kMaxPlausibleAnnotationTracks && plausiblePointer(trackData)) {
                for (std::int32_t t = 0; t < trackCount; ++t) {
                    const auto track = trackData + static_cast<std::uintptr_t>(t) * kAnnotationTrackStride;
                    char trackName[64];
                    copyHkStringPtr(
                        *reinterpret_cast<std::uintptr_t*>(track + kAnnotationTrackNameOffset),
                        trackName,
                        sizeof(trackName));
                    const auto annotationCount =
                        *reinterpret_cast<std::int32_t*>(track + kAnnotationTrackAnnotationsCountOffset);
                    const auto annotationData =
                        *reinterpret_cast<std::uintptr_t*>(track + kAnnotationTrackAnnotationsDataOffset);
                    if (annotationCount <= 0 || annotationCount > kMaxPlausibleAnnotations ||
                        !plausiblePointer(annotationData)) {
                        continue;
                    }
                    for (std::int32_t a = 0; a < annotationCount; ++a) {
                        const auto annotation =
                            annotationData + static_cast<std::uintptr_t>(a) * kAnnotationStride;
                        const float time = *reinterpret_cast<float*>(annotation + kAnnotationTimeOffset);
                        if (!std::isfinite(time) || time < -1.0f || time > duration + 1.0f) {
                            continue;
                        }
                        char text[96];
                        if (copyHkStringPtr(
                                *reinterpret_cast<std::uintptr_t*>(annotation + kAnnotationTextOffset),
                                text,
                                sizeof(text)) == 0) {
                            continue;
                        }
                        ++annotationLines;
                        if (s_markerLogBudget > 1) {
                            --s_markerLogBudget;
                            PAPER_LOG_DEBUG(Weapon,
                                "ANIMATION-MARKER [annotation] clip='{}' track='{}' t={:.3f}/{:.3f}s text='{}'",
                                clipName,
                                trackName,
                                time,
                                duration,
                                text);
                        }
                    }
                }
            }

            std::uint32_t triggerLines = 0;
            std::int32_t triggerCountRaw = 0;
            const auto triggersObject =
                *reinterpret_cast<std::uintptr_t*>(clipGenerator + kClipGeneratorTriggersOffset);
            if (plausiblePointer(triggersObject)) {
                triggerCountRaw =
                    *reinterpret_cast<std::int32_t*>(triggersObject + kTriggerArrayCountOffset);
                const auto triggerData =
                    *reinterpret_cast<std::uintptr_t*>(triggersObject + kTriggerArrayDataOffset);
                if (triggerCountRaw > 0 && triggerCountRaw <= kMaxPlausibleTriggers &&
                    plausiblePointer(triggerData)) {
                    for (std::int32_t i = 0; i < triggerCountRaw; ++i) {
                        const auto trigger = triggerData + static_cast<std::uintptr_t>(i) * kTriggerStride;
                        const float localTime = *reinterpret_cast<float*>(trigger + kTriggerLocalTimeOffset);
                        // relativeToEndOfClip triggers carry negative times.
                        if (!std::isfinite(localTime) || localTime < -(duration + 1.0f) ||
                            localTime > duration + 1.0f) {
                            continue;
                        }
                        const auto eventId =
                            *reinterpret_cast<std::int32_t*>(trigger + kTriggerEventIdOffset);
                        ++triggerLines;
                        char eventName[96]{};
                        resolveGraphEventName(behaviorGraph, eventId, eventName, sizeof(eventName));
                        if (s_markerLogBudget > 1) {
                            --s_markerLogBudget;
                            PAPER_LOG_DEBUG(Weapon,
                                "ANIMATION-MARKER [trigger] clip='{}' t={:.3f}/{:.3f}s eventId={} name='{}'",
                                clipName,
                                localTime,
                                duration,
                                eventId,
                                eventName);
                        }
                    }
                }
            }
            if (s_markerLogBudget > 0) {
                --s_markerLogBudget;
                PAPER_LOG_DEBUG(Weapon,
                    "ANIMATION-MARKER summary clip='{}' duration={:.2f}s annotationTracksRaw={} annotationsLogged={} triggersObject={} triggersRaw={} triggersLogged={} graphEventNames={}",
                    clipName,
                    duration,
                    trackCount,
                    annotationLines,
                    plausiblePointer(triggersObject) ? "set" : "null",
                    triggerCountRaw,
                    triggerLines,
                    behaviorGraphEventNameCount(behaviorGraph));
            }
        }

        // Temporary investigation: distinguish the loaded binding's map from
        // the selected rig before interpreting hand participation. Remove this
        // metadata dump after that mapping is verified. Only the debug trace
        // calls it, under s_hookMutex, once per binding/rig pair; stored
        // addresses are comparison keys, not retained readable objects.
        template <class T>
        bool readHandMappingField(const std::uintptr_t base,
            const std::uintptr_t offset, T& value)
        {
            return plausiblePointer(base) &&
                native_memory::tryReadField(reinterpret_cast<const void*>(base), offset, value);
        }

        std::array<char, 64> handMappingName(const std::uintptr_t stringPointer)
        {
            std::array<char, 64> name{};
            const auto pointer = stringPointer & ~std::uintptr_t{ 1 };
            if (!plausiblePointer(pointer) ||
                !native_memory::guardedCopyFromMemory(
                    reinterpret_cast<const void*>(pointer), name.data(), name.size() - 1)) {
                std::memcpy(name.data(), "<unreadable>", 12);
            }
            for (auto& character : name) {
                if (character == '\0') break;
                if (static_cast<unsigned char>(character) < 32 ||
                    static_cast<unsigned char>(character) > 126) character = '?';
            }
            return name;
        }

        template <class Index>
        void traceHandMappingIndices(const char* kind, const std::uintptr_t binding,
            const std::uintptr_t data, const std::int32_t count)
        {
            auto& trace = s_handActionTrace;
            std::array<Index, 512> indices{};
            const bool valid = count >= 0 && count <= static_cast<std::int32_t>(indices.size()) &&
                (count == 0 || (plausiblePointer(data) && native_memory::guardedCopyFromMemory(
                    reinterpret_cast<const void*>(data), indices.data(), count * sizeof(Index))));
            trace.log->debug("HAND_MAPPING array binding={:X} kind={} data={:X} count={} readable={}",
                binding, kind, data, count, valid);
            if (!valid) return;
            fmt::memory_buffer values;
            for (std::int32_t first = 0; first < count; first += 32) {
                values.clear();
                for (auto index = first; index < (std::min)(first + 32, count); ++index) {
                    fmt::format_to(std::back_inserter(values), "{}{}", index == first ? "" : ",",
                        static_cast<int>(indices[index]));
                }
                trace.log->debug("HAND_MAPPING indices binding={:X} kind={} first={} values=[{}]",
                    binding, kind, first, std::string_view(values.data(), values.size()));
            }
        }

        void traceHandMappingDefinition(const std::uintptr_t binding,
            const std::uintptr_t skeleton, const char* clipName)
        {
            auto& trace = s_handActionTrace;
            std::uintptr_t animation = 0, vtable = 0, map = 0, partitions = 0;
            std::uintptr_t bones = 0, parents = 0, rigPartitions = 0;
            std::int32_t trackCount = -1, mapCount = -1, partitionCount = -1;
            std::int32_t boneCount = -1, parentCount = -1, rigPartitionCount = -1, referenceCount = -1;
            float duration = 0.0f;
            const bool bindingReadable =
                readHandMappingField(binding, kBindingAnimationOffset, animation) &&
                readHandMappingField(binding, kBindingTrackToBoneDataOffset, map) &&
                readHandMappingField(binding, kBindingTrackToBoneCountOffset, mapCount) &&
                readHandMappingField(binding, kBindingPartitionIndicesOffset, partitions) &&
                readHandMappingField(binding, kBindingPartitionCountOffset, partitionCount);
            const bool animationReadable =
                readHandMappingField(animation, 0, vtable) &&
                readHandMappingField(animation, kAnimationDurationOffset, duration) &&
                readHandMappingField(animation, kAnimationTrackCountOffset, trackCount);
            const bool rigReadable =
                readHandMappingField(skeleton, kSkeletonBonesDataOffset, bones) &&
                readHandMappingField(skeleton, kSkeletonBonesCountOffset, boneCount) &&
                readHandMappingField(skeleton, kSkeletonParentIndicesOffset, parents) &&
                readHandMappingField(skeleton, kSkeletonParentIndicesCountOffset, parentCount) &&
                readHandMappingField(skeleton, kSkeletonPartitionsDataOffset, rigPartitions) &&
                readHandMappingField(skeleton, kSkeletonPartitionsCountOffset, rigPartitionCount) &&
                readHandMappingField(skeleton, kSkeletonReferencePoseCountOffset, referenceCount);
            trace.log->debug("HAND_MAPPING definition version=1 weapon={:08X}/{:016X} binding={:X} skeleton={:X} clip='{}' animation={:X} vtableRel={:X} duration={} tracks={} mapCount={} partitionCount={} bones={} parents={} referencePoses={} rigPartitions={} readable={}/{}/{}",
                trace.formId, trace.generation, binding, skeleton, clipName, animation, moduleRelative(vtable),
                duration, trackCount, mapCount, partitionCount, boneCount, parentCount, referenceCount,
                rigPartitionCount, bindingReadable, animationReadable, rigReadable);
            if (bindingReadable) {
                traceHandMappingIndices<std::int16_t>("track-to-bone", binding, map, mapCount);
                traceHandMappingIndices<std::uint16_t>("partition-selection", binding, partitions, partitionCount);
            }
            if (!rigReadable) return;
            for (const auto previous : trace.mappedSkeletons) if (previous == skeleton) return;
            const auto slot = std::find(trace.mappedSkeletons.begin(), trace.mappedSkeletons.end(), 0);
            if (slot == trace.mappedSkeletons.end()) {
                trace.log->debug("HAND_MAPPING rig unavailable skeleton={:X} stage=rig-budget", skeleton);
                return;
            }
            *slot = skeleton;

            // Batch names to avoid filling the bounded asynchronous queue with
            // one line per bone. Array counts and copies have independent caps.
            constexpr std::int32_t maxBones = 256;
            std::array<std::int16_t, maxBones> parentIndices{};
            const auto copiedParents = (std::clamp)(parentCount, 0, maxBones);
            const bool parentsReadable = parentCount >= 0 &&
                (copiedParents == 0 || (plausiblePointer(parents) && native_memory::guardedCopyFromMemory(
                    reinterpret_cast<const void*>(parents), parentIndices.data(), copiedParents * sizeof(std::int16_t))));
            trace.log->debug("HAND_MAPPING rig skeleton={:X} boneData={:X} parentData={:X} bones={} parents={} parentsReadable={} boneLimit={} partitionLimit=64",
                skeleton, bones, parents, boneCount, parentCount, parentsReadable, maxBones);
            fmt::memory_buffer values;
            const auto copiedBones = (std::clamp)(boneCount, 0, maxBones);
            for (std::int32_t first = 0; first < copiedBones; first += 8) {
                values.clear();
                for (auto index = first; index < (std::min)(first + 8, copiedBones); ++index) {
                    std::uintptr_t namePointer = 0;
                    const bool readable = readHandMappingField(bones, index * kSkeletonBoneStride, namePointer);
                    const auto name = handMappingName(readable ? namePointer : 0);
                    fmt::format_to(std::back_inserter(values), "{}{}:parent={}:name='{}'",
                        index == first ? "" : ";", index,
                        parentsReadable && index < copiedParents ? parentIndices[index] : -32768, name.data());
                }
                trace.log->debug("HAND_MAPPING bones skeleton={:X} first={} values=[{}]",
                    skeleton, first, std::string_view(values.data(), values.size()));
            }
            const auto copiedPartitions = (std::clamp)(rigPartitionCount, 0, 64);
            for (std::int32_t index = 0; index < copiedPartitions; ++index) {
                const auto entry = rigPartitions + index * kSkeletonPartitionStride;
                std::int16_t start = -1, count = -1;
                const bool readable = plausiblePointer(rigPartitions) &&
                    readHandMappingField(entry, kPartitionStartBoneOffset, start) &&
                    readHandMappingField(entry, kPartitionNumBonesOffset, count);
                trace.log->debug("HAND_MAPPING partition skeleton={:X} index={} start={} count={} readable={}",
                    skeleton, index, start, count, readable);
            }
            trace.log->debug("HAND_MAPPING rig-end skeleton={:X} bonesCopied={} partitionsCopied={} truncated={}",
                skeleton, copiedBones, copiedPartitions, boneCount > copiedBones || rigPartitionCount > copiedPartitions);
        }

        bool traceUnblendedHands(
            const std::uintptr_t binding, const std::uintptr_t skeleton,
            const std::uintptr_t animation, const float duration,
            const std::int32_t boneCount, const std::int32_t trackCount,
            const std::array<std::int16_t, kSampleBufferTracks>& trackBones,
            const std::int32_t mappedTrackCount, const char* clipName)
        {
            constexpr std::size_t maxChain = 32;
            constexpr std::uint32_t sampleCount = 33;
            const auto unavailable = [&](const char* stage) {
                s_handActionTrace.log->debug("HAND_ACTION unavailable weapon={:08X}/{:016X} binding={:X} stage={}",
                    s_handActionTrace.formId, s_handActionTrace.generation, binding, stage);
                return false;
            };
            const auto parents = *reinterpret_cast<const std::int16_t* const*>(
                skeleton + kSkeletonParentIndicesOffset);
            const auto parentCount = *reinterpret_cast<const std::int32_t*>(
                skeleton + kSkeletonParentIndicesCountOffset);
            // Same reference-pose layout used by ExactClipPreharvest. A
            // partition may omit ancestors; their reference locals are needed.
            const auto reference = *reinterpret_cast<const HkQsTransform* const*>(skeleton + kSkeletonReferencePoseOffset);
            const auto referenceCount = *reinterpret_cast<const std::int32_t*>(skeleton + kSkeletonReferencePoseCountOffset);
            if (trackCount > static_cast<std::int32_t>(kSampleBufferTracks) ||
                mappedTrackCount < trackCount || parentCount < boneCount ||
                referenceCount < boneCount || !parents || !reference) return unavailable("rig-arrays-or-track-capacity");

            struct Chain
            {
                std::array<std::int16_t, maxChain> bones{};
                std::array<std::int16_t, maxChain> tracks{};
                std::uint32_t count{ 0 };
                bool targetAnimated{ false };
            };
            std::array<Chain, 3> chains{};
            constexpr std::array names{ "Weapon", "RArm_Hand", "LArm_Hand" };
            for (std::size_t target = 0; target < chains.size(); ++target) {
                std::int32_t bone = -1;
                for (std::int32_t i = 0; i < boneCount; ++i) {
                    const auto* name = skeletonBoneName(skeleton, i);
                    if (name && std::strcmp(name, names[target]) == 0) { bone = i; break; }
                }
                if (bone < 0) return unavailable(names[target]);
                auto& chain = chains[target];
                while (bone >= 0 && chain.count < maxChain) {
                    if (bone >= boneCount) return unavailable("parent-index");
                    for (std::uint32_t i = 0; i < chain.count; ++i) {
                        if (chain.bones[i] == bone) return unavailable("parent-cycle");
                    }
                    auto& track = chain.tracks[chain.count];
                    track = -1;
                    for (std::int32_t i = 0; i < trackCount; ++i) {
                        if (trackBones[i] == bone) { track = static_cast<std::int16_t>(i); break; }
                    }
                    if (chain.count == 0) chain.targetAnimated = track >= 0;
                    chain.bones[chain.count++] = static_cast<std::int16_t>(bone);
                    bone = parents[bone];
                }
                if (bone >= 0) return unavailable("parent-depth");
            }

            const auto sampler = reinterpret_cast<SampleTracks_t>(
                (*reinterpret_cast<const std::uintptr_t* const*>(animation))[kSampleTracksSlot]);
            if (!sampler) return unavailable("sampler");
            alignas(16) std::array<HkQsTransform, kSampleBufferTracks> sampled{};
            auto& trace = s_handActionTrace;
            trace.log->debug("HAND_ACTION definition weapon={:08X}/{:016X} binding={:X} skeleton={:X} bones={} clip='{}' duration={:.6f} samples={} mappedTargets={}/{}/{}",
                trace.formId, trace.generation, binding, skeleton, boneCount, clipName, duration, sampleCount,
                chains[0].targetAnimated, chains[1].targetAnimated, chains[2].targetAnimated);
            for (std::uint32_t step = 0; step < sampleCount; ++step) {
                const float time = duration * static_cast<float>(step) / (sampleCount - 1);
                sampler(reinterpret_cast<void*>(animation), time, trackCount, sampled.data(), 0, nullptr);
                std::array<RE::NiTransform, 3> models{};
                for (std::size_t target = 0; target < chains.size(); ++target) {
                    auto& model = models[target];
                    model = transform_math::identityTransform<RE::NiTransform>();
                    const auto& chain = chains[target];
                    for (auto i = chain.count; i > 0; --i) {
                        const auto track = chain.tracks[i - 1];
                        const auto& raw = track >= 0 ? sampled[track] : reference[chain.bones[i - 1]];
                        for (int component = 0; component < 4; ++component) {
                            if (!std::isfinite(raw.translate[component]) || !std::isfinite(raw.rotate[component]) ||
                                !std::isfinite(raw.scale[component])) return unavailable("nonfinite-pose");
                        }
                        if (raw.scale[0] <= 0.0001f || std::abs(raw.scale[0] - raw.scale[1]) > 0.0001f ||
                            std::abs(raw.scale[0] - raw.scale[2]) > 0.0001f) return unavailable("nonuniform-or-zero-scale");
                        auto local = transform_math::identityTransform<RE::NiTransform>();
                        local.translate = { raw.translate[0], raw.translate[1], raw.translate[2] };
                        local.rotate = transform_math::havokQuaternionToNiRows<RE::NiMatrix3>(raw.rotate);
                        local.scale = raw.scale[0];
                        model = transform_math::composeTransforms(model, local);
                    }
                }
                for (std::size_t hand = 1; hand < models.size(); ++hand) {
                    const auto pose = transform_math::relativeTransform(models[0], models[hand]);
                    float q[4]{};
                    transform_math::niRowsToHavokQuaternion(pose.rotate, q);
                    trace.log->debug("HAND_ACTION sample weapon={:08X}/{:016X} binding={:X} step={} t={:.6f} hand={} T=({:.6f},{:.6f},{:.6f}) Qxyzw=({:.6f},{:.6f},{:.6f},{:.6f}) S={:.6f}",
                        trace.formId, trace.generation, binding, step, time, hand == 1 ? "right" : "left",
                        pose.translate.x, pose.translate.y, pose.translate.z, q[0], q[1], q[2], q[3], pose.scale);
                }
            }
            return true;
        }

        // Gates the binding the way the engine's playback sampler needs it
        // (the sampler itself guards nothing), resolves which bone each
        // transform track drives, then traces both hands over the clip.
        bool traceBindingHands(
            const std::uintptr_t binding,
            const std::uintptr_t skeleton,
            const char* clipAnimationName)
        {
            const auto rejected = [&](const char* stage) {
                s_handActionTrace.log->debug("HAND_MAPPING rejected binding={:X} skeleton={:X} clip='{}' stage={}",
                    binding, skeleton, clipAnimationName, stage);
                return false;
            };
            const auto animation = *reinterpret_cast<std::uintptr_t*>(binding + kBindingAnimationOffset);
            if (!plausiblePointer(animation)) {
                return rejected("animation-pointer");
            }

            // Only spline-compressed clips are supported; the sampler slot is
            // dispatched virtually but its semantics were verified on this
            // class specifically.
            const auto vtable = *reinterpret_cast<std::uintptr_t*>(animation);
            if (vtable != RE::VTABLE::hkaSplineCompressedAnimation[0].address()) {
                return rejected("unsupported-animation-type");
            }

            const float duration = *reinterpret_cast<float*>(animation + kAnimationDurationOffset);
            const auto animationTrackCount = *reinterpret_cast<std::int32_t*>(animation + kAnimationTrackCountOffset);
            if (!std::isfinite(duration) || duration < kMinClipDurationSeconds || duration > kMaxClipDurationSeconds ||
                animationTrackCount <= 0 || animationTrackCount > kMaxPlausibleTrackCount) {
                return rejected("clip-parameters");
            }

            // The engine sampler dereferences the spline payload unguarded;
            // binding-set stubs (token allocations, tiny m_data) and anything
            // else unloadable is skipped here. A real clip's mask stream needs
            // at least one byte per track per block.
            const auto splineNumBlocks = *reinterpret_cast<std::int32_t*>(animation + kSplineNumBlocksOffset);
            const auto splineMaxFramesPerBlock = *reinterpret_cast<std::int32_t*>(animation + kSplineMaxFramesPerBlockOffset);
            const auto splineBlockOffsets = *reinterpret_cast<std::uintptr_t*>(animation + kSplineBlockOffsetsOffset);
            const auto splineBlockOffsetsCount = *reinterpret_cast<std::int32_t*>(animation + kSplineBlockOffsetsCountOffset);
            const auto splineFloatBlockOffsets = *reinterpret_cast<std::uintptr_t*>(animation + kSplineFloatBlockOffsetsOffset);
            const auto splineFloatBlockOffsetsCount = *reinterpret_cast<std::int32_t*>(animation + kSplineFloatBlockOffsetsCountOffset);
            const auto splineDataBase = *reinterpret_cast<std::uintptr_t*>(animation + kSplineDataBaseOffset);
            const auto splineDataSize = *reinterpret_cast<std::int32_t*>(animation + kSplineDataSizeOffset);
            if (splineNumBlocks <= 0 || splineMaxFramesPerBlock < 2 ||
                !plausiblePointer(splineBlockOffsets) || splineBlockOffsetsCount < splineNumBlocks ||
                !plausiblePointer(splineFloatBlockOffsets) || splineFloatBlockOffsetsCount < splineNumBlocks ||
                !plausiblePointer(splineDataBase) || splineDataSize < animationTrackCount) {
                return rejected("spline-payload");
            }

            const auto trackToBoneData = *reinterpret_cast<std::uintptr_t*>(binding + kBindingTrackToBoneDataOffset);
            const auto trackToBoneCount = *reinterpret_cast<std::int32_t*>(binding + kBindingTrackToBoneCountOffset);
            const auto boneCount = *reinterpret_cast<std::int32_t*>(skeleton + kSkeletonBonesCountOffset);
            if (boneCount <= 0 || boneCount > kMaxPlausibleBoneCount) {
                return rejected("skeleton-bone-count");
            }

            /*
             * Resolve which bone each transform track drives, in priority:
             * an explicit track-to-bone map; the flattened bone ranges of
             * the clip's skeleton partitions (weapon clips ship this way:
             * empty map, one partition - identity over the FULL rig would
             * scatter the tracks onto wrong bones); identity, valid only
             * when the clip covers the whole rig.
             */
            std::array<std::int16_t, kSampleBufferTracks> trackBones{};
            std::int32_t mappedTrackCount = 0;
            const auto partitionIndicesData = *reinterpret_cast<std::uintptr_t*>(binding + kBindingPartitionIndicesOffset);
            const auto partitionCount = *reinterpret_cast<std::int32_t*>(binding + kBindingPartitionCountOffset);
            if (trackToBoneCount > 0) {
                if (!plausiblePointer(trackToBoneData) || trackToBoneCount > kMaxPlausibleTrackCount) {
                    return rejected("explicit-track-map");
                }
                const auto* map = reinterpret_cast<const std::int16_t*>(trackToBoneData);
                const auto count = (std::min)(trackToBoneCount, static_cast<std::int32_t>(trackBones.size()));
                for (std::int32_t i = 0; i < count; ++i) {
                    trackBones[static_cast<std::size_t>(i)] = map[i];
                }
                mappedTrackCount = count;
            } else if (partitionCount > 0) {
                const auto skeletonPartitionsData = *reinterpret_cast<std::uintptr_t*>(skeleton + kSkeletonPartitionsDataOffset);
                const auto skeletonPartitionCount = *reinterpret_cast<std::int32_t*>(skeleton + kSkeletonPartitionsCountOffset);
                if (!plausiblePointer(partitionIndicesData) || partitionCount > kMaxPlausibleTrackCount ||
                    !plausiblePointer(skeletonPartitionsData) || skeletonPartitionCount <= 0 ||
                    skeletonPartitionCount > kMaxPlausibleBoneCount) {
                    return rejected("partition-arrays");
                }
                const auto* partitionIndices = reinterpret_cast<const std::uint16_t*>(partitionIndicesData);
                for (std::int32_t p = 0; p < partitionCount && mappedTrackCount < static_cast<std::int32_t>(trackBones.size()); ++p) {
                    const auto partitionIndex = static_cast<std::int32_t>(partitionIndices[p]);
                    if (partitionIndex >= skeletonPartitionCount) {
                        continue;
                    }
                    const auto entry = skeletonPartitionsData +
                                       static_cast<std::uintptr_t>(partitionIndex) * kSkeletonPartitionStride;
                    const auto startBone = *reinterpret_cast<const std::int16_t*>(entry + kPartitionStartBoneOffset);
                    const auto numBones = *reinterpret_cast<const std::int16_t*>(entry + kPartitionNumBonesOffset);
                    if (startBone < 0 || numBones <= 0 || startBone + numBones > boneCount) {
                        continue;
                    }
                    for (std::int16_t b = 0; b < numBones && mappedTrackCount < static_cast<std::int32_t>(trackBones.size()); ++b) {
                        trackBones[static_cast<std::size_t>(mappedTrackCount++)] = static_cast<std::int16_t>(startBone + b);
                    }
                }
                if (mappedTrackCount == 0) {
                    return rejected("empty-partition-map");
                }
            } else if (animationTrackCount == boneCount) {
                const auto count = (std::min)(animationTrackCount, static_cast<std::int32_t>(trackBones.size()));
                for (std::int32_t i = 0; i < count; ++i) {
                    trackBones[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(i);
                }
                mappedTrackCount = count;
            } else {
                // No map, no partitions, and the track count does not cover
                // the rig - identity would misattribute every track.
                return rejected("unmapped-track-count-mismatch");
            }

            return traceUnblendedHands(binding, skeleton, animation, duration,
                boneCount, animationTrackCount, trackBones, mappedTrackCount, clipAnimationName);
        }

        struct GraphArrayView
        {
            std::uintptr_t base{ 0 };
            std::uint32_t capacity{ 0 };
        };

        // Every graph slot is visited, gated per slot by the
        // BShkbAnimationGraph vtable (in-game confirmed module offset) so a
        // stale capacity slot degrades into a skip.
        constexpr std::uintptr_t kGraphVtableModuleOffset = 0x2E00A48;

        bool resolveGraphArray(std::uintptr_t manager, GraphArrayView& out)
        {
            if (!plausiblePointer(manager)) {
                return false;
            }
            const auto capacityAndFlags = *reinterpret_cast<std::uint32_t*>(manager + kManagerGraphsCapacityOffset);
            const auto storageAddress = manager + kManagerGraphsStorageOffset;
            const auto graphsBase = (capacityAndFlags & kGraphsInlineStorageFlag) != 0
                ? storageAddress
                : *reinterpret_cast<std::uintptr_t*>(storageAddress);
            if (!plausiblePointer(graphsBase)) {
                return false;
            }
            out.base = graphsBase;
            out.capacity = (std::min)(capacityAndFlags & ~kGraphsInlineStorageFlag, kMaxGraphs);
            return true;
        }

        // Vtable-gated graphs-array entry; 0 when the slot is empty, stale,
        // or not a BShkbAnimationGraph.
        [[nodiscard]] std::uintptr_t graphAtIndex(const GraphArrayView& graphs, std::uint32_t index)
        {
            if (index >= graphs.capacity) {
                return 0;
            }
            const auto graph = reinterpret_cast<const std::uintptr_t*>(graphs.base)[index];
            if (!plausiblePointer(graph) || objectVtableRel(graph) != kGraphVtableModuleOffset) {
                return 0;
            }
            return graph;
        }

        [[nodiscard]] const void* managerFromWeaponHolder(const void* weaponGraphHolder)
        {
            const auto holder = reinterpret_cast<std::uintptr_t>(weaponGraphHolder);
            if (!plausiblePointer(holder)) {
                return nullptr;
            }
            const auto manager = *reinterpret_cast<std::uintptr_t*>(holder + kHolderManagerOffset);
            return plausiblePointer(manager) ? reinterpret_cast<const void*>(manager) : nullptr;
        }

        using ClipGeneratorActivateFn = void (*)(void*, void*);
        ClipGeneratorActivateFn s_originalClipActivate = nullptr;
        using ClipGeneratorDeactivateFn = void (*)(void*, void*);
        ClipGeneratorDeactivateFn s_originalClipDeactivate = nullptr;

        void traceHandActionActivation(const std::uintptr_t clip, const std::uintptr_t context)
        {
            auto& trace = s_handActionTrace;
            if (!trace.log || trace.faulted || trace.eventsRemaining == 0) return;
            /*
             * The second argument is a stack-built hkbContext, not the
             * character. The owning hkbCharacter is one of its leading pointer
             * members, so the first few qwords are identity-matched against
             * the traced characters; only the match is dereferenced.
             */
            std::uintptr_t character = 0;
            for (std::size_t i = 0; i < 4 && !character; ++i) {
                const auto candidate = reinterpret_cast<const std::uintptr_t*>(context)[i];
                for (const auto target : trace.characters) {
                    if (target != 0 && target == candidate) { character = candidate; break; }
                }
            }
            if (!character) {
                if (trace.unmatchedContextsLogged < 3) {
                    ++trace.unmatchedContextsLogged;
                    const auto* slots = reinterpret_cast<const std::uintptr_t*>(context);
                    trace.log->debug("HAND_ACTION unmatched weapon={:08X} clip={:X} context=[{:X},{:X},{:X},{:X}]",
                        trace.formId, clip, slots[0], slots[1], slots[2], slots[3]);
                }
                return;
            }
            ++trace.activationMatches;
            --trace.eventsRemaining;
            std::array<char, kClipNameCapacity> name{};
            copyHkStringPtr(*reinterpret_cast<const std::uintptr_t*>(clip + kClipGeneratorAnimationNameOffset),
                name.data(), name.size());
            reload_selection_trace::observeClip(name.data());
            const auto wrapper = *reinterpret_cast<const std::uintptr_t*>(clip + kClipGeneratorLoadedBindingOffset);
            if (!plausiblePointer(wrapper)) {
                trace.log->debug("HAND_ACTION unavailable weapon={:08X} stage=control clip='{}'", trace.formId, name.data());
                return;
            }
            const auto binding = *reinterpret_cast<const std::uintptr_t*>(wrapper + kLoadedBindingWrapperBindingOffset);
            trace.log->debug("HAND_ACTION activate weapon={:08X}/{:016X} character={:X} clip={:X} binding={:X} name='{}' t={:.6f} crop=({:.6f},{:.6f})",
                trace.formId, trace.generation, character, clip, binding, name.data(),
                *reinterpret_cast<const float*>(clip + kClipGeneratorLocalTimeOffset),
                *reinterpret_cast<const float*>(clip + kClipGeneratorCropStartOffset),
                *reinterpret_cast<const float*>(clip + kClipGeneratorCropEndOffset));
            bool tracked = false;
            for (auto& active : trace.activeClips) {
                if (active == 0 || active == clip) { active = clip; tracked = true; break; }
            }
            if (!tracked) trace.log->warn("HAND_ACTION active clip capacity reached weapon={:08X}", trace.formId);
            if (!plausiblePointer(binding)) return;
            if (trace.samplesUsed == trace.sampledBindings.size()) return;
            const auto setup = *reinterpret_cast<const std::uintptr_t*>(character + kCharacterSetupOffset);
            if (!plausiblePointer(setup)) {
                trace.log->debug("HAND_ACTION unavailable weapon={:08X} stage=setup", trace.formId);
                return;
            }
            const auto skeleton = *reinterpret_cast<const std::uintptr_t*>(setup + kSetupAnimationSkeletonOffset);
            if (!plausiblePointer(skeleton)) {
                trace.log->debug("HAND_ACTION unavailable weapon={:08X} stage=skeleton", trace.formId);
                return;
            }
            for (const auto& previous : trace.sampledBindings) {
                if (previous.binding == binding && previous.skeleton == skeleton) return;
            }
            trace.sampledBindings[trace.samplesUsed++] = { binding, skeleton };
            if (trace.samplesUsed == trace.sampledBindings.size()) {
                trace.log->debug("HAND_ACTION definition budget reached weapon={:08X}; later clips retain lifecycle events only", trace.formId);
            }
            traceHandMappingDefinition(binding, skeleton, name.data());
            // The clip's own hkbBehaviorGraph rides at hkbContext+0x10 -
            // needed to turn graph-local trigger eventIds into authored names.
            // Exact-vtable gate: anything else degrades into nameless trigger
            // lines, never a bad dereference.
            auto behavior = *reinterpret_cast<const std::uintptr_t*>(context + kContextBehaviorGraphOffset);
            if (!plausiblePointer(behavior) || *reinterpret_cast<const std::uintptr_t*>(behavior) !=
                REL::Module::get().base() + kBehaviorGraphVtableModuleOffset) behavior = 0;
            logClipMarkers(clip, binding, name.data(), behavior);
            const bool sampled = traceBindingHands(binding, skeleton, name.data());
            trace.log->debug("HAND_ACTION sampled weapon={:08X}/{:016X} binding={:X} skeleton={:X} complete={} definitions={}/{}",
                trace.formId, trace.generation, binding, skeleton, sampled, trace.samplesUsed, trace.sampledBindings.size());
        }

        bool guardedTraceHandActionActivation(const std::uintptr_t clip, const std::uintptr_t context)
        {
            __try {
                traceHandActionActivation(clip, context);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        /*
         * Runs on the engine's graph-update thread right after the original
         * hkbClipGenerator::activate completed, i.e. while the animation
         * control at clipGenerator+0xD0 (and its binding at control+0x38) is
         * guaranteed resident. A native read fault disables the trace for
         * this target instead of reaching the engine.
         */
        void clipGeneratorActivateShim(void* clipGeneratorRaw, void* contextRaw)
        {
            if (s_originalClipActivate) {
                s_originalClipActivate(clipGeneratorRaw, contextRaw);
            }
            s_hookCalls.fetch_add(1, std::memory_order_relaxed);
            const auto clipGenerator = reinterpret_cast<std::uintptr_t>(clipGeneratorRaw);
            const auto context = reinterpret_cast<std::uintptr_t>(contextRaw);
            if (!plausiblePointer(clipGenerator) || !plausiblePointer(context)) {
                return;
            }

            std::scoped_lock lock(s_hookMutex);
            bool traced = false;
            try {
                traced = guardedTraceHandActionActivation(clipGenerator, context);
            } catch (...) {
                traced = false;
            }
            if (!traced) {
                s_handActionTrace.faulted = true;
                if (s_handActionTrace.log) {
                    s_handActionTrace.log->error("HAND_ACTION disabled after native read/sample fault weapon={:08X}", s_handActionTrace.formId);
                }
            }
        }

        void clipGeneratorDeactivateShim(void* clipGeneratorRaw, void* context)
        {
            const auto clipGenerator = reinterpret_cast<std::uintptr_t>(clipGeneratorRaw);
            {
                std::scoped_lock lock(s_hookMutex);
                for (auto& clip : s_handActionTrace.activeClips) {
                    if (clip != clipGenerator) continue;
                    clip = 0;
                    if (s_handActionTrace.log && s_handActionTrace.eventsRemaining > 0) {
                        --s_handActionTrace.eventsRemaining;
                        float localTime = 0.0f;
                        const bool validTime = native_memory::tryReadField(
                            clipGeneratorRaw, kClipGeneratorLocalTimeOffset, localTime);
                        s_handActionTrace.log->debug("HAND_ACTION deactivate weapon={:08X}/{:016X} clip={:X} t={:.6f} validTime={}",
                            s_handActionTrace.formId, s_handActionTrace.generation, clipGenerator, localTime, validTime);
                    }
                    break;
                }
            }
            if (s_originalClipDeactivate) {
                s_originalClipDeactivate(clipGeneratorRaw, context);
            }
        }

        bool ensureHooksInstalled()
        {
            static bool s_installAttempted = false;
            static bool s_installed = false;
            if (s_installed) {
                return true;
            }
            if (s_installAttempted) {
                return false;
            }
            s_installAttempted = true;
            if (!REL::Module::IsVR() ||
                REL::Module::get().version() != F4SE::RUNTIME_VR_1_2_72) {
                PAPER_LOG_ERROR(
                    Init,
                    "Hand-action trace clip hooks require verified Fallout4VR.exe 1.2.72 layouts");
                return false;
            }
            const auto vtableBase = REL::Module::get().base() + kClipGeneratorVtableModuleOffset;
            // 8-byte aligned pointer stores are atomic on x64, so concurrent
            // graph updates dispatching through the slots stay safe during the
            // swaps. Activate traces the clip and deactivate ends it.
            const auto activateSlot = vtableBase + kClipGeneratorActivateSlotOffset;
            const auto deactivateSlot = vtableBase + kClipGeneratorDeactivateSlotOffset;
            // Each slot holds the native function or another plugin's; the shims call
            // through whichever is there.
            const auto base = REL::Module::get().base();
            const auto activateResolved =
                RPS::Runtime::HookChain::resolveSlot(activateSlot, base + kClipGeneratorActivateFunctionOffset);
            const auto deactivateResolved =
                RPS::Runtime::HookChain::resolveSlot(deactivateSlot, base + kClipGeneratorDeactivateFunctionOffset);
            for (const auto* resolved : { &activateResolved, &deactivateResolved }) {
                if (resolved->foreign()) {
                    PAPER_LOG_INFO(Init, "Hand-action trace: a clip generator slot is already replaced by {}; PAPER calls through it",
                        resolved->owner);
                }
            }
            const std::uintptr_t activateFunction = activateResolved.callee;
            const std::uintptr_t deactivateFunction = deactivateResolved.callee;
            if (!activateResolved || !deactivateResolved) {
                PAPER_LOG_ERROR(
                    Init,
                    "Hand-action trace hook validation failed; clip generator vtable entries do not match Fallout4VR.exe 1.2.72");
                return false;
            }
            s_originalClipActivate = reinterpret_cast<ClipGeneratorActivateFn>(
                activateFunction);
            REL::safe_write(activateSlot, reinterpret_cast<std::uintptr_t>(&clipGeneratorActivateShim));
            s_originalClipDeactivate = reinterpret_cast<ClipGeneratorDeactivateFn>(
                deactivateFunction);
            REL::safe_write(deactivateSlot, reinterpret_cast<std::uintptr_t>(&clipGeneratorDeactivateShim));
            std::uintptr_t installedActivate = 0;
            std::uintptr_t installedDeactivate = 0;
            if (!native_memory::tryReadValue(
                    reinterpret_cast<const std::uintptr_t*>(activateSlot),
                    installedActivate) ||
                !native_memory::tryReadValue(
                    reinterpret_cast<const std::uintptr_t*>(deactivateSlot),
                    installedDeactivate) ||
                installedActivate != reinterpret_cast<std::uintptr_t>(
                    &clipGeneratorActivateShim) ||
                installedDeactivate != reinterpret_cast<std::uintptr_t>(
                    &clipGeneratorDeactivateShim)) {
                REL::safe_write(activateSlot, activateFunction);
                REL::safe_write(deactivateSlot, deactivateFunction);
                s_originalClipActivate = nullptr;
                s_originalClipDeactivate = nullptr;
                PAPER_LOG_ERROR(
                    Init,
                    "Hand-action trace hook installation did not verify; original vtable entries restored");
                return false;
            }
            s_installed = true;
            PAPER_LOG_INFO(Weapon,
                "Hand-action trace clip hooks installed (activate +{:#x}, deactivate +{:#x})",
                moduleRelative(reinterpret_cast<std::uintptr_t>(s_originalClipActivate)),
                moduleRelative(reinterpret_cast<std::uintptr_t>(s_originalClipDeactivate)));
            return true;
        }

        bool readHandTraceCharacters(const std::uint32_t formId, const void* actorManager,
            std::array<std::uintptr_t, kMaxHookCharacters>& characters)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* biped = player ? player->GetBiped(true).get() : nullptr;
            std::size_t count = 0;
            const auto appendManager = [&](const void* manager) {
                GraphArrayView graphs{};
                if (!resolveGraphArray(reinterpret_cast<std::uintptr_t>(manager), graphs)) return;
                for (std::uint32_t i = 0; i < graphs.capacity && count < characters.size(); ++i) {
                    const auto graph = graphAtIndex(graphs, i);
                    if (graph == 0) continue;
                    const auto character = graph + kGraphCharacterOffset;
                    if (std::find(characters.begin(), characters.begin() + count, character) == characters.begin() + count) {
                        characters[count++] = character;
                    }
                }
            };
            // The player graph owns playing arm clips. Equipped biped holders
            // alone can expose a resource graph that never activates them.
            appendManager(actorManager);
            for (auto slot = static_cast<std::uint32_t>(RE::BIPED_OBJECT::kWeaponHand);
                 biped && slot < static_cast<std::uint32_t>(RE::BIPED_OBJECT::kTotal); ++slot) {
                const auto& object = biped->object[slot];
                if (!object.parent.object || object.parent.object->GetFormID() != formId ||
                    !object.objectGraphManager) continue;
                appendManager(managerFromWeaponHolder(object.objectGraphManager.get()));
            }
            return count != 0;
        }

        bool guardedReadHandTraceCharacters(const std::uint32_t formId, const void* actorManager,
            std::array<std::uintptr_t, kMaxHookCharacters>& characters)
        {
            __try {
                return readHandTraceCharacters(formId, actorManager, characters);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void logHandTraceSummary(const char* reason)
        {
            const auto& trace = s_handActionTrace;
            if (!trace.log) return;
            trace.log->debug("HAND_ACTION summary reason={} weapon={:08X}/{:016X} hookCalls={} activationMatches={} definitions={} remainingEvents={} faulted={}",
                reason, trace.formId, trace.generation,
                s_hookCalls.load(std::memory_order_relaxed) - trace.hookCallsAtTarget,
                trace.activationMatches, trace.samplesUsed, trace.eventsRemaining, trace.faulted);
        }
    }

    void refreshHandActionTrace(const std::uint32_t weaponFormId,
        const std::uint64_t weaponGenerationKey, const std::uintptr_t weaponNode,
        const std::shared_ptr<spdlog::logger>& log)
    {
        std::array<std::uintptr_t, kMaxHookCharacters> characters{};
        const bool requested = log && weaponFormId != 0 && weaponNode != 0;
        // Own the acquired reference outside the guarded raw graph walk so a
        // failed read still releases it through the same RAII path.
        native_animation_graph::AcquiredGraphManager actorManager{
            requested ? RE::PlayerCharacter::GetSingleton() : nullptr };
        const bool available = requested && guardedReadHandTraceCharacters(weaponFormId, actorManager.get(), characters);
        if (!available) characters = {};
        const bool hooksReady = available && ensureHooksInstalled();
        std::scoped_lock lock(s_hookMutex);
        auto& trace = s_handActionTrace;
        if (!requested) { logHandTraceSummary("cleared"); trace = {}; return; }
        if (trace.formId == weaponFormId && trace.generation == weaponGenerationKey &&
            trace.weaponNode == weaponNode && trace.characters == characters && trace.log == log) {
            if (++trace.framesSinceSummary >= 180) {
                logHandTraceSummary("periodic");
                trace.framesSinceSummary = 0;
            }
            return;
        }
        logHandTraceSummary("target-changed");
        trace = {};
        trace.log = log;
        trace.formId = weaponFormId;
        trace.generation = weaponGenerationKey;
        trace.weaponNode = weaponNode;
        trace.characters = characters;
        trace.faulted = !hooksReady;
        trace.hookCallsAtTarget = s_hookCalls.load(std::memory_order_relaxed);
        s_markerLogBudget = kMarkerLogBudgetPerGeneration;
        trace.log->debug("HAND_ACTION target weapon={:08X}/{:016X} scene={:X} characters={} hooks={} maxDefinitions=32 samplesPerDefinition=33 maxLifecycleEvents=512 mappingVersion=1 maxMappedRigs=8 maxMappedBones=256 maxMapEntries=512",
            weaponFormId, weaponGenerationKey, weaponNode,
            std::count_if(characters.begin(), characters.end(), [](const auto value) { return value != 0; }), hooksReady);
        for (std::size_t i = 0; i < characters.size(); ++i) {
            if (characters[i] != 0) trace.log->debug("HAND_ACTION character weapon={:08X} slot={} identity={:X}", weaponFormId, i, characters[i]);
        }
    }
}
