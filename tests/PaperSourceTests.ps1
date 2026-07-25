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
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'installReloadStateChangeHook\(\)[\s\S]*installWeaponFireHook\(\)[\s\S]*captureNativeGraphOutput\(\)' 'Reload, manual-cycle fire, and native graph capture behavior must remain in Paper.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '"Anims44"[\s\S]*"Revolver"[\s\S]*weaponData\.keywords[\s\S]*isManualCycleFireAnimationAllowed' 'Manual-cycle fire animation eligibility must admit Bethesda-style revolver animation keywords on both base and live instance data.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'queryCurrentWeaponClassification[\s\S]*outClassification\.formId\s*==\s*weapon\.formID[\s\S]*RockProviderWeaponKeywordFlagV1::Shotgun[\s\S]*isManualCycleFireAnimationAllowed' 'Manual-cycle fire animation eligibility must admit the current weapon''s ROCK-classified shotgun family.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '\.rifle\s*=[\s\S]*currentWeaponClassificationValid[\s\S]*sizeClass\s*==[\s\S]*RockProviderWeaponSizeClassV1::Rifle' 'Manual-cycle fire animation eligibility must broadly admit the current weapon when ROCK classifies it as a rifle.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '"BoltAction"[\s\S]*"Lever"[\s\S]*"Repeater"[\s\S]*usesManualCycleAnimationKeyword[\s\S]*weaponData\.keywords[\s\S]*manualCycleAnimationKeyword' 'Manual-cycle fire animation eligibility must recognize bolt, lever, and repeater animation keywords on both base and live instance data.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '0x00226454[\s\S]*"Shotgun"[\s\S]*hasShotgunKeyword\(&weapon\)[\s\S]*hasShotgunKeyword\(weaponData\.keywords\)' 'Shotgun admission must retain the exact vanilla family keyword and a bounded base/live animation-keyword fallback.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'WeaponTypePistol|RockProviderWeaponSizeClassV1::Pistol' 'Revolver admission must not broaden manual-cycle authority to every pistol.'
Reject-Text 'src/PaperMain.cpp' 'TwoHandGripActive|WeaponTransformOwned' 'Manual-cycle hand animation must not require ROCK two-hand or weapon-transform ownership.'
Require-Text 'src/PaperMain.cpp' 'SupportFullAuthority[\s\S]*SupportVisualOnly[\s\S]*queryWeaponPartGripState[\s\S]*leftPartGripStateValid[\s\S]*WeaponRootLocal[\s\S]*leftSupportGripValid[\s\S]*querySelectedAuthoredGripPose[\s\S]*weaponGenerationKey[\s\S]*weaponFormId[\s\S]*authoredLeftValid' 'Manual-cycle support-hand animation must separately observe the active current-weapon support grip and ROCK''s selected authored pose.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'shouldPublishWeaponFixedSupportHand[\s\S]*authoredLeftActive[\s\S]*clearManualCycleVisualForHand\([\s\S]*Hand::Left' 'Manual-cycle support-hand publication must retain partial reloads, admit only an active authored grip, and clear Paper''s left-hand tag otherwise.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'leftSupportGripValid[\s\S]*authoredLeftValid[\s\S]*isAuthoredSupportGripMatch' 'Manual-cycle authored support-grip admission must require a bounded transform match between the active and selected authored grip targets.'
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
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'kNativeHandColor[\s\S]*kRockGripColor[\s\S]*kResolvedHandColor[\s\S]*kAppliedHandColor' 'Paper must preserve distinct native, ROCK grip, resolved, and applied hand colors.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'queryDebugAuthoritySnapshot[\s\S]*publishDebugOverlay' 'Paper must publish its native authority snapshot through ROCK.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'getPresentedHandFrameV1[\s\S]*RockProviderHandFrameFlagV1::PresentedVisual' 'Applied-hand diagnostics must use ROCK''s final hFRIK-presented frame, not its root-flattened physics authority.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' '!rockApiClient\(\)\.publishDebugOverlay\(publication\)[\s\S]*rockApiClient\(\)\.clearDebugOverlay\(\)' 'A rejected replacement must clear the preceding owner-scoped overlay instead of freezing stale diagnostics.'
Reject-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'VRCompositor|IVRCompositor|Submit\s*\(' 'Paper must consume ROCK''s renderer rather than install a second compositor path.'

Require-Text 'src/PaperMain.cpp' 'REL::Module::IsVR\(\)[\s\S]*const auto requiredRuntime\s*=\s*F4SE::RUNTIME_LATEST_VR;[\s\S]*RuntimeVersion\(\)\s*<\s*requiredRuntime' 'The new plugin must retain the approved FO4VR query compatibility pattern.'
Reject-Text 'CMakeLists.txt' 'F4VR-CommonFramework|PAPER_Toolkit' 'PAPER must depend directly on CommonLibF4VR and remain independent of PAPER_Toolkit.'
Require-Text 'src/api/PAPERApi.h' 'game-thread only[\s\S]*getRuntimeStateV1\)\([\s\S]*std::uint64_t ownerToken[\s\S]*copyCapturedTransformsV1\)\([\s\S]*std::uint64_t ownerToken' 'Paper provider queries must be owner-bound and explicitly game-thread-only.'
Require-Text 'src/api/PAPERProvider.cpp' 'PaperConsumerCapabilityV1::RuntimeState[\s\S]*PaperConsumerCapabilityV1::CapturedTransforms[\s\S]*PaperConsumerCapabilityV1::NativeHandPose' 'Paper provider queries must enforce negotiated capabilities.'

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
