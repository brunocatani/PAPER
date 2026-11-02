#pragma once

#include <PAPER/Provider.h>

#include <cstdint>
#include <string_view>

namespace paper::weapon_classification_policy
{
    struct WeaponFamilySignals
    {
        bool rockClassificationAvailable{ false };
        bool rockClassificationValid{ false };
        std::uint64_t keywordFlags{ 0 };
        std::uint32_t sizeClass{ 0 };

        bool weaponDataAvailable{ false };
        bool automaticWeaponFlag{ false };
        bool boltActionWeaponFlag{ false };
        bool revolverAnimationKeyword{ false };

        bool partEvidenceComplete{ false };
        bool magazinePart{ false };
        bool cylinderPart{ false };
        bool slidePart{ false };
        bool stockPart{ false };
        bool handguardPart{ false };
        bool foregripPart{ false };
        bool leverPart{ false };
        bool pumpPart{ false };
        bool breakActionPart{ false };
        bool laserCellPart{ false };
        bool shellPart{ false };
        bool looseRoundPart{ false };

        std::string_view pluginName{};
        std::string_view editorId{};
        std::string_view displayName{};
    };

    struct WeaponFamilyClassification
    {
        std::uint64_t familyFlags{ 0 };
        api::WeaponPrimaryFamily primaryFamily{
            api::WeaponPrimaryFamily::Unknown
        };
        std::uint32_t evidenceFlags{ 0 };
    };

    [[nodiscard]] constexpr char lowerAscii(const char value) noexcept
    {
        return value >= 'A' && value <= 'Z' ?
            static_cast<char>(value - 'A' + 'a') :
            value;
    }

    [[nodiscard]] constexpr bool equalsIgnoreCase(
        const std::string_view left,
        const std::string_view right) noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (lowerAscii(left[index]) != lowerAscii(right[index])) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool containsIgnoreCase(
        const std::string_view value,
        const std::string_view token) noexcept
    {
        if (token.empty()) {
            return true;
        }
        if (token.size() > value.size()) {
            return false;
        }
        for (std::size_t index = 0;
             index <= value.size() - token.size();
             ++index) {
            if (equalsIgnoreCase(value.substr(index, token.size()), token)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool isAsciiAlphaNumeric(
        const char value) noexcept
    {
        const auto lower = lowerAscii(value);
        return (lower >= 'a' && lower <= 'z') ||
               (lower >= '0' && lower <= '9');
    }

    [[nodiscard]] constexpr bool containsDelimitedTokenIgnoreCase(
        const std::string_view value,
        const std::string_view token) noexcept
    {
        if (token.empty() || token.size() > value.size()) {
            return false;
        }
        for (std::size_t index = 0;
             index <= value.size() - token.size();
             ++index) {
            if (!equalsIgnoreCase(value.substr(index, token.size()), token)) {
                continue;
            }
            const bool beginsAtBoundary =
                index == 0 || !isAsciiAlphaNumeric(value[index - 1]);
            const auto after = index + token.size();
            const bool endsAtBoundary =
                after == value.size() ||
                !isAsciiAlphaNumeric(value[after]);
            if (beginsAtBoundary && endsAtBoundary) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool anyIdentityContains(
        const WeaponFamilySignals& signals,
        const std::string_view token) noexcept
    {
        return containsIgnoreCase(signals.pluginName, token) ||
               containsIgnoreCase(signals.editorId, token) ||
               containsIgnoreCase(signals.displayName, token);
    }

    [[nodiscard]] constexpr bool anyIdentityHasToken(
        const WeaponFamilySignals& signals,
        const std::string_view token) noexcept
    {
        return containsDelimitedTokenIgnoreCase(signals.pluginName, token) ||
               containsDelimitedTokenIgnoreCase(signals.editorId, token) ||
               containsDelimitedTokenIgnoreCase(signals.displayName, token);
    }

    [[nodiscard]] constexpr WeaponFamilyClassification classifyWeaponFamily(
        const WeaponFamilySignals& signals) noexcept
    {
        using api::WeaponFamilyEvidenceFlag;
        using api::WeaponFamilyFlag;
        using api::WeaponKeywordFlag;
        using api::WeaponPrimaryFamily;
        using api::WeaponSizeClass;

        WeaponFamilyClassification result{};
        const auto evidence = [](const WeaponFamilyEvidenceFlag value) {
            return static_cast<std::uint32_t>(value);
        };
        const auto family = [](const WeaponFamilyFlag value) {
            return static_cast<std::uint64_t>(value);
        };
        const auto keyword = [](const WeaponKeywordFlag value) {
            return static_cast<std::uint64_t>(value);
        };
        const auto add = [&](const WeaponFamilyFlag value,
                             const WeaponFamilyEvidenceFlag source) {
            result.familyFlags |= family(value);
            result.evidenceFlags |= evidence(source);
        };
        const auto hasKeyword = [&](const WeaponKeywordFlag value) {
            return (signals.keywordFlags & keyword(value)) != 0;
        };
        const auto hasFamily = [&](const WeaponFamilyFlag value) {
            return (result.familyFlags & family(value)) != 0;
        };

        if (signals.rockClassificationAvailable) {
            const auto addKeywordFamily = [&](
                                              const WeaponKeywordFlag source,
                                              const WeaponFamilyFlag target) {
                if (hasKeyword(source)) {
                    add(target, WeaponFamilyEvidenceFlag::RockKeyword);
                }
            };
            addKeywordFamily(WeaponKeywordFlag::Pistol,
                WeaponFamilyFlag::Pistol);
            addKeywordFamily(WeaponKeywordFlag::Rifle,
                WeaponFamilyFlag::Rifle);
            addKeywordFamily(WeaponKeywordFlag::Shotgun,
                WeaponFamilyFlag::Shotgun);
            addKeywordFamily(WeaponKeywordFlag::AssaultRifle,
                WeaponFamilyFlag::AssaultRifle);
            addKeywordFamily(WeaponKeywordFlag::Sniper,
                WeaponFamilyFlag::SniperRifle);
            addKeywordFamily(WeaponKeywordFlag::GaussRifle,
                WeaponFamilyFlag::GaussRifle);
            addKeywordFamily(WeaponKeywordFlag::LaserMusket,
                WeaponFamilyFlag::LaserMusket);
            addKeywordFamily(WeaponKeywordFlag::HeavyGun,
                WeaponFamilyFlag::HeavyWeapon);
            addKeywordFamily(WeaponKeywordFlag::HandToHand,
                WeaponFamilyFlag::HandToHand);
            addKeywordFamily(WeaponKeywordFlag::Melee1H,
                WeaponFamilyFlag::OneHandedMelee);
            addKeywordFamily(WeaponKeywordFlag::Melee2H,
                WeaponFamilyFlag::TwoHandedMelee);
            addKeywordFamily(WeaponKeywordFlag::Unarmed,
                WeaponFamilyFlag::UnarmedWeapon);
            addKeywordFamily(WeaponKeywordFlag::Minigun,
                WeaponFamilyFlag::Minigun);
            addKeywordFamily(WeaponKeywordFlag::Fatman,
                WeaponFamilyFlag::FatMan);
            addKeywordFamily(WeaponKeywordFlag::MissileLauncher,
                WeaponFamilyFlag::MissileLauncher);
            addKeywordFamily(WeaponKeywordFlag::GatlingLaser,
                WeaponFamilyFlag::GatlingLaser);
            addKeywordFamily(WeaponKeywordFlag::Flamer,
                WeaponFamilyFlag::Flamer);
            addKeywordFamily(WeaponKeywordFlag::Cryolater,
                WeaponFamilyFlag::Cryolator);
            addKeywordFamily(WeaponKeywordFlag::JunkJet,
                WeaponFamilyFlag::JunkJet);
            addKeywordFamily(WeaponKeywordFlag::RailwayRifle,
                WeaponFamilyFlag::RailwayRifle);
            addKeywordFamily(WeaponKeywordFlag::Broadsider,
                WeaponFamilyFlag::Broadsider);
            addKeywordFamily(WeaponKeywordFlag::Syringer,
                WeaponFamilyFlag::Syringer);
            addKeywordFamily(WeaponKeywordFlag::FlareGun,
                WeaponFamilyFlag::FlareGun);
            addKeywordFamily(WeaponKeywordFlag::GammaGun,
                WeaponFamilyFlag::GammaGun);
            addKeywordFamily(WeaponKeywordFlag::AlienBlaster,
                WeaponFamilyFlag::AlienBlaster);
            addKeywordFamily(WeaponKeywordFlag::Ripper,
                WeaponFamilyFlag::Ripper);
            addKeywordFamily(WeaponKeywordFlag::Shishkebab,
                WeaponFamilyFlag::Shishkebab);
            addKeywordFamily(WeaponKeywordFlag::Laser,
                WeaponFamilyFlag::LaserWeapon);
            addKeywordFamily(WeaponKeywordFlag::Plasma,
                WeaponFamilyFlag::PlasmaWeapon);
            addKeywordFamily(WeaponKeywordFlag::Ballistic,
                WeaponFamilyFlag::BallisticWeapon);
            addKeywordFamily(WeaponKeywordFlag::Thrown,
                WeaponFamilyFlag::ThrownWeapon);
            addKeywordFamily(WeaponKeywordFlag::Grenade,
                WeaponFamilyFlag::Grenade);
            addKeywordFamily(WeaponKeywordFlag::Mine,
                WeaponFamilyFlag::Mine);
            addKeywordFamily(WeaponKeywordFlag::Explosive,
                WeaponFamilyFlag::ExplosiveWeapon);
            addKeywordFamily(WeaponKeywordFlag::Automatic,
                WeaponFamilyFlag::Automatic);
        }

        if (signals.magazinePart) {
            add(WeaponFamilyFlag::MagazineFed,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.cylinderPart) {
            add(WeaponFamilyFlag::Revolver,
                WeaponFamilyEvidenceFlag::PartTopology);
            add(WeaponFamilyFlag::CylinderFed,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.slidePart) {
            add(WeaponFamilyFlag::SlideOperated,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.leverPart) {
            add(WeaponFamilyFlag::LeverAction,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.pumpPart) {
            add(WeaponFamilyFlag::PumpAction,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.breakActionPart) {
            add(WeaponFamilyFlag::BreakAction,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.laserCellPart) {
            add(WeaponFamilyFlag::LaserCellFed,
                WeaponFamilyEvidenceFlag::PartTopology);
            add(WeaponFamilyFlag::EnergyWeapon,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.shellPart) {
            add(WeaponFamilyFlag::ShellFed,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.looseRoundPart) {
            add(WeaponFamilyFlag::LooseRoundFed,
                WeaponFamilyEvidenceFlag::PartTopology);
        }

        if (signals.revolverAnimationKeyword) {
            add(WeaponFamilyFlag::Revolver,
                WeaponFamilyEvidenceFlag::AnimationKeyword);
        }
        if (signals.boltActionWeaponFlag) {
            add(WeaponFamilyFlag::BoltAction,
                WeaponFamilyEvidenceFlag::WeaponFlags);
        }
        if (signals.automaticWeaponFlag) {
            add(WeaponFamilyFlag::Automatic,
                WeaponFamilyEvidenceFlag::WeaponFlags);
        }

        const bool akPattern =
            anyIdentityContains(signals, "ak47") ||
            anyIdentityContains(signals, "ak74") ||
            anyIdentityContains(signals, "kalashnikov") ||
            containsIgnoreCase(signals.editorId, "weapakm") ||
            containsIgnoreCase(signals.editorId, "weaponakm") ||
            anyIdentityHasToken(signals, "akm") ||
            anyIdentityHasToken(signals, "ak");
        const bool arPattern =
            anyIdentityContains(signals, "ar15") ||
            anyIdentityContains(signals, "ar-15") ||
            anyIdentityContains(signals, "ar_15") ||
            anyIdentityContains(signals, "m4a1") ||
            anyIdentityContains(signals, "m16") ||
            anyIdentityHasToken(signals, "m4");
        const bool submachineGun =
            anyIdentityContains(signals, "submachine") ||
            anyIdentityContains(signals, "machinepistol") ||
            anyIdentityHasToken(signals, "smg");
        const bool lightMachineGun =
            anyIdentityContains(signals, "lightmachine") ||
            anyIdentityContains(signals, "light machine") ||
            anyIdentityHasToken(signals, "lmg");
        const bool machineGun = !submachineGun &&
            (lightMachineGun ||
             anyIdentityContains(signals, "machinegun") ||
             anyIdentityContains(signals, "machine gun"));

        const bool namedPistol = anyIdentityContains(signals, "pistol");
        const bool namedRevolver = anyIdentityContains(signals, "revolver");
        const bool namedRifle = anyIdentityContains(signals, "rifle");
        const bool namedAssaultRifle =
            anyIdentityContains(signals, "assaultrifle") ||
            anyIdentityContains(signals, "assault rifle");
        const bool namedShotgun = anyIdentityContains(signals, "shotgun");
        const bool namedSniper = anyIdentityContains(signals, "sniper");
        const bool namedBoltAction =
            anyIdentityContains(signals, "boltaction") ||
            anyIdentityContains(signals, "bolt-action") ||
            anyIdentityContains(signals, "bolt action");
        const bool namedLeverAction =
            anyIdentityContains(signals, "leveraction") ||
            anyIdentityContains(signals, "lever-action") ||
            anyIdentityContains(signals, "lever action");
        const bool namedPumpAction =
            anyIdentityContains(signals, "pumpaction") ||
            anyIdentityContains(signals, "pump-action") ||
            anyIdentityContains(signals, "pump action");
        const bool namedBreakAction =
            anyIdentityContains(signals, "breakaction") ||
            anyIdentityContains(signals, "break-action") ||
            anyIdentityContains(signals, "break action");

        const auto addNamedFamily = [&](const bool matched,
                                        const WeaponFamilyFlag value) {
            if (matched) {
                add(value,
                    WeaponFamilyEvidenceFlag::FormIdentityText);
            }
        };
        addNamedFamily(namedPistol, WeaponFamilyFlag::Pistol);
        addNamedFamily(namedRevolver, WeaponFamilyFlag::Revolver);
        addNamedFamily(namedRifle, WeaponFamilyFlag::Rifle);
        addNamedFamily(namedAssaultRifle,
            WeaponFamilyFlag::AssaultRifle);
        addNamedFamily(namedShotgun, WeaponFamilyFlag::Shotgun);
        addNamedFamily(namedSniper, WeaponFamilyFlag::SniperRifle);
        addNamedFamily(namedBoltAction,
            WeaponFamilyFlag::BoltAction);
        addNamedFamily(namedLeverAction,
            WeaponFamilyFlag::LeverAction);
        addNamedFamily(namedPumpAction,
            WeaponFamilyFlag::PumpAction);
        addNamedFamily(namedBreakAction,
            WeaponFamilyFlag::BreakAction);

        if (akPattern) {
            add(WeaponFamilyFlag::AKPattern,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (arPattern) {
            add(WeaponFamilyFlag::ARPattern,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (submachineGun) {
            add(WeaponFamilyFlag::SubmachineGun,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (machineGun) {
            add(WeaponFamilyFlag::MachineGun,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (lightMachineGun) {
            add(WeaponFamilyFlag::LightMachineGun,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (anyIdentityContains(signals, "bullpup")) {
            add(WeaponFamilyFlag::BullpupPattern,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (anyIdentityContains(signals, "beltfed") ||
            anyIdentityContains(signals, "belt-fed") ||
            anyIdentityContains(signals, "belt fed")) {
            add(WeaponFamilyFlag::BeltFed,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (anyIdentityContains(signals, "drumfed") ||
            anyIdentityContains(signals, "drum-fed") ||
            anyIdentityContains(signals, "drum fed")) {
            add(WeaponFamilyFlag::DrumFed,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (anyIdentityContains(signals, "tubefed") ||
            anyIdentityContains(signals, "tube-fed") ||
            anyIdentityContains(signals, "tube fed")) {
            add(WeaponFamilyFlag::TubeFed,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (anyIdentityContains(signals, "internalmag") ||
            anyIdentityContains(signals, "internal magazine") ||
            anyIdentityContains(signals, "fixedmag") ||
            anyIdentityContains(signals, "fixed magazine")) {
            add(WeaponFamilyFlag::InternalFeed,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }
        if (anyIdentityContains(signals, "singleload") ||
            anyIdentityContains(signals, "single-load") ||
            anyIdentityContains(signals, "single load") ||
            anyIdentityContains(signals, "singleshot") ||
            anyIdentityContains(signals, "single-shot") ||
            anyIdentityContains(signals, "single shot")) {
            add(WeaponFamilyFlag::SingleLoad,
                WeaponFamilyEvidenceFlag::FormIdentityText);
        }

        if (hasFamily(WeaponFamilyFlag::BoltAction)) {
            if (signals.magazinePart) {
                add(WeaponFamilyFlag::BoltActionWithMagazine,
                    WeaponFamilyEvidenceFlag::PartTopology);
            } else if (signals.partEvidenceComplete) {
                add(WeaponFamilyFlag::BoltActionWithoutMagazine,
                    WeaponFamilyEvidenceFlag::PartTopology);
            }
        }

        const bool longGunTopology =
            signals.stockPart || signals.handguardPart ||
            signals.foregripPart ||
            hasFamily(WeaponFamilyFlag::BoltAction) ||
            hasFamily(WeaponFamilyFlag::LeverAction);
        const bool explicitLongGunFamily =
            hasFamily(WeaponFamilyFlag::Rifle) ||
            hasFamily(WeaponFamilyFlag::AssaultRifle) ||
            hasFamily(WeaponFamilyFlag::AKPattern) ||
            hasFamily(WeaponFamilyFlag::ARPattern) ||
            hasFamily(WeaponFamilyFlag::SniperRifle) ||
            hasFamily(WeaponFamilyFlag::Shotgun) ||
            hasFamily(WeaponFamilyFlag::GaussRifle) ||
            hasFamily(WeaponFamilyFlag::LaserMusket) ||
            hasFamily(WeaponFamilyFlag::RailwayRifle);

        if (hasFamily(WeaponFamilyFlag::SlideOperated) &&
            !longGunTopology && !explicitLongGunFamily &&
            !hasFamily(WeaponFamilyFlag::HeavyWeapon)) {
            result.familyFlags |= family(WeaponFamilyFlag::Pistol);
        }

        if (longGunTopology &&
            !hasFamily(WeaponFamilyFlag::Pistol)) {
            add(WeaponFamilyFlag::Rifle,
                WeaponFamilyEvidenceFlag::PartTopology);
        }

        if (hasFamily(WeaponFamilyFlag::AssaultRifle) ||
            hasFamily(WeaponFamilyFlag::AKPattern) ||
            hasFamily(WeaponFamilyFlag::ARPattern) ||
            hasFamily(WeaponFamilyFlag::SniperRifle) ||
            hasFamily(WeaponFamilyFlag::GaussRifle) ||
            hasFamily(WeaponFamilyFlag::LaserMusket) ||
            hasFamily(WeaponFamilyFlag::RailwayRifle)) {
            result.familyFlags |= family(WeaponFamilyFlag::Rifle);
        }
        if (hasFamily(WeaponFamilyFlag::Revolver)) {
            if (!hasFamily(WeaponFamilyFlag::Rifle) &&
                !hasFamily(WeaponFamilyFlag::HeavyWeapon)) {
                result.familyFlags |= family(WeaponFamilyFlag::Pistol);
            }
        }
        if (hasFamily(WeaponFamilyFlag::LaserWeapon) ||
            hasFamily(WeaponFamilyFlag::PlasmaWeapon) ||
            hasFamily(WeaponFamilyFlag::GatlingLaser) ||
            hasFamily(WeaponFamilyFlag::LaserMusket) ||
            hasFamily(WeaponFamilyFlag::GammaGun) ||
            hasFamily(WeaponFamilyFlag::AlienBlaster)) {
            result.familyFlags |= family(WeaponFamilyFlag::EnergyWeapon);
        }
        if (hasFamily(WeaponFamilyFlag::Minigun) ||
            hasFamily(WeaponFamilyFlag::GatlingLaser) ||
            hasFamily(WeaponFamilyFlag::MissileLauncher) ||
            hasFamily(WeaponFamilyFlag::FatMan) ||
            hasFamily(WeaponFamilyFlag::Flamer) ||
            hasFamily(WeaponFamilyFlag::Cryolator) ||
            hasFamily(WeaponFamilyFlag::JunkJet) ||
            hasFamily(WeaponFamilyFlag::Broadsider)) {
            result.familyFlags |= family(WeaponFamilyFlag::HeavyWeapon);
        }
        if (hasFamily(WeaponFamilyFlag::MissileLauncher) ||
            hasFamily(WeaponFamilyFlag::FatMan) ||
            hasFamily(WeaponFamilyFlag::Broadsider) ||
            hasFamily(WeaponFamilyFlag::FlareGun)) {
            result.familyFlags |= family(WeaponFamilyFlag::Launcher);
        }
        if (hasFamily(WeaponFamilyFlag::OneHandedMelee) ||
            hasFamily(WeaponFamilyFlag::TwoHandedMelee) ||
            hasFamily(WeaponFamilyFlag::HandToHand) ||
            hasFamily(WeaponFamilyFlag::Ripper) ||
            hasFamily(WeaponFamilyFlag::Shishkebab)) {
            result.familyFlags |= family(WeaponFamilyFlag::MeleeWeapon);
        }
        if (hasFamily(WeaponFamilyFlag::Grenade) ||
            hasFamily(WeaponFamilyFlag::Mine)) {
            result.familyFlags |= family(WeaponFamilyFlag::ThrownWeapon);
            result.familyFlags |= family(WeaponFamilyFlag::ExplosiveWeapon);
        }

        const bool semanticSizeKnown =
            hasFamily(WeaponFamilyFlag::Pistol) ||
            hasFamily(WeaponFamilyFlag::Rifle) ||
            hasFamily(WeaponFamilyFlag::Shotgun) ||
            hasFamily(WeaponFamilyFlag::SubmachineGun) ||
            hasFamily(WeaponFamilyFlag::MachineGun) ||
            hasFamily(WeaponFamilyFlag::LightMachineGun) ||
            hasFamily(WeaponFamilyFlag::HeavyWeapon) ||
            hasFamily(WeaponFamilyFlag::Minigun) ||
            hasFamily(WeaponFamilyFlag::Launcher) ||
            hasFamily(WeaponFamilyFlag::MeleeWeapon) ||
            hasFamily(WeaponFamilyFlag::UnarmedWeapon) ||
            hasFamily(WeaponFamilyFlag::ThrownWeapon) ||
            hasFamily(WeaponFamilyFlag::ExplosiveWeapon);
        if (signals.rockClassificationValid && !semanticSizeKnown) {
            // ROCK's size class is a coarse physical bucket. Preserve it in
            // the API, but use it as a semantic family only when stronger
            // keyword, identity, runtime, or part evidence found no family.
            switch (static_cast<WeaponSizeClass>(signals.sizeClass)) {
            case WeaponSizeClass::Melee:
                add(WeaponFamilyFlag::MeleeWeapon,
                    WeaponFamilyEvidenceFlag::RockSizeClass);
                break;
            case WeaponSizeClass::Pistol:
                add(WeaponFamilyFlag::Pistol,
                    WeaponFamilyEvidenceFlag::RockSizeClass);
                break;
            case WeaponSizeClass::Rifle:
                add(WeaponFamilyFlag::Rifle,
                    WeaponFamilyEvidenceFlag::RockSizeClass);
                break;
            case WeaponSizeClass::Heavy:
                add(WeaponFamilyFlag::HeavyWeapon,
                    WeaponFamilyEvidenceFlag::RockSizeClass);
                break;
            }
        }

        const bool manualCycle =
            hasFamily(WeaponFamilyFlag::Revolver) ||
            hasFamily(WeaponFamilyFlag::BoltAction) ||
            hasFamily(WeaponFamilyFlag::LeverAction) ||
            hasFamily(WeaponFamilyFlag::PumpAction) ||
            hasFamily(WeaponFamilyFlag::BreakAction) ||
            hasFamily(WeaponFamilyFlag::LaserMusket);
        if (manualCycle) {
            result.familyFlags |= family(WeaponFamilyFlag::ManualCycle);
        }
        const bool firearm =
            hasFamily(WeaponFamilyFlag::Pistol) ||
            hasFamily(WeaponFamilyFlag::Rifle) ||
            hasFamily(WeaponFamilyFlag::Shotgun) ||
            hasFamily(WeaponFamilyFlag::HeavyWeapon) ||
            hasFamily(WeaponFamilyFlag::SubmachineGun);
        if (signals.weaponDataAvailable && signals.partEvidenceComplete &&
            firearm &&
            !signals.automaticWeaponFlag && !manualCycle) {
            add(WeaponFamilyFlag::SemiAutomatic,
                WeaponFamilyEvidenceFlag::WeaponFlags);
        }
        if (!signals.partEvidenceComplete) {
            result.evidenceFlags |= evidence(
                WeaponFamilyEvidenceFlag::PartEvidenceIncomplete);
        }

        if (hasFamily(WeaponFamilyFlag::AKPattern)) {
            result.primaryFamily = WeaponPrimaryFamily::AKPattern;
        } else if (hasFamily(WeaponFamilyFlag::ARPattern)) {
            result.primaryFamily = WeaponPrimaryFamily::ARPattern;
        } else if (hasFamily(WeaponFamilyFlag::Revolver)) {
            result.primaryFamily = WeaponPrimaryFamily::Revolver;
        } else if (hasFamily(WeaponFamilyFlag::BoltActionWithMagazine)) {
            result.primaryFamily = WeaponPrimaryFamily::BoltActionWithMagazine;
        } else if (hasFamily(WeaponFamilyFlag::BoltActionWithoutMagazine)) {
            result.primaryFamily = WeaponPrimaryFamily::BoltActionWithoutMagazine;
        } else if (hasFamily(WeaponFamilyFlag::BoltAction)) {
            result.primaryFamily = WeaponPrimaryFamily::BoltAction;
        } else if (hasFamily(WeaponFamilyFlag::LeverAction)) {
            result.primaryFamily = WeaponPrimaryFamily::LeverAction;
        } else if (hasFamily(WeaponFamilyFlag::PumpAction)) {
            result.primaryFamily = WeaponPrimaryFamily::PumpAction;
        } else if (hasFamily(WeaponFamilyFlag::BreakAction)) {
            result.primaryFamily = WeaponPrimaryFamily::BreakAction;
        } else if (hasFamily(WeaponFamilyFlag::SubmachineGun)) {
            result.primaryFamily = WeaponPrimaryFamily::SubmachineGun;
        } else if (hasFamily(WeaponFamilyFlag::LightMachineGun)) {
            result.primaryFamily = WeaponPrimaryFamily::LightMachineGun;
        } else if (hasFamily(WeaponFamilyFlag::MachineGun)) {
            result.primaryFamily = WeaponPrimaryFamily::MachineGun;
        } else if (hasFamily(WeaponFamilyFlag::Minigun)) {
            result.primaryFamily = WeaponPrimaryFamily::Minigun;
        } else if (hasFamily(WeaponFamilyFlag::Launcher)) {
            result.primaryFamily = WeaponPrimaryFamily::Launcher;
        } else if (hasFamily(WeaponFamilyFlag::AssaultRifle)) {
            result.primaryFamily = WeaponPrimaryFamily::AssaultRifle;
        } else if (hasFamily(WeaponFamilyFlag::SniperRifle)) {
            result.primaryFamily = WeaponPrimaryFamily::SniperRifle;
        } else if (hasFamily(WeaponFamilyFlag::Shotgun)) {
            result.primaryFamily = WeaponPrimaryFamily::Shotgun;
        } else if (hasFamily(WeaponFamilyFlag::HeavyWeapon)) {
            result.primaryFamily = WeaponPrimaryFamily::HeavyWeapon;
        } else if (hasFamily(WeaponFamilyFlag::Pistol)) {
            result.primaryFamily = WeaponPrimaryFamily::Pistol;
        } else if (hasFamily(WeaponFamilyFlag::Rifle)) {
            result.primaryFamily = WeaponPrimaryFamily::Rifle;
        } else if (hasFamily(WeaponFamilyFlag::EnergyWeapon)) {
            result.primaryFamily = WeaponPrimaryFamily::EnergyWeapon;
        } else if (hasFamily(WeaponFamilyFlag::MeleeWeapon)) {
            result.primaryFamily = WeaponPrimaryFamily::MeleeWeapon;
        } else if (hasFamily(WeaponFamilyFlag::UnarmedWeapon)) {
            result.primaryFamily = WeaponPrimaryFamily::UnarmedWeapon;
        } else if (hasFamily(WeaponFamilyFlag::ThrownWeapon)) {
            result.primaryFamily = WeaponPrimaryFamily::ThrownWeapon;
        } else if (hasFamily(WeaponFamilyFlag::ExplosiveWeapon)) {
            result.primaryFamily = WeaponPrimaryFamily::ExplosiveWeapon;
        }

        return result;
    }
}
