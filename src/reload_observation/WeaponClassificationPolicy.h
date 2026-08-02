#pragma once

#include "api/PAPERApi.h"

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
        api::PaperWeaponPrimaryFamilyV1 primaryFamily{
            api::PaperWeaponPrimaryFamilyV1::Unknown
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
        using api::PaperWeaponFamilyEvidenceFlagV1;
        using api::PaperWeaponFamilyFlagV1;
        using api::PaperWeaponKeywordFlagV1;
        using api::PaperWeaponPrimaryFamilyV1;
        using api::PaperWeaponSizeClassV1;

        WeaponFamilyClassification result{};
        const auto evidence = [](const PaperWeaponFamilyEvidenceFlagV1 value) {
            return static_cast<std::uint32_t>(value);
        };
        const auto family = [](const PaperWeaponFamilyFlagV1 value) {
            return static_cast<std::uint64_t>(value);
        };
        const auto keyword = [](const PaperWeaponKeywordFlagV1 value) {
            return static_cast<std::uint64_t>(value);
        };
        const auto add = [&](const PaperWeaponFamilyFlagV1 value,
                             const PaperWeaponFamilyEvidenceFlagV1 source) {
            result.familyFlags |= family(value);
            result.evidenceFlags |= evidence(source);
        };
        const auto hasKeyword = [&](const PaperWeaponKeywordFlagV1 value) {
            return (signals.keywordFlags & keyword(value)) != 0;
        };
        const auto hasFamily = [&](const PaperWeaponFamilyFlagV1 value) {
            return (result.familyFlags & family(value)) != 0;
        };

        if (signals.rockClassificationValid) {
            switch (static_cast<PaperWeaponSizeClassV1>(signals.sizeClass)) {
            case PaperWeaponSizeClassV1::Melee:
                add(PaperWeaponFamilyFlagV1::MeleeWeapon,
                    PaperWeaponFamilyEvidenceFlagV1::RockSizeClass);
                break;
            case PaperWeaponSizeClassV1::Pistol:
                add(PaperWeaponFamilyFlagV1::Pistol,
                    PaperWeaponFamilyEvidenceFlagV1::RockSizeClass);
                break;
            case PaperWeaponSizeClassV1::Rifle:
                add(PaperWeaponFamilyFlagV1::Rifle,
                    PaperWeaponFamilyEvidenceFlagV1::RockSizeClass);
                break;
            case PaperWeaponSizeClassV1::Heavy:
                add(PaperWeaponFamilyFlagV1::HeavyWeapon,
                    PaperWeaponFamilyEvidenceFlagV1::RockSizeClass);
                break;
            }
        }

        if (signals.rockClassificationAvailable) {
            const auto addKeywordFamily = [&](
                                              const PaperWeaponKeywordFlagV1 source,
                                              const PaperWeaponFamilyFlagV1 target) {
                if (hasKeyword(source)) {
                    add(target, PaperWeaponFamilyEvidenceFlagV1::RockKeyword);
                }
            };
            addKeywordFamily(PaperWeaponKeywordFlagV1::Pistol,
                PaperWeaponFamilyFlagV1::Pistol);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Rifle,
                PaperWeaponFamilyFlagV1::Rifle);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Shotgun,
                PaperWeaponFamilyFlagV1::Shotgun);
            addKeywordFamily(PaperWeaponKeywordFlagV1::AssaultRifle,
                PaperWeaponFamilyFlagV1::AssaultRifle);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Sniper,
                PaperWeaponFamilyFlagV1::SniperRifle);
            addKeywordFamily(PaperWeaponKeywordFlagV1::GaussRifle,
                PaperWeaponFamilyFlagV1::GaussRifle);
            addKeywordFamily(PaperWeaponKeywordFlagV1::LaserMusket,
                PaperWeaponFamilyFlagV1::LaserMusket);
            addKeywordFamily(PaperWeaponKeywordFlagV1::HeavyGun,
                PaperWeaponFamilyFlagV1::HeavyWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::HandToHand,
                PaperWeaponFamilyFlagV1::HandToHand);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Melee1H,
                PaperWeaponFamilyFlagV1::OneHandedMelee);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Melee2H,
                PaperWeaponFamilyFlagV1::TwoHandedMelee);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Unarmed,
                PaperWeaponFamilyFlagV1::UnarmedWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Minigun,
                PaperWeaponFamilyFlagV1::Minigun);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Fatman,
                PaperWeaponFamilyFlagV1::FatMan);
            addKeywordFamily(PaperWeaponKeywordFlagV1::MissileLauncher,
                PaperWeaponFamilyFlagV1::MissileLauncher);
            addKeywordFamily(PaperWeaponKeywordFlagV1::GatlingLaser,
                PaperWeaponFamilyFlagV1::GatlingLaser);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Flamer,
                PaperWeaponFamilyFlagV1::Flamer);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Cryolater,
                PaperWeaponFamilyFlagV1::Cryolator);
            addKeywordFamily(PaperWeaponKeywordFlagV1::JunkJet,
                PaperWeaponFamilyFlagV1::JunkJet);
            addKeywordFamily(PaperWeaponKeywordFlagV1::RailwayRifle,
                PaperWeaponFamilyFlagV1::RailwayRifle);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Broadsider,
                PaperWeaponFamilyFlagV1::Broadsider);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Syringer,
                PaperWeaponFamilyFlagV1::Syringer);
            addKeywordFamily(PaperWeaponKeywordFlagV1::FlareGun,
                PaperWeaponFamilyFlagV1::FlareGun);
            addKeywordFamily(PaperWeaponKeywordFlagV1::GammaGun,
                PaperWeaponFamilyFlagV1::GammaGun);
            addKeywordFamily(PaperWeaponKeywordFlagV1::AlienBlaster,
                PaperWeaponFamilyFlagV1::AlienBlaster);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Ripper,
                PaperWeaponFamilyFlagV1::Ripper);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Shishkebab,
                PaperWeaponFamilyFlagV1::Shishkebab);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Laser,
                PaperWeaponFamilyFlagV1::LaserWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Plasma,
                PaperWeaponFamilyFlagV1::PlasmaWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Ballistic,
                PaperWeaponFamilyFlagV1::BallisticWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Thrown,
                PaperWeaponFamilyFlagV1::ThrownWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Grenade,
                PaperWeaponFamilyFlagV1::Grenade);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Mine,
                PaperWeaponFamilyFlagV1::Mine);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Explosive,
                PaperWeaponFamilyFlagV1::ExplosiveWeapon);
            addKeywordFamily(PaperWeaponKeywordFlagV1::Automatic,
                PaperWeaponFamilyFlagV1::Automatic);
        }

        if (signals.magazinePart) {
            add(PaperWeaponFamilyFlagV1::MagazineFed,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.cylinderPart) {
            add(PaperWeaponFamilyFlagV1::Revolver,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
            add(PaperWeaponFamilyFlagV1::CylinderFed,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.slidePart) {
            add(PaperWeaponFamilyFlagV1::SlideOperated,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.leverPart) {
            add(PaperWeaponFamilyFlagV1::LeverAction,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.pumpPart) {
            add(PaperWeaponFamilyFlagV1::PumpAction,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.breakActionPart) {
            add(PaperWeaponFamilyFlagV1::BreakAction,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.laserCellPart) {
            add(PaperWeaponFamilyFlagV1::LaserCellFed,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
            add(PaperWeaponFamilyFlagV1::EnergyWeapon,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.shellPart) {
            add(PaperWeaponFamilyFlagV1::ShellFed,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }
        if (signals.looseRoundPart) {
            add(PaperWeaponFamilyFlagV1::LooseRoundFed,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }

        if (signals.revolverAnimationKeyword) {
            add(PaperWeaponFamilyFlagV1::Revolver,
                PaperWeaponFamilyEvidenceFlagV1::AnimationKeyword);
        }
        if (signals.boltActionWeaponFlag) {
            add(PaperWeaponFamilyFlagV1::BoltAction,
                PaperWeaponFamilyEvidenceFlagV1::WeaponFlags);
        }
        if (signals.automaticWeaponFlag) {
            add(PaperWeaponFamilyFlagV1::Automatic,
                PaperWeaponFamilyEvidenceFlagV1::WeaponFlags);
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
                                        const PaperWeaponFamilyFlagV1 value) {
            if (matched) {
                add(value,
                    PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
            }
        };
        addNamedFamily(namedPistol, PaperWeaponFamilyFlagV1::Pistol);
        addNamedFamily(namedRevolver, PaperWeaponFamilyFlagV1::Revolver);
        addNamedFamily(namedRifle, PaperWeaponFamilyFlagV1::Rifle);
        addNamedFamily(namedAssaultRifle,
            PaperWeaponFamilyFlagV1::AssaultRifle);
        addNamedFamily(namedShotgun, PaperWeaponFamilyFlagV1::Shotgun);
        addNamedFamily(namedSniper, PaperWeaponFamilyFlagV1::SniperRifle);
        addNamedFamily(namedBoltAction,
            PaperWeaponFamilyFlagV1::BoltAction);
        addNamedFamily(namedLeverAction,
            PaperWeaponFamilyFlagV1::LeverAction);
        addNamedFamily(namedPumpAction,
            PaperWeaponFamilyFlagV1::PumpAction);
        addNamedFamily(namedBreakAction,
            PaperWeaponFamilyFlagV1::BreakAction);

        if (akPattern) {
            add(PaperWeaponFamilyFlagV1::AKPattern,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (arPattern) {
            add(PaperWeaponFamilyFlagV1::ARPattern,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (submachineGun) {
            add(PaperWeaponFamilyFlagV1::SubmachineGun,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (machineGun) {
            add(PaperWeaponFamilyFlagV1::MachineGun,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (lightMachineGun) {
            add(PaperWeaponFamilyFlagV1::LightMachineGun,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (anyIdentityContains(signals, "bullpup")) {
            add(PaperWeaponFamilyFlagV1::BullpupPattern,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (anyIdentityContains(signals, "beltfed") ||
            anyIdentityContains(signals, "belt-fed") ||
            anyIdentityContains(signals, "belt fed")) {
            add(PaperWeaponFamilyFlagV1::BeltFed,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (anyIdentityContains(signals, "drumfed") ||
            anyIdentityContains(signals, "drum-fed") ||
            anyIdentityContains(signals, "drum fed")) {
            add(PaperWeaponFamilyFlagV1::DrumFed,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (anyIdentityContains(signals, "tubefed") ||
            anyIdentityContains(signals, "tube-fed") ||
            anyIdentityContains(signals, "tube fed")) {
            add(PaperWeaponFamilyFlagV1::TubeFed,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (anyIdentityContains(signals, "internalmag") ||
            anyIdentityContains(signals, "internal magazine") ||
            anyIdentityContains(signals, "fixedmag") ||
            anyIdentityContains(signals, "fixed magazine")) {
            add(PaperWeaponFamilyFlagV1::InternalFeed,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }
        if (anyIdentityContains(signals, "singleload") ||
            anyIdentityContains(signals, "single-load") ||
            anyIdentityContains(signals, "single load") ||
            anyIdentityContains(signals, "singleshot") ||
            anyIdentityContains(signals, "single-shot") ||
            anyIdentityContains(signals, "single shot")) {
            add(PaperWeaponFamilyFlagV1::SingleLoad,
                PaperWeaponFamilyEvidenceFlagV1::FormIdentityText);
        }

        if (hasFamily(PaperWeaponFamilyFlagV1::BoltAction)) {
            if (signals.magazinePart) {
                add(PaperWeaponFamilyFlagV1::BoltActionWithMagazine,
                    PaperWeaponFamilyEvidenceFlagV1::PartTopology);
            } else if (signals.partEvidenceComplete) {
                add(PaperWeaponFamilyFlagV1::BoltActionWithoutMagazine,
                    PaperWeaponFamilyEvidenceFlagV1::PartTopology);
            }
        }

        if ((signals.stockPart || signals.handguardPart ||
             signals.foregripPart ||
             hasFamily(PaperWeaponFamilyFlagV1::BoltAction) ||
             hasFamily(PaperWeaponFamilyFlagV1::LeverAction)) &&
            !hasFamily(PaperWeaponFamilyFlagV1::Pistol)) {
            add(PaperWeaponFamilyFlagV1::Rifle,
                PaperWeaponFamilyEvidenceFlagV1::PartTopology);
        }

        if (hasFamily(PaperWeaponFamilyFlagV1::AssaultRifle) ||
            hasFamily(PaperWeaponFamilyFlagV1::AKPattern) ||
            hasFamily(PaperWeaponFamilyFlagV1::ARPattern) ||
            hasFamily(PaperWeaponFamilyFlagV1::SniperRifle) ||
            hasFamily(PaperWeaponFamilyFlagV1::GaussRifle) ||
            hasFamily(PaperWeaponFamilyFlagV1::LaserMusket) ||
            hasFamily(PaperWeaponFamilyFlagV1::RailwayRifle)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::Rifle);
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::Revolver)) {
            if (!hasFamily(PaperWeaponFamilyFlagV1::Rifle) &&
                !hasFamily(PaperWeaponFamilyFlagV1::HeavyWeapon)) {
                result.familyFlags |= family(PaperWeaponFamilyFlagV1::Pistol);
            }
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::SlideOperated) &&
            !hasFamily(PaperWeaponFamilyFlagV1::Rifle) &&
            !hasFamily(PaperWeaponFamilyFlagV1::HeavyWeapon)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::Pistol);
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::LaserWeapon) ||
            hasFamily(PaperWeaponFamilyFlagV1::PlasmaWeapon) ||
            hasFamily(PaperWeaponFamilyFlagV1::GatlingLaser) ||
            hasFamily(PaperWeaponFamilyFlagV1::LaserMusket) ||
            hasFamily(PaperWeaponFamilyFlagV1::GammaGun) ||
            hasFamily(PaperWeaponFamilyFlagV1::AlienBlaster)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::EnergyWeapon);
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::Minigun) ||
            hasFamily(PaperWeaponFamilyFlagV1::GatlingLaser) ||
            hasFamily(PaperWeaponFamilyFlagV1::MissileLauncher) ||
            hasFamily(PaperWeaponFamilyFlagV1::FatMan) ||
            hasFamily(PaperWeaponFamilyFlagV1::Flamer) ||
            hasFamily(PaperWeaponFamilyFlagV1::Cryolator) ||
            hasFamily(PaperWeaponFamilyFlagV1::JunkJet) ||
            hasFamily(PaperWeaponFamilyFlagV1::Broadsider)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::HeavyWeapon);
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::MissileLauncher) ||
            hasFamily(PaperWeaponFamilyFlagV1::FatMan) ||
            hasFamily(PaperWeaponFamilyFlagV1::Broadsider) ||
            hasFamily(PaperWeaponFamilyFlagV1::FlareGun)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::Launcher);
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::OneHandedMelee) ||
            hasFamily(PaperWeaponFamilyFlagV1::TwoHandedMelee) ||
            hasFamily(PaperWeaponFamilyFlagV1::HandToHand) ||
            hasFamily(PaperWeaponFamilyFlagV1::Ripper) ||
            hasFamily(PaperWeaponFamilyFlagV1::Shishkebab)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::MeleeWeapon);
        }
        if (hasFamily(PaperWeaponFamilyFlagV1::Grenade) ||
            hasFamily(PaperWeaponFamilyFlagV1::Mine)) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::ThrownWeapon);
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::ExplosiveWeapon);
        }

        const bool manualCycle =
            hasFamily(PaperWeaponFamilyFlagV1::Revolver) ||
            hasFamily(PaperWeaponFamilyFlagV1::BoltAction) ||
            hasFamily(PaperWeaponFamilyFlagV1::LeverAction) ||
            hasFamily(PaperWeaponFamilyFlagV1::PumpAction) ||
            hasFamily(PaperWeaponFamilyFlagV1::BreakAction) ||
            hasFamily(PaperWeaponFamilyFlagV1::LaserMusket);
        if (manualCycle) {
            result.familyFlags |= family(PaperWeaponFamilyFlagV1::ManualCycle);
        }
        const bool firearm =
            hasFamily(PaperWeaponFamilyFlagV1::Pistol) ||
            hasFamily(PaperWeaponFamilyFlagV1::Rifle) ||
            hasFamily(PaperWeaponFamilyFlagV1::Shotgun) ||
            hasFamily(PaperWeaponFamilyFlagV1::HeavyWeapon) ||
            hasFamily(PaperWeaponFamilyFlagV1::SubmachineGun);
        if (signals.weaponDataAvailable && signals.partEvidenceComplete &&
            firearm &&
            !signals.automaticWeaponFlag && !manualCycle) {
            add(PaperWeaponFamilyFlagV1::SemiAutomatic,
                PaperWeaponFamilyEvidenceFlagV1::WeaponFlags);
        }
        if (!signals.partEvidenceComplete) {
            result.evidenceFlags |= evidence(
                PaperWeaponFamilyEvidenceFlagV1::PartEvidenceIncomplete);
        }

        if (hasFamily(PaperWeaponFamilyFlagV1::AKPattern)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::AKPattern;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::ARPattern)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::ARPattern;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::Revolver)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::Revolver;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::BoltActionWithMagazine)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::BoltActionWithMagazine;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::BoltActionWithoutMagazine)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::BoltActionWithoutMagazine;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::BoltAction)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::BoltAction;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::LeverAction)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::LeverAction;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::PumpAction)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::PumpAction;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::BreakAction)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::BreakAction;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::SubmachineGun)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::SubmachineGun;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::LightMachineGun)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::LightMachineGun;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::MachineGun)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::MachineGun;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::Minigun)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::Minigun;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::Launcher)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::Launcher;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::AssaultRifle)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::AssaultRifle;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::SniperRifle)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::SniperRifle;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::Shotgun)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::Shotgun;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::HeavyWeapon)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::HeavyWeapon;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::Pistol)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::Pistol;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::Rifle)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::Rifle;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::EnergyWeapon)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::EnergyWeapon;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::MeleeWeapon)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::MeleeWeapon;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::UnarmedWeapon)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::UnarmedWeapon;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::ThrownWeapon)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::ThrownWeapon;
        } else if (hasFamily(PaperWeaponFamilyFlagV1::ExplosiveWeapon)) {
            result.primaryFamily = PaperWeaponPrimaryFamilyV1::ExplosiveWeapon;
        }

        return result;
    }
}
