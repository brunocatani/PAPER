param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$failures = [System.Collections.Generic.List[string]]::new()

function Require-Text {
    param([string]$Path, [string]$Pattern, [string]$Message)
    $text = Get-Content -Raw -LiteralPath (Join-Path $Root $Path)
    if ($text -notmatch $Pattern) {
        $failures.Add($Message)
    }
}

function Reject-Text {
    param([string]$Path, [string]$Pattern, [string]$Message)
    $text = Get-Content -Raw -LiteralPath (Join-Path $Root $Path)
    if ($text -match $Pattern) {
        $failures.Add($Message)
    }
}

$directGraphHookFiles = @(
    'src/native/EntryTrampolineHook.cpp',
    'src/native/EntryTrampolineHook.h'
)
foreach ($relativePath in $directGraphHookFiles) {
    if (Test-Path -LiteralPath (Join-Path $Root $relativePath)) {
        $failures.Add("Paper must consume ROCK's graph-output phase instead of retaining '$relativePath'.")
    }
}

Require-Text 'src/PaperMain.cpp' 'NativeGraphOutput[\s\S]*publishRockAuthority\(runtimeOperational\)[\s\S]*captureNativeGraphOutput\(\)' 'Paper must publish authority and capture at ROCK''s native graph-output phase.'
Require-Text 'src/PaperMain.cpp' 'BeforeRock[\s\S]*installEventHooks\(\)[\s\S]*beginRockFrame[\s\S]*ApplyPhase::BeforeRock[\s\S]*AfterRock[\s\S]*ApplyPhase::AfterRock[\s\S]*Complete' 'Paper must own lifecycle hooks and both reload-pose application phases.'
Require-Text 'src/PaperMain.cpp' 'AfterRock[\s\S]*ApplyPhase::AfterRock[\s\S]*debug_visualization::publish\(\*context, s_gripState\)' 'Paper diagnostics must publish after its final native pose application.'
Require-Text 'src/PaperMain.cpp' 'resetSession\(\)[\s\S]*debug_visualization::clear\(\)' 'Session teardown must clear Paper-owned overlay publications.'
Require-Text 'src/PaperMain.cpp' 'kPreLoadGame[\s\S]{0,160}s_gameLoaded\.store\(false[\s\S]{0,120}resetSession\(\)[\s\S]*kPostLoadGame[\s\S]{0,500}s_gameLoaded\.store\(true[\s\S]{0,120}connectRock\(\)' 'PAPER must close and reopen its frame callback gate across game-session load boundaries.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'installReloadStateChangeHook\(\)[\s\S]*installWeaponFireHook\(\)[\s\S]*captureNativeGraphOutput\(\)' 'Reload, manual-cycle fire, and native graph capture behavior must remain in Paper.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'onWeaponFire[\s\S]*activityOrderAtEvent[\s\S]*s_playerWeaponFireActivityOrderAtEvent\.store[\s\S]*s_playerWeaponFireSequence\.fetch_add[\s\S]*!s_runtimeEnabled' 'Every handled player fire must publish its exact event sequence and live activity identity before manual-cycle eligibility gates.'
Require-Text 'src/native/NativeOffsets.h' 'kFunc_ReadyWeaponHandler_ShouldHandleEvent\s*=\s*0x0FCE650[\s\S]*kVtableEntry_ReadyWeaponHandler_ShouldHandleEvent\s*=\s*0x2D8A480' 'The Tactical Reload bridge must retain the blindly verified FO4VR ReadyWeapon predicate target and vtable slot.'
Require-Text 'src/native/NativeOffsets.h' 'kFunc_PlayerControls_DoAction\s*=\s*0x0FC07E0[\s\S]*kSignature_PlayerControls_AutomaticReload\s*=\s*0x0FC0D18[\s\S]*kCallsite_PlayerControls_AutomaticReload\s*=\s*0x0FC0D4F' 'Manual-reload-only must retain the independently verified dispatcher, empty-ammo signature, and automatic producer callsite.'
Require-Text 'src/reload_control/ManualReloadOnly.cpp' 'kAutomaticReloadProducerSignature[\s\S]*0x8D, 0x50, 0x6C[\s\S]*0x44, 0x8D, 0x40, 0x02[\s\S]*matchesAutomaticReloadProducerSignature[\s\S]*REL::Module::IsVR\(\)[\s\S]*REL::Module::get\(\)\.version\(\)\s*!=\s*F4SE::RUNTIME_VR_1_2_72[\s\S]*kCallsite_PlayerControls_AutomaticReload[\s\S]*decodedTarget\s*!=\s*expectedDispatcher\.address\(\)[\s\S]*write_call<5>\(' 'The automatic reload hook must fail closed on the exact FO4VR executable, empty-ammo producer signature, and dispatcher target before patching only the verified callsite.'
Require-Text 'src/reload_control/ManualReloadOnly.cpp' 'onAutomaticReloadRequest[\s\S]*s_runtimeEnabled\.load[\s\S]*return false[\s\S]*original\(dispatcher, actionId, priority\)' 'The automatic reload wrapper must suppress only while enabled and otherwise preserve the original dispatcher call.'
Reject-Text 'src/reload_control/ManualReloadOnly.cpp' '0x0FC94AE|0x0FC97D4|0x0F29E49|ReadyWeaponHandler' 'The empty-ammo hook must not patch or filter manual ReadyWeapon or camera reload-resume producers.'
Require-Text 'src/PaperConfig.h' 'manualReloadOnly\s*\{\s*true\s*\}' 'Manual-reload-only must use an enabled compiled default so existing production INIs gain the requested behavior without replacement.'
Require-Text 'src/PaperConfig.cpp' 'GetBoolValue\([\s\S]*"Reload"[\s\S]*"bManualReloadOnly"[\s\S]*manualReloadOnly' 'Paper must load the manual-reload-only toggle from the Reload INI section.'
Require-Text 'src/PaperMain.cpp' 'configureManualReloadOnlyForSession[\s\S]*manual_reload_only::installHook\(\)[\s\S]*manual_reload_only::setRuntimeEnabled\(requested && hookReady\)[\s\S]*kGameLoaded[\s\S]*configureManualReloadOnlyForSession\(\)[\s\S]*kPostLoadGame[\s\S]*configureManualReloadOnlyForSession\(\)' 'Manual-reload-only must install and apply its INI state independently at each game-session boundary.'
Require-Text 'src/PaperMain.cpp' 'F4SE::Init\(f4se, false\)[\s\S]*F4SE::AllocTrampoline\(64\)' 'PAPER must allocate its bounded branch trampoline before the automatic reload hook can install.'
Require-Text 'src/compat/TacticalReloadBridge.cpp' 'kReadyWeaponShouldHandleSignature[\s\S]*matchesReadyWeaponShouldHandleSignature[\s\S]*REL::Module::IsVR\(\)[\s\S]*REL::Module::get\(\)\.version\(\)\s*!=[\s\S]*F4SE::RUNTIME_VR_1_2_72[\s\S]*expectedTarget[\s\S]*\*slot\s*!=\s*expectedTarget\.address\(\)[\s\S]*VirtualProtect[\s\S]*FlushInstructionCache' 'The Tactical Reload ReadyWeapon hook must fail closed on executable identity, exact vtable target, and a masked live instruction signature before patching.'
Require-Text 'src/compat/TacticalReloadBridge.cpp' 'const bool accepted\s*=\s*original\(handler,\s*event\)[\s\S]*if\s*\(!accepted[\s\S]*event->As<RE::ButtonEvent>[\s\S]*processAcceptedButtonEvent[\s\S]*return accepted' 'The Tactical Reload bridge must call the original accepted-input predicate exactly once and preserve its result.'
Require-Text 'src/compat/TacticalReloadBridge.cpp' 'TacticalReload\.esm[\s\S]*0x001734[\s\S]*0x001ECF[\s\S]*0x001ED3[\s\S]*0x001ED7[\s\S]*LookupForm<RE::BGSKeyword>[\s\S]*supported TacticalReload\.esm keyword contract is absent or malformed' 'The compatibility bridge must select only the exact supported shipped Tactical Reload keyword contract and fail closed otherwise.'
Require-Text 'src/compat/TacticalReloadBridge.cpp' 'kActorStateStorageOffset[\s\S]*formType\s*!=\s*RE::ENUM_FORM_ID::kWEAP[\s\S]*isWeaponReadyForManualReload[\s\S]*decodeWeaponState' 'Manual intent must require an equipped weapon and use the verified FO4VR actor-state storage rather than CommonLibF4VR''s shifted named bitfields.'
Reject-Text 'src/compat/TacticalReloadBridge.cpp' '(?:->|\.)weaponState\b|(?:->|\.)gunState\b|GetWeaponMagicDrawn\s*\(' 'The Tactical Reload bridge must not read CommonLibF4VR weaponState, gunState, or GetWeaponMagicDrawn because those named bitfields are shifted on FO4VR 1.2.72.'
Require-Text 'src/compat/TacticalReloadBridgePolicy.h' 'keywordPresentAtArm[\s\S]*keywordAddedByPaper[\s\S]*transactionOwnsKeywordCleanup[\s\S]*ReloadStartExpired[\s\S]*RuntimeDisabled[\s\S]*SessionReset' 'The Tactical Reload transaction must track duplicate Papyrus intent and own bounded cleanup across timeout, disable, and session-reset paths.'
Require-Text 'src/compat/TacticalReloadBridge.cpp' 's_cleanSessionBaseline[\s\S]*establishCleanSessionBaseline\(\)[\s\S]*isReloading[\s\S]*Removed stale transient ManualReload keyword[\s\S]*s_cleanSessionBaseline\.load' 'The bridge must establish a save-safe clean ManualReload baseline and defer activation while a loaded native reload is active.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 's_playerReloadStartSequence\.fetch_add[\s\S]*tactical_reload_bridge::notifyPlayerReloadStart\(\)[\s\S]*s_playerReloadEndSequence\.fetch_add[\s\S]*tactical_reload_bridge::notifyPlayerReloadEnd\(\)' 'The bridge must consume PAPER''s existing native reload lifecycle rather than install a duplicate reload-state hook.'
Require-Text 'src/PaperMain.cpp' 'configureRuntime[\s\S]*tactical_reload_bridge::setRuntimeEnabled\(operational\)[\s\S]*BeforeRock[\s\S]*installEventHooks\(\)[\s\S]*tactical_reload_bridge::installInputHook\(\)[\s\S]*tactical_reload_bridge::beginFrame\(context->deltaSeconds\)[\s\S]*resetSession\(\)[\s\S]*tactical_reload_bridge::resetSession\(\)[\s\S]*kGameLoaded[\s\S]*tactical_reload_bridge::initializeSession\(\)' 'PAPER must install, advance, disable, reset, and reinitialize the Tactical Reload bridge on its existing game-thread lifecycle.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'onWeaponFire[\s\S]*s_manualCycleHandAnimationEligible[\s\S]*currentPlayerWeaponInstanceData[\s\S]*manualCycleWatchdogSeconds[\s\S]*s_localManualCycleTestRequestSequence' 'Handled player fire must admit native hand animation by compatible authored-hand topology without weapon-family classification.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'ManualCycleWeaponEligibility|isManualCycleFireAnimationAllowed|usesManualCycleAnimationKeyword|usesRevolverFireAnimation|WEAPON_FLAGS::kBoltAction' 'Fire-cycle hand animation must not depend on weapon-family or animation-keyword semantics.'
Reject-Text 'src/PaperMain.cpp' 'refreshManualCycleWeaponActionEvidence|ManualCycleWeaponActionEvidence|copyWeaponEvidenceDetails|RockProviderWeaponActionRoleV1::Pump|RockProviderWeaponActionRoleV1::Lever' 'Authored-hand animation admission must not scan semantic weapon-part evidence.'
Reject-Text 'src/PaperMain.cpp' 'TwoHandGripActive|WeaponTransformOwned' 'Manual-cycle hand animation must not require ROCK two-hand or weapon-transform ownership.'
Reject-Text 'src/animation/NativeAnimationAuthorityPolicy.h' 'FiringGripOccupied' 'Full native-animation compatibility must not require a ROCK manual firing grip during ordinary right-hand native carry.'
Require-Text 'src/animation/NativeAnimationAuthorityPolicy.h' 'advanceNativeAnimationCompatibility[\s\S]*animationAuthorityRequested[\s\S]*handlingStateValid[\s\S]*firingHandIsLeft[\s\S]*partCarryActive[\s\S]*animationRequestResetRequired[\s\S]*weaponPresent[\s\S]*weaponFormId[\s\S]*weaponGenerationKey[\s\S]*weaponIdentityCoherent[\s\S]*state\.weaponBound' 'PAPER must fail closed for incompatible handling topology, require a canceled request to reset, and bind each active animation-authority session to one concrete equipped weapon generation.'
Require-Text 'src/PaperMain.cpp' 'refreshWeaponState[\s\S]*queryEquippedWeaponHandlingState[\s\S]*currentFiringHand[\s\S]*PartCarryActive[\s\S]*WeaponPresent[\s\S]*weaponIdentityCoherent' 'PAPER compatibility must consume ROCK''s authoritative firing-hand, PartCarry, weapon-presence, and identity state.'
Require-Text 'src/PaperMain.cpp' 'currentPaperAnimationAuthority[\s\S]*currentLocalAuthorityFlags[\s\S]*currentConsumerAuthorityFlags[\s\S]*authority\.requestedFlags\(\)\s*!=\s*0' 'PAPER must apply the same weapon-bound compatibility gate to built-in and public-consumer animation authority.'
Require-Text 'src/PaperMain.cpp' 'BeforeRock[\s\S]*before-rock/pre-lifecycle[\s\S]*configureRuntime\(runtimeOperational\)[\s\S]*beginRockFrame[\s\S]*before-rock/post-lifecycle[\s\S]*!postLifecycleCompatibility\.compatible\(\)[\s\S]*configureRuntime\(false\)[\s\S]*publishRockAuthorityFlags\(0\)' 'BeforeRock must validate both the preceding lease and any newly armed local animation before publishing or applying pose authority.'
Require-Text 'src/PaperMain.cpp' 'AfterRock[\s\S]*evaluateAnimationCompatibility[\s\S]*!compatibility\.compatible\(\)[\s\S]*configureRuntime\(false\)[\s\S]*publishRockAuthorityFlags\(0\)[\s\S]*ApplyPhase::AfterRock' 'AfterRock must cancel an active session before final pose application when physical weapon state changes mid-frame.'
Require-Text 'src/PaperMain.cpp' 'Weapon-animation compatibility[\s\S]*externalHandlingAuthority[\s\S]*owner=\{:016X\}[\s\S]*authorityFlags[\s\S]*runtimeFlags[\s\S]*localFlags[\s\S]*consumerFlags[\s\S]*weapon=\{:08X\}/\{:016X\}' 'Compatibility diagnostics must identify live lease ownership, handling flags, local/consumer animation authority, and equipped weapon identity.'
Reject-Text 'src/PaperMain.cpp' 'GetModuleHandleA|RIW\.dll|ROCK_Immersive_Weapons' 'PAPER compatibility must depend on ROCK runtime state, not hard-coded addon module detection.'
Require-Text 'src/PaperMain.cpp' 'leftSupportCandidate[\s\S]*isSupportGripKind\(leftPartGrip\.gripKind\)[\s\S]*WeaponRootLocal[\s\S]*leftSupportGripValid\s*=\s*true[\s\S]*authoredLeftSupportGripActive\s*=[\s\S]*leftPartGrip\.authoredSupportGrip\s*!=\s*0' 'Manual-cycle support-hand animation must consume ROCK''s explicit authored-grip provenance while preserving weapon-root-local physical authority.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'shouldPublishWeaponFixedSupportHand\([\s\S]*s_framePartialReloadExpected,[\s\S]*authoredLeftActive[\s\S]*clearManualCycleVisualForHand\([\s\S]*Hand::Left' 'Fire-cycle support-hand publication must retain partial reloads and otherwise require only ROCK''s authored-grab provenance.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'isAuthoredSupportGripMatch|authoredLeftValid|authoredLeftHandInWeapon' 'Manual-cycle authored support admission must not reintroduce transform matching or duplicate authored-pose inference.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'requestChanged[\s\S]*advanceManualCycleAuthoredSupportGripLatch[\s\S]*leaseStarted\s*=\s*true[\s\S]*s_latestManualCycleRockGripBaselines[\s\S]*refreshEffectiveManualCycleRockGripBaselines' 'Manual-cycle support-hand ownership must be latched from the pre-ROCK grip observation at the cycle boundary.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'activeNonAuthoredGripObserved[\s\S]*leftPartGripStateValid[\s\S]*leftPartGripActive[\s\S]*advanceManualCycleAuthoredSupportGripLatch' 'A positively observed non-authored left-hand weapon grip must invalidate the cycle support-hand latch.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'kExpectedPostFrikPrefix|kFunc_PlayerPostUpdateAnimationGraphManager|entry_trampoline_hook' 'Paper must not install a competing PostUpdateAnimationGraphManager detour.'
Reject-Text 'src/native/NativeOffsets.h' 'PostUpdateAnimationGraphManager|UpdateFirstPersonArm' 'Paper native offsets must be limited to its reload/manual-cycle lifecycle ownership.'

Require-Text 'data/config/PAPER.ini' 'bNativeReloadAnimationAuthorityTestEnabled\s*=\s*true[\s\S]*bNativeReloadAnimationPartialAuthorityTestEnabled\s*=\s*true' 'Both migrated reload authority switches must default true in Paper.'
Require-Text 'data/config/PAPER.ini' '\[Debug\][\s\S]*bDebugDrawNativeAnimation\s*=\s*false[\s\S]*bDebugDrawNativeAnimationText\s*=\s*true[\s\S]*fDebugNativeAnimationAxisLength[\s\S]*fDebugNativeAnimationMarkerSize' 'Paper must retain bounded, opt-in native animation visualization controls.'
Reject-Text 'data/config/PAPER.ini' 'AuthoredPrimaryFiringGrip|Offhand|EquippedWeaponGrab' 'ROCK-owned equipped-weapon grip settings must not migrate to Paper.'
Require-Text 'CMakeLists.txt' 'file\(READ\s+"\$\{PAPER_DEFAULT_INI_SOURCE\}"\s+PAPER_DEFAULT_INI_CONTENT\)[\s\S]*PaperDefaultIni\.h\.in' 'Paper must compile its canonical reference INI into the plugin for first-run creation.'
Require-Text 'src/PaperConfig.cpp' 'ensureFileExists\([\s\S]*config_defaults::kIni[\s\S]*ini\.LoadFile\(activePath\.c_str\(\)\)' 'Paper must create a missing production INI before attempting to load it.'

$referenceIni = [System.IO.File]::ReadAllBytes((Join-Path $Root 'data/config/PAPER.ini'))
$deployedIni = [System.IO.File]::ReadAllBytes((Join-Path $Root 'data/mod/PAPER_Config/PAPER.ini'))
if (-not [System.Linq.Enumerable]::SequenceEqual[byte]($referenceIni, $deployedIni)) {
    $failures.Add('Paper reference and deployed default INIs must remain byte-for-byte synchronized.')
}

Require-Text 'src/api/RockApiClient.cpp' 'DebugOverlayPublication[\s\S]*ROCK_PROVIDER_API_V1_NATIVE_ANIMATION_RUNTIME_CLEAR_TABLE_BYTES[\s\S]*supportsDebugOverlayPublicationV1[\s\S]*supportsPresentedHandFramesV1' 'Paper must negotiate ROCK''s complete V1 debug and final presented-hand surfaces.'
Require-Text 'src/api/RockApiClient.cpp' 'PoseReadback[\s\S]*supportsWeaponPartGripStateV1[\s\S]*supportsPoseReadbackV1[\s\S]*getWeaponPartGripStateV1[\s\S]*getSelectedAuthoredGripPoseV1' 'Paper must negotiate and consume ROCK''s V1 part-grip and authored-pose readback surfaces.'
Require-Text 'src/api/RockApiClient.cpp' 'supportsEquippedWeaponHandlingAuthorityV1[\s\S]*queryEquippedWeaponHandlingState[\s\S]*getEquippedWeaponHandlingStateV1' 'Paper must negotiate and consume ROCK''s read-only V1 equipped-weapon handling snapshot.'
Reject-Text 'src/api/RockApiClient.cpp' 'RockProviderConsumerCapabilityV1::\s*EquippedWeaponHandlingAuthority' 'PAPER must not request ownership of ROCK''s equipped-weapon handling lease merely to observe compatibility state.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'kNativeHandColor[\s\S]*kRockGripColor[\s\S]*kResolvedHandColor[\s\S]*kAppliedHandColor' 'Paper must preserve distinct native, ROCK grip, resolved, and applied hand colors.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'queryDebugAuthoritySnapshot[\s\S]*publishDebugOverlay' 'Paper must publish its native authority snapshot through ROCK.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'getPresentedHandFrameV1[\s\S]*RockProviderHandFrameFlagV1::PresentedVisual' 'Applied-hand diagnostics must use ROCK''s final hFRIK-presented frame, not its root-flattened physics authority.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' '!rockApiClient\(\)\.publishDebugOverlay\(publication\)[\s\S]*rockApiClient\(\)\.clearDebugOverlay\(\)' 'A rejected replacement must clear the preceding owner-scoped overlay instead of freezing stale diagnostics.'
Reject-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'VRCompositor|IVRCompositor|Submit\s*\(' 'Paper must consume ROCK''s renderer rather than install a second compositor path.'

Require-Text 'src/PaperMain.cpp' 'REL::Module::IsVR\(\)[\s\S]*REL::Module::get\(\)\.version\(\)[\s\S]*executableVersion\s*!=\s*F4SE::RUNTIME_VR_1_2_72' 'Query must validate VR identity and the exact Fallout4VR.exe 1.2.72 layout domain.'
Reject-Text 'src/PaperMain.cpp' 'RuntimeVersion\(\)\s*(?:==|!=|<=|>=|<|>)' 'QueryInterface::RuntimeVersion must never be used as the executable-version compatibility gate.'
Reject-Text 'src/PaperMain.cpp' 'RUNTIME_LATEST_VR' 'The ambiguous VR executable constant alias must not re-enter PAPER bootstrap.'
Require-Text 'CMakeLists.txt' 'add_custom_target\(PAPERSourceBoundary[\s\S]*PaperSourceTests\.ps1[\s\S]*add_dependencies\(\$\{PROJECT_NAME\}\s+PAPERSourceBoundary\)' 'The normal PAPER build must enforce the loader/source regression boundary.'
Reject-Text 'CMakeLists.txt' 'F4VR-CommonFramework|PAPER_Toolkit' 'PAPER must depend directly on CommonLibF4VR and remain independent of PAPER_Toolkit.'
Require-Text 'src/api/PAPERApi.h' 'game-thread only[\s\S]*getRuntimeStateV1\)\([\s\S]*std::uint64_t ownerToken[\s\S]*copyCapturedTransformsV1\)\([\s\S]*std::uint64_t ownerToken' 'Paper provider queries must be owner-bound and explicitly game-thread-only.'
Require-Text 'src/api/PAPERProvider.cpp' 'PaperConsumerCapabilityV1::RuntimeState[\s\S]*PaperConsumerCapabilityV1::CapturedTransforms[\s\S]*PaperConsumerCapabilityV1::NativeHandPose' 'Paper provider queries must enforce negotiated capabilities.'
Require-Text 'src/api/PAPERApi.h' 'PAPER_API_VERSION\s*=\s*1[\s\S]*PAPER_PROVIDER_API_V1_BASE_TABLE_BYTES[\s\S]*offsetof\(PaperProviderApiV1, getReloadObservationLimitsV1\)[\s\S]*PaperProviderDescriptorV1' 'The additive reload-observation surface must preserve API V1 and advertise its exact table extent.'
Require-Text 'src/api/PAPERApi.h' 'ReloadStageIdentification\s*=\s*1u\s*<<\s*9[\s\S]*PaperReloadStageStateV1[\s\S]*PaperReloadStagePartV1[\s\S]*getReloadStageStateV1[\s\S]*copyReloadStagePartsV1' 'PAPER V1 must expose independent pistol stage flags and their per-part transform evidence.'
Require-Text 'src/api/PAPERApi.h' 'PaperWeaponKeywordFlagV1[\s\S]*PaperWeaponPrimaryFamilyV1[\s\S]*PaperWeaponFamilyFlagV1[\s\S]*PaperWeaponFamilyEvidenceFlagV1[\s\S]*familyFlags[\s\S]*primaryFamily[\s\S]*familyEvidenceFlags' 'PAPER V1 must own the raw keyword vocabulary and additive multi-label weapon-family classification contract.'
Require-Text 'src/api/PAPERProvider.cpp' 'getReloadStageStateV1[\s\S]*ReloadStageIdentification[\s\S]*reload_stages::getState' 'Stage state snapshots must remain owner-bound and capability-gated by PAPER.'
Require-Text 'src/api/PAPERProvider.cpp' 'copyReloadStagePartsV1[\s\S]*ReloadStageIdentification[\s\S]*reload_stages::copyParts' 'Stage part snapshots must remain owner-bound and capability-gated by PAPER.'
Require-Text 'src/api/PAPERApi.h' 'NativePosePipeline\s*=\s*1u\s*<<\s*10[\s\S]*PaperNativePoseFrameStateV1[\s\S]*PaperNativeHandSolutionV1[\s\S]*getNativePoseFrameStateV1[\s\S]*getNativeHandSolutionV1[\s\S]*PAPER_PROVIDER_API_V1_NATIVE_POSE_PIPELINE_TABLE_BYTES' 'PAPER V1 must append its observational native-pose pipeline without shifting earlier table extents.'
Require-Text 'src/api/PAPERProvider.cpp' 'getNativePoseFrameStateV1[\s\S]*NativePosePipeline[\s\S]*getNativeHandSolutionV1[\s\S]*NativePosePipeline[\s\S]*s_nativePosePipeline' 'Native-pose snapshots must remain owner-bound, capability-gated, and coherently published.'
Require-Text 'src/PaperMain.cpp' 'refreshEnrichmentDemand[\s\S]*NativePosePipeline[\s\S]*nativePosePipeline[\s\S]*Complete[\s\S]*native_pose_pipeline::publishFrame[\s\S]*completeRockFrame\(\)[\s\S]*dispatchEvent' 'Native-pose diagnostics must be demand-gated and published before transient frame state is cleared and consumers are notified.'
Require-Text 'src/animation/NativePosePipeline.cpp' 'queryPresentedHandPose[\s\S]*PresentedPoseCoherent[\s\S]*measurePoseResidual[\s\S]*queryDebugAuthoritySnapshot[\s\S]*publishNativePosePipeline' 'Native-pose publication must join PAPER decisions to the coherent final ROCK-presented pose and measured residual.'
Reject-Text 'src/api/PAPERApi.h' 'setNativePose|setHandSolution|setIk|setIK' 'The observational pipeline must not expose a generic pose or IK write contract.'
Require-Text 'src/api/PAPERProvider.cpp' 'validateReloadConsumer[\s\S]*ReloadObservations[\s\S]*ReloadEvidenceGeometry[\s\S]*getReloadCatalogStateV1[\s\S]*copyReloadEvidencePointsV1[\s\S]*copyReloadNodeObservationsV1' 'Reload observation and geometry queries must remain owner-bound and capability-gated.'
Require-Text 'src/PaperMain.cpp' 'NativeGraphOutput[\s\S]*reload_observation::capturePhase[\s\S]*BeforeRock[\s\S]*reload_observation::advanceFrame[\s\S]*AfterRock[\s\S]*PaperReloadObservationPhaseV1::PostRock[\s\S]*Complete[\s\S]*reload_observation::completeFrame[\s\S]*dispatchEvent' 'PAPER must publish raw weapon observations at its existing ROCK phases before notifying frame consumers.'
Require-Text 'src/PaperMain.cpp' 'BeforeRock[\s\S]*reload_observation::advanceFrame[\s\S]*animation_evidence::advanceFrame[\s\S]*Complete[\s\S]*reload_observation::completeFrame[\s\S]*animation_evidence::completeFrame[\s\S]*dispatchEvent' 'PAPER must enrich the committed reload catalog and publish live animation evidence before frame consumers run.'
Require-Text 'src/api/PAPERProvider.cpp' 'hasConsumerCapability[\s\S]*onOwnerThread\(\)[\s\S]*s_consumers[\s\S]*consumer\.capabilities' 'Internal enrichment demand must be derived from active, owner-thread PAPER consumer capabilities.'
Require-Text 'src/PaperMain.cpp' 'refreshEnrichmentDemand[\s\S]*ReloadAnimationEvidence[\s\S]*ReloadAnimationTelemetry[\s\S]*reloadEvidenceGeometry[\s\S]*ReloadEvidenceGeometry[\s\S]*ReloadObservations[\s\S]*enrichmentDemand\.reloadObservation[\s\S]*enrichmentDemand\.reloadEvidenceGeometry[\s\S]*enrichmentDemand\.animationTelemetry[\s\S]*enrichmentDemand\.exactAnimationEvidence' 'Reload observation, evidence geometry, passive animation telemetry, and exact evidence demand must remain independently capability-gated.'
Require-Text 'src/PaperMain.cpp' 'ReloadStageIdentification[\s\S]*reloadStages[\s\S]*reloadObservation[\s\S]*animationTelemetry[\s\S]*Complete[\s\S]*animation_evidence::completeFrame[\s\S]*reload_stages::completeFrame[\s\S]*dispatchEvent' 'Stage demand must activate its raw inputs and publish only after observation and animation snapshots complete.'
Require-Text 'src/api/PAPERApi.h' 'PAPER_MAX_RELOAD_EVIDENCE_V1\s*=\s*100' 'PAPER must preserve the complete bounded ROCK weapon evidence catalog instead of truncating it to the frame-snapshot body count.'
Require-Text 'src/reload_observation/ReloadObservation.cpp' 'geometryRequested[\s\S]*s_geometryDemandActive[\s\S]*collectEvidenceGeometry[\s\S]*record\.scheduledPointCount\s*=\s*collectEvidenceGeometry' 'Weapon evidence metadata must remain complete while expensive geometry collection follows only explicit geometry demand.'
Require-Text 'src/api/PAPERApi.h' 'PAPER_API_VERSION\s*=\s*1[\s\S]*ReloadAnimationEvidence\s*=\s*1u\s*<<\s*7[\s\S]*ReloadAnimationTelemetry\s*=\s*1u\s*<<\s*8[\s\S]*PAPER_PROVIDER_API_V1_RELOAD_OBSERVATION_TABLE_BYTES[\s\S]*PAPER_PROVIDER_API_V1_RELOAD_ANIMATION_TABLE_BYTES' 'Passive animation telemetry must be an additive V1 capability without changing the established full-evidence bit or table extent.'
Require-Text 'src/api/PAPERProvider.cpp' 'kReloadAnimationReadCapabilities[\s\S]*ReloadAnimationEvidence[\s\S]*ReloadAnimationTelemetry[\s\S]*validateReloadConsumerAny[\s\S]*getReloadAnimationCatalogStateV1[\s\S]*copyReloadAnimationSamplesV1[\s\S]*copyReloadAnimationSkeletonV1' 'Every raw animation query must remain owner-bound and accept either the passive-telemetry or full-evidence read capability.'
Require-Text 'src/animation_evidence/AnimationEvidence.cpp' 'exactPreharvestDemand[\s\S]*if \(state\.valid && state\.exactPreharvestDemandActive &&[\s\S]*clearCatalog\(state\)[\s\S]*if \(exactPreharvestDemand\)[\s\S]*updateExact\(state, names\)' 'Exact preharvest must run only under explicit full-evidence demand and release its native ownership when that demand drops.'
Require-Text 'src/animation_evidence/AnimationEvidence.cpp' 'passiveUpdateDue[\s\S]*kPassiveRewalkIntervalFrames[\s\S]*const bool passiveDue\s*=\s*passiveUpdateDue\(state\)[\s\S]*if \(passiveDue \|\| exactPreharvestDemand\)[\s\S]*collectWeaponNames\(root\)[\s\S]*if \(passiveDue\)[\s\S]*updatePassive\(state, root, names\)' 'Passive telemetry must gate the recursive weapon-name walk behind its rewalk cadence while exact preharvest retains explicit per-frame access.'
Require-Text 'src/animation_evidence/ExactClipPreharvest.cpp' 'kSimpleAnimationGraphManagerHolderCtor\s*=\s*0x0811F10[\s\S]*validateNativeEntry[\s\S]*selectFirstPersonGraph[\s\S]*clipSampleCount[\s\S]*publishSampledClip' 'Exact evidence must retain the validated off-screen first-person weapon sampler and publish raw full-clip tracks.'
Require-Text 'src/animation_evidence/ExactClipPreharvest.cpp' 'steady_clock::now[\s\S]*canSampleAnotherPose[\s\S]*duration_cast<std::chrono::microseconds>[\s\S]*clip\.nextSample' 'Exact sampling must yield against a wall-clock frame budget without removing raw sample points.'
Require-Text 'src/animation_evidence/ExactClipPreharvest.cpp' 'samplingBudgetYielded[\s\S]*sampleLimitYielded[\s\S]*snapshotDiagnostics[\s\S]*backgroundGraphActive[\s\S]*clipResourceActive' 'The exact preharvest must expose whether explicit sampling yielded and whether retained engine graph/resource ownership remains active.'
Require-Text 'src/animation_evidence/AnimationEvidence.cpp' 'FrameDiagnostics[\s\S]*catalogSetupMicroseconds[\s\S]*passiveDrainMicroseconds[\s\S]*nameCollectionMicroseconds[\s\S]*passiveUpdateMicroseconds[\s\S]*exactUpdateMicroseconds[\s\S]*passiveStatsMicroseconds[\s\S]*snapshotFrameDiagnostics' 'Animation enrichment must publish an internal per-stage timing snapshot that distinguishes catalog, passive, exact, and bookkeeping work.'
Require-Text 'src/PaperMain.cpp' 'RELOAD-PERF frames=[\s\S]*gameMs\(avg/max\)[\s\S]*paperMs\(avg/max\)[\s\S]*RELOAD-PERF detail[\s\S]*dispatch[\s\S]*background=[\s\S]*budgetYields=' 'PAPER must emit a rate-limited performance trace that separates engine frame time, provider phases, consumer dispatch, retained background graph lifetime, and sampling-budget engagement.'
Require-Text 'src/animation_evidence/ClipTelemetry.cpp' 'PAPER_LOG_DEBUG\(Weapon,[\s\S]*ANIMATION-MARKER \[annotation\][\s\S]*PAPER_LOG_DEBUG\(Weapon,[\s\S]*ANIMATION-MARKER \[trigger\]' 'Per-marker telemetry must not synchronously flood the normal INFO log during weapon equip.'
Reject-Text 'src/animation_evidence/ExactClipPreharvest.cpp' 'PAPER_LOG_WARN\(Animation,[\s\S]{0,160}Authored animation preharvest skipped weapon=' 'Expected per-clip preharvest rejections must not synchronously flood the normal WARN log.'
Require-Text 'src/animation_evidence/ClipTelemetry.cpp' 'kClipGeneratorActivateSlotOffset\s*=\s*0x38[\s\S]*kClipGeneratorUpdateSlotOffset\s*=\s*0x40[\s\S]*captureClipMarkers[\s\S]*CapturePacket[\s\S]*activityState' 'Passive evidence must retain raw clip lifecycle timing, markers, and sampled weapon tracks.'
Require-Text 'src/animation_evidence/ClipTelemetry.cpp' 'kClipGeneratorActivateFunctionOffset[\s\S]*REL::Module::IsVR\(\)[\s\S]*REL::Module::get\(\)\.version\(\)\s*!=\s*F4SE::RUNTIME_VR_1_2_72[\s\S]*hook validation failed' 'Passive lifecycle hooks must fail closed on the verified FO4VR 1.2.72 runtime and exact native entries.'
Reject-Text 'src/animation_evidence/ClipTelemetry.cpp' 'QueryInterface::RuntimeVersion|RuntimeVersion\(\)\s*(?:==|!=|<=|>=|<|>)|RUNTIME_LATEST_VR' 'Passive hook validation must use executable identity, never the loader-reported runtime constant.'
Reject-Text 'src/animation_evidence/ExactClipPreharvest.cpp' 'buildAuthoredGroups|isolateMagazineExtractionLeg|WeaponMagazine|MagOut|MagIn|ReloadDone|Converging|Diverging' 'Exact animation evidence must not reduce clips into semantic paths or special-case a magazine branch.'
Reject-Text 'src/animation_evidence/AnimationEvidence.cpp' 'MagOut|MagIn|ReloadDone|MagazineConnected|Converging|Diverging|movementThreshold' 'The animation evidence store must not infer reload stages from raw clips or transforms.'
Reject-Text 'src/animation_evidence/AnimationEvidence.cpp' 'paper_toolkit|PAPER_Toolkit' 'PAPER animation enrichment must not depend on or route through PAPER Toolkit.'
Require-Text 'src/reload_observation/ReloadObservation.cpp' 'queryEquippedWeaponClassification[\s\S]*baselineWeaponLocal[\s\S]*copyWeaponEvidenceDetails[\s\S]*copyWeaponEvidencePoints[\s\S]*snapshotSequence' 'PAPER must own classification/evidence passthrough, catalog baselines, and phase-stamped snapshot publication.'
Require-Text 'src/reload_observation/ReloadObservation.cpp' 'runtimeWeaponFamilySignals[\s\S]*kAutomatic[\s\S]*kBoltAction[\s\S]*classifyCatalogWeaponFamily[\s\S]*partEvidenceComplete[\s\S]*classifyWeaponFamily' 'PAPER weapon-family enrichment must combine live weapon flags and bounded ROCK part topology without making incomplete negative magazine claims.'
Require-Text 'src/reload_observation/WeaponClassificationPolicy.h' 'AKPattern[\s\S]*ARPattern[\s\S]*SubmachineGun[\s\S]*MachineGun[\s\S]*LightMachineGun' 'The pure classification policy must retain text-evidenced platform and automatic-weapon family distinctions.'
Require-Text 'src/reload_observation/WeaponClassificationPolicy.h' 'Revolver[\s\S]*BoltAction[\s\S]*BoltActionWithMagazine[\s\S]*BoltActionWithoutMagazine[\s\S]*LeverAction[\s\S]*PumpAction[\s\S]*BreakAction' 'The pure classification policy must retain action and magazine-presence distinctions.'
Require-Text 'src/reload_observation/WeaponClassificationPolicy.h' 'PartEvidenceIncomplete[\s\S]*primaryFamily' 'The pure classification policy must expose evidence incompleteness and a deterministic primary family for visual refinement.'
Require-Text 'src/reload_observation/ReloadObservation.cpp' 'resolveWeaponLocalFromGraph[\s\S]*object->local[\s\S]*composeTransforms' 'Reload part motion must be composed from graph-local transforms so controller motion cannot contaminate animation evidence.'
Reject-Text 'src/reload_observation/ReloadObservation.cpp' 'relativeTransform\([\s\S]{0,120}(?:root|weaponWorld)[\s\S]{0,120}object->world' 'Reload part motion must not subtract mixed-frame world-transform caches.'
Reject-Text 'src/reload_observation/ReloadObservation.cpp' 'MagOut|MagIn|ReloadDone|MagazineConnected|Converging|Diverging|movementThreshold' 'The provider must not infer reload stages or movement semantics from raw observations.'
Reject-Text 'src/reload_observation/ReloadObservationPolicy.h' 'MagOut|MagIn|ReloadDone|MagazineConnected|Converging|Diverging|movementThreshold' 'Observation target selection must remain semantic-free.'
Require-Text 'src/reload_stages/ReloadStages.cpp' 'RockProviderWeaponSizeClassV1::Pistol[\s\S]*RockProviderWeaponPartKindV1::Magazine[\s\S]*RockProviderWeaponPartKindV1::Slide[\s\S]*RockProviderWeaponPartKindV1::Bolt[\s\S]*RockProviderWeaponActionRoleV1::Bolt[\s\S]*const bool stageFrameValid\s*=\s*nativeOutputAvailable[\s\S]*PaperReloadStageFlagV1::Rest[\s\S]*PaperReloadStageFlagV1::Fire[\s\S]*PaperReloadStageFlagV1::SlideBack[\s\S]*PaperReloadStageFlagV1::SlideForward[\s\S]*PaperReloadStageFlagV1::BoltForward[\s\S]*PaperReloadStageFlagV1::BoltBack[\s\S]*PaperReloadStageFlagV1::MagazineIn[\s\S]*PaperReloadStageFlagV1::MagazineOut' 'The derived stage layer must retain pistol classification as evidence without letting a coarse size bucket suppress the eight independent requested signals.'
Reject-Text 'src/reload_stages/ReloadStages.cpp' 'stageFrameValid\s*=\s*pistol\s*&&' 'ROCK size classification must not suppress coherent semantic stage evidence selected for visual validation.'
Require-Text 'src/reload_stages/ReloadStages.cpp' 'allGroupMembersMatch\([\s\S]{0,180}aggregatePartCount[\s\S]{0,600}allGroupMembersMatch\([\s\S]{0,180}slidePartCount[\s\S]{0,600}allGroupMembersMatch\([\s\S]{0,180}slidePartCount[\s\S]{0,600}allGroupMembersMatch\([\s\S]{0,180}boltPartCount[\s\S]{0,600}allGroupMembersMatch\([\s\S]{0,180}boltPartCount[\s\S]{0,600}allGroupMembersMatch\([\s\S]{0,180}magazinePartCount[\s\S]{0,600}allGroupMembersMatch\([\s\S]{0,180}magazinePartCount' 'Rest, slide, bolt, and magazine aggregate stages must require every member of their deduplicated group to match.'
Reject-Text 'src/reload_stages/ReloadStages.cpp' 'animationName|WeaponMagazine|ReloadStart|ReloadEnd' 'Stage identification must use exact event/activity identities and classified part transforms, never animation or node-name guessing.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'afterHandlerActivityOrder\s*>\s*activityOrderBeforeEvent[\s\S]{0,160}afterHandlerActivityOrder\s*:[\s\S]{0,80}0' 'Fire correlation must not bind an animation activity that was already active before the handled weapon-fire event.'

$nativeAuthoritySource = Get-Content -Raw (Join-Path $Root 'src/animation/NativeAnimationAuthority.cpp')
$fixedRestoreStart = $nativeAuthoritySource.IndexOf('bool restoreFixedVisibleWeaponTarget(')
$fixedRestoreEnd = $nativeAuthoritySource.IndexOf('bool tryRestoreFixedVisibleWeaponTarget(', $fixedRestoreStart)
if ($fixedRestoreStart -lt 0 -or $fixedRestoreEnd -le $fixedRestoreStart) {
    $failures.Add('Paper source boundary could not isolate the fixed visible-weapon restore.')
} else {
    $fixedRestoreSource = $nativeAuthoritySource.Substring($fixedRestoreStart, $fixedRestoreEnd - $fixedRestoreStart)
    if ($fixedRestoreSource -match 'updateTransformsDown\s*\(') {
        $failures.Add('Fixed visible-weapon restore must not rebuild native animated descendants from their locals.')
    }
    if ($fixedRestoreSource -notmatch 'weaponNode->local\s*=\s*weaponLocal\s*;[\s\S]*weaponNode->world\s*=\s*weaponWorld\s*;') {
        $failures.Add('Fixed visible-weapon restore must update the Weapon root local and exact world without touching descendants.')
    }
}

Require-Text 'src/exports.def' 'PAPERAPI_GetProviderApi[\s\S]*PAPERAPI_GetProviderDescriptorV1' 'PAPER must export both the legacy-compatible V1 lookup and the table-extent descriptor.'

$sourceHeader = [System.IO.File]::ReadAllBytes((Join-Path $Root 'src/api/PAPERApi.h'))
$sdkHeader = [System.IO.File]::ReadAllBytes((Join-Path $Root 'SDK/PAPER/include/PAPERApi.h'))
if (-not [System.Linq.Enumerable]::SequenceEqual[byte]($sourceHeader, $sdkHeader)) {
    $failures.Add('The public Paper SDK header is not byte-for-byte synchronized with the provider header.')
}

if ($failures.Count -gt 0) {
    Write-Host 'PaperSourceTests failed:' -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host 'PaperSourceTests passed.' -ForegroundColor Green
