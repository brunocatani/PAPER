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
        $failures.Add("Reanimate must consume ROCK's graph-output phase instead of retaining '$relativePath'.")
    }
}

Require-Text 'src/ReanimateMain.cpp' 'NativeGraphOutput[\s\S]*publishRockAuthority\(runtimeOperational\)[\s\S]*captureNativeGraphOutput\(\)' 'Reanimate must publish authority and capture at ROCK''s native graph-output phase.'
Require-Text 'src/ReanimateMain.cpp' 'BeforeRock[\s\S]*installEventHooks\(\)[\s\S]*beginRockFrame[\s\S]*ApplyPhase::BeforeRock[\s\S]*AfterRock[\s\S]*ApplyPhase::AfterRock[\s\S]*Complete' 'Reanimate must own lifecycle hooks and both reload-pose application phases.'
Require-Text 'src/ReanimateMain.cpp' 'AfterRock[\s\S]*ApplyPhase::AfterRock[\s\S]*debug_visualization::publish\(\*context, s_gripState\)' 'Reanimate diagnostics must publish after its final native pose application.'
Require-Text 'src/ReanimateMain.cpp' 'resetSession\(\)[\s\S]*debug_visualization::clear\(\)' 'Session teardown must clear Reanimate-owned overlay publications.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'installReloadStateChangeHook\(\)[\s\S]*installWeaponFireHook\(\)[\s\S]*captureNativeGraphOutput\(\)' 'Reload, manual-cycle fire, and native graph capture behavior must remain in Reanimate.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '"Anims44"[\s\S]*"Revolver"[\s\S]*weaponData\.keywords[\s\S]*isManualCycleFireAnimationAllowed' 'Manual-cycle fire animation eligibility must admit Bethesda-style revolver animation keywords on both base and live instance data.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'queryEquippedWeaponClassification[\s\S]*classification\.formId\s*==\s*weapon\.formID[\s\S]*RockProviderWeaponKeywordFlagV1::Shotgun[\s\S]*isManualCycleFireAnimationAllowed' 'Manual-cycle fire animation eligibility must admit only the current weapon''s ROCK-classified shotgun family.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '"BoltAction"[\s\S]*"Lever"[\s\S]*"Repeater"[\s\S]*usesManualCycleAnimationKeyword[\s\S]*weaponData\.keywords[\s\S]*manualCycleAnimationKeyword' 'Manual-cycle fire animation eligibility must recognize bolt, lever, and repeater animation keywords on both base and live instance data.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '0x00226454[\s\S]*"Shotgun"[\s\S]*hasShotgunKeyword\(&weapon\)[\s\S]*hasShotgunKeyword\(weaponData\.keywords\)' 'Shotgun admission must retain the exact vanilla family keyword and a bounded base/live animation-keyword fallback.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'WeaponTypePistol|RockProviderWeaponSizeClassV1::Pistol' 'Revolver admission must not broaden manual-cycle authority to every pistol.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'RockProviderWeaponSizeClassV1::Rifle' 'Shotgun admission must use ROCK''s specific shotgun keyword instead of broadening manual-cycle authority to every rifle.'
Reject-Text 'src/ReanimateMain.cpp' 'TwoHandGripActive|WeaponTransformOwned' 'Manual-cycle hand animation must not require ROCK two-hand or weapon-transform ownership.'
Require-Text 'src/ReanimateMain.cpp' 'SupportFullAuthority[\s\S]*SupportVisualOnly[\s\S]*queryWeaponPartGripState[\s\S]*WeaponRootLocal[\s\S]*querySelectedAuthoredGripPose[\s\S]*weaponGenerationKey[\s\S]*weaponFormId[\s\S]*leftSupportGripValid' 'Manual-cycle support-hand animation must require the current weapon-root-local support grip to match ROCK''s selected authored grip identity.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'shouldPublishWeaponFixedSupportHand[\s\S]*authoredLeftActive[\s\S]*clearManualCycleVisualForHand\([\s\S]*Hand::Left' 'Manual-cycle support-hand publication must retain partial reloads, admit only an active authored grip, and clear Reanimate''s left-hand tag otherwise.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'leftSupportGripValid[\s\S]*authoredLeftValid[\s\S]*isAuthoredSupportGripMatch' 'Manual-cycle authored support-grip admission must require a bounded transform match between the active and selected authored grip targets.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'kExpectedPostFrikPrefix|kFunc_PlayerPostUpdateAnimationGraphManager|entry_trampoline_hook' 'Reanimate must not install a competing PostUpdateAnimationGraphManager detour.'
Reject-Text 'src/native/NativeOffsets.h' 'PostUpdateAnimationGraphManager|UpdateFirstPersonArm' 'Reanimate native offsets must be limited to its reload/manual-cycle lifecycle ownership.'

Require-Text 'data/config/ROCK_Reanimate.ini' 'bNativeReloadAnimationAuthorityTestEnabled\s*=\s*true[\s\S]*bNativeReloadAnimationPartialAuthorityTestEnabled\s*=\s*true' 'Both migrated reload authority switches must default true in Reanimate.'
Require-Text 'data/config/ROCK_Reanimate.ini' '\[Debug\][\s\S]*bDebugDrawNativeAnimation\s*=\s*false[\s\S]*bDebugDrawNativeAnimationText\s*=\s*true[\s\S]*fDebugNativeAnimationAxisLength[\s\S]*fDebugNativeAnimationMarkerSize' 'Reanimate must retain bounded, opt-in native animation visualization controls.'
Reject-Text 'data/config/ROCK_Reanimate.ini' 'AuthoredPrimaryFiringGrip|Offhand|EquippedWeaponGrab' 'ROCK-owned equipped-weapon grip settings must not migrate to Reanimate.'

Require-Text 'src/api/RockApiClient.cpp' 'DebugOverlayPublication[\s\S]*ROCK_PROVIDER_API_V1_NATIVE_ANIMATION_RUNTIME_CLEAR_TABLE_BYTES[\s\S]*supportsDebugOverlayPublicationV1[\s\S]*supportsPresentedHandFramesV1' 'Reanimate must negotiate ROCK''s complete V1 debug and final presented-hand surfaces.'
Require-Text 'src/api/RockApiClient.cpp' 'PoseReadback[\s\S]*supportsWeaponPartGripStateV1[\s\S]*supportsPoseReadbackV1[\s\S]*getWeaponPartGripStateV1[\s\S]*getSelectedAuthoredGripPoseV1' 'Reanimate must negotiate and consume ROCK''s V1 part-grip and authored-pose readback surfaces.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'kNativeHandColor[\s\S]*kRockGripColor[\s\S]*kResolvedHandColor[\s\S]*kAppliedHandColor' 'Reanimate must preserve distinct native, ROCK grip, resolved, and applied hand colors.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'queryDebugAuthoritySnapshot[\s\S]*publishDebugOverlay' 'Reanimate must publish its native authority snapshot through ROCK.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'getPresentedHandFrameV1[\s\S]*RockProviderHandFrameFlagV1::PresentedVisual' 'Applied-hand diagnostics must use ROCK''s final hFRIK-presented frame, not its root-flattened physics authority.'
Require-Text 'src/debug/NativeAnimationDebugVisualization.cpp' '!rockApiClient\(\)\.publishDebugOverlay\(publication\)[\s\S]*rockApiClient\(\)\.clearDebugOverlay\(\)' 'A rejected replacement must clear the preceding owner-scoped overlay instead of freezing stale diagnostics.'
Reject-Text 'src/debug/NativeAnimationDebugVisualization.cpp' 'VRCompositor|IVRCompositor|Submit\s*\(' 'Reanimate must consume ROCK''s renderer rather than install a second compositor path.'

Require-Text 'src/ReanimateMain.cpp' 'REL::Module::IsVR\(\)[\s\S]*const auto requiredRuntime\s*=\s*F4SE::RUNTIME_LATEST_VR;[\s\S]*RuntimeVersion\(\)\s*<\s*requiredRuntime' 'The new plugin must retain the approved FO4VR query compatibility pattern.'
Reject-Text 'CMakeLists.txt' 'F4VR-CommonFramework|PAPER' 'Reanimate must depend directly on CommonLibF4VR and remain independent of PAPER_Redux.'
Require-Text 'src/api/ROCKReanimateApi.h' 'game-thread only[\s\S]*getRuntimeStateV1\)\([\s\S]*std::uint64_t ownerToken[\s\S]*copyCapturedTransformsV1\)\([\s\S]*std::uint64_t ownerToken' 'Reanimate provider queries must be owner-bound and explicitly game-thread-only.'
Require-Text 'src/api/ROCKReanimateProvider.cpp' 'ReanimateConsumerCapabilityV1::RuntimeState[\s\S]*ReanimateConsumerCapabilityV1::CapturedTransforms[\s\S]*ReanimateConsumerCapabilityV1::NativeHandPose' 'Reanimate provider queries must enforce negotiated capabilities.'

$sourceHeader = [System.IO.File]::ReadAllBytes((Join-Path $Root 'src/api/ROCKReanimateApi.h'))
$sdkHeader = [System.IO.File]::ReadAllBytes((Join-Path $Root 'SDK/ROCK_Reanimate/include/ROCKReanimateApi.h'))
if (-not [System.Linq.Enumerable]::SequenceEqual[byte]($sourceHeader, $sdkHeader)) {
    $failures.Add('The public Reanimate SDK header is not byte-for-byte synchronized with the provider header.')
}

if ($failures.Count -gt 0) {
    Write-Host 'ReanimateSourceTests failed:' -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host 'ReanimateSourceTests passed.' -ForegroundColor Green
