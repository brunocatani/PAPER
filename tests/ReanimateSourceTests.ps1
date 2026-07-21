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
Require-Text 'src/animation/NativeAnimationAuthority.cpp' 'installReloadStateChangeHook\(\)[\s\S]*installWeaponFireHook\(\)[\s\S]*captureNativeGraphOutput\(\)' 'Reload, manual-cycle fire, and native graph capture behavior must remain in Reanimate.'
Require-Text 'src/animation/NativeAnimationAuthority.cpp' '"Anims44"[\s\S]*"Revolver"[\s\S]*weaponData\.keywords[\s\S]*isManualCycleFireAnimationAllowed' 'Manual-cycle fire animation eligibility must admit Bethesda-style revolver animation keywords on both base and live instance data.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'WeaponTypePistol|RockProviderWeaponSizeClassV1::Pistol' 'Revolver admission must not broaden manual-cycle authority to every pistol.'
Reject-Text 'src/animation/NativeAnimationAuthority.cpp' 'kExpectedPostFrikPrefix|kFunc_PlayerPostUpdateAnimationGraphManager|entry_trampoline_hook' 'Reanimate must not install a competing PostUpdateAnimationGraphManager detour.'
Reject-Text 'src/native/NativeOffsets.h' 'PostUpdateAnimationGraphManager|UpdateFirstPersonArm' 'Reanimate native offsets must be limited to its reload/manual-cycle lifecycle ownership.'

Require-Text 'data/config/ROCK_Reanimate.ini' 'bNativeReloadAnimationAuthorityTestEnabled\s*=\s*true[\s\S]*bNativeReloadAnimationPartialAuthorityTestEnabled\s*=\s*true' 'Both migrated reload authority switches must default true in Reanimate.'
Reject-Text 'data/config/ROCK_Reanimate.ini' 'AuthoredPrimaryFiringGrip|Offhand|EquippedWeaponGrab' 'ROCK-owned equipped-weapon grip settings must not migrate to Reanimate.'

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
