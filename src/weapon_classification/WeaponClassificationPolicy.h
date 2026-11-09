#pragma once

#include <cstdint>
#include <string_view>

namespace paper::weapon_classification_policy
{
    // Mirrors of ROCK's raw classification vocabulary (weapon size class and
    // keyword mask), so the policy reads them without ROCK's headers.
    enum class WeaponSizeClass : std::uint32_t
    {
        Melee = 0,
        Pistol = 1,
        Rifle = 2,
        Heavy = 3,
    };

    enum class WeaponKeywordFlag : std::uint64_t
    {
        None = 0,
        Pistol = 1ull << 0,
        Rifle = 1ull << 1,
        Shotgun = 1ull << 2,
        AssaultRifle = 1ull << 3,
        Sniper = 1ull << 4,
        GaussRifle = 1ull << 5,
        LaserMusket = 1ull << 6,
        HeavyGun = 1ull << 7,
        HandToHand = 1ull << 8,
        Melee1H = 1ull << 9,
        Melee2H = 1ull << 10,
        Unarmed = 1ull << 11,
        Minigun = 1ull << 12,
        Fatman = 1ull << 13,
        MissileLauncher = 1ull << 14,
        GatlingLaser = 1ull << 15,
        Flamer = 1ull << 16,
        Cryolater = 1ull << 17,
        JunkJet = 1ull << 18,
        RailwayRifle = 1ull << 19,
        Broadsider = 1ull << 20,
        Syringer = 1ull << 21,
        FlareGun = 1ull << 22,
        GammaGun = 1ull << 23,
        AlienBlaster = 1ull << 24,
        Ripper = 1ull << 25,
        Shishkebab = 1ull << 26,
        Laser = 1ull << 27,
        Plasma = 1ull << 28,
        Ballistic = 1ull << 29,
        Thrown = 1ull << 30,
        Grenade = 1ull << 31,
        Mine = 1ull << 32,
        Explosive = 1ull << 33,
        Automatic = 1ull << 34,
    };

    enum class WeaponPrimaryFamily : std::uint32_t
    {
        Unknown = 0,
        Pistol = 1,
        Revolver = 2,
        Rifle = 3,
        AssaultRifle = 4,
        AKPattern = 5,
        ARPattern = 6,
        SubmachineGun = 7,
        Shotgun = 8,
        SniperRifle = 9,
        BoltActionWithMagazine = 10,
        BoltActionWithoutMagazine = 11,
        BoltAction = 12,
        LeverAction = 13,
        PumpAction = 14,
        BreakAction = 15,
        HeavyWeapon = 16,
        MachineGun = 17,
        LightMachineGun = 18,
        Minigun = 19,
        Launcher = 20,
        EnergyWeapon = 21,
        MeleeWeapon = 22,
        UnarmedWeapon = 23,
        ThrownWeapon = 24,
        ExplosiveWeapon = 25,
    };

    /*
     * Multi-label taxonomy: family, operating mechanism, feed evidence, and
     * the specific weapon families already represented by ROCK's raw keyword
     * mask may coexist. The primary family is only a summary; policy reads
     * these flags.
     */
    enum class WeaponFamilyFlag : std::uint64_t
    {
        None = 0,
        Pistol = 1ull << 0,
        Revolver = 1ull << 1,
        Rifle = 1ull << 2,
        AssaultRifle = 1ull << 3,
        AKPattern = 1ull << 4,
        ARPattern = 1ull << 5,
        SubmachineGun = 1ull << 6,
        Shotgun = 1ull << 7,
        SniperRifle = 1ull << 8,
        BoltAction = 1ull << 9,
        BoltActionWithMagazine = 1ull << 10,
        BoltActionWithoutMagazine = 1ull << 11,
        LeverAction = 1ull << 12,
        PumpAction = 1ull << 13,
        BreakAction = 1ull << 14,
        HeavyWeapon = 1ull << 15,
        MachineGun = 1ull << 16,
        LightMachineGun = 1ull << 17,
        Minigun = 1ull << 18,
        Launcher = 1ull << 19,
        EnergyWeapon = 1ull << 20,
        BallisticWeapon = 1ull << 21,
        MeleeWeapon = 1ull << 22,
        UnarmedWeapon = 1ull << 23,
        ThrownWeapon = 1ull << 24,
        ExplosiveWeapon = 1ull << 25,
        MagazineFed = 1ull << 26,
        CylinderFed = 1ull << 27,
        SlideOperated = 1ull << 28,
        LaserCellFed = 1ull << 29,
        ShellFed = 1ull << 30,
        LooseRoundFed = 1ull << 31,
        Automatic = 1ull << 32,
        SemiAutomatic = 1ull << 33,
        ManualCycle = 1ull << 34,
        InternalFeed = 1ull << 35,
        SingleLoad = 1ull << 36,
        TubeFed = 1ull << 37,
        BeltFed = 1ull << 38,
        DrumFed = 1ull << 39,
        BullpupPattern = 1ull << 40,
        GaussRifle = 1ull << 41,
        LaserMusket = 1ull << 42,
        RailwayRifle = 1ull << 43,
        GatlingLaser = 1ull << 44,
        MissileLauncher = 1ull << 45,
        FatMan = 1ull << 46,
        Flamer = 1ull << 47,
        Cryolator = 1ull << 48,
        JunkJet = 1ull << 49,
        Broadsider = 1ull << 50,
        Syringer = 1ull << 51,
        FlareGun = 1ull << 52,
        GammaGun = 1ull << 53,
        AlienBlaster = 1ull << 54,
        LaserWeapon = 1ull << 55,
        PlasmaWeapon = 1ull << 56,
        Grenade = 1ull << 57,
        Mine = 1ull << 58,
        OneHandedMelee = 1ull << 59,
        TwoHandedMelee = 1ull << 60,
        HandToHand = 1ull << 61,
        Ripper = 1ull << 62,
        Shishkebab = 1ull << 63,
    };

    enum class WeaponFamilyEvidenceFlag : std::uint32_t
    {
        None = 0,
        RockKeyword = 1u << 0,
        RockSizeClass = 1u << 1,
        WeaponFlags = 1u << 2,
        PartTopology = 1u << 3,
        AnimationKeyword = 1u << 4,
        FormIdentityText = 1u << 5,
        PartEvidenceIncomplete = 1u << 6,
    };

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
        bool doubleBarrelAnimationKeyword{ false };

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
        WeaponPrimaryFamily primaryFamily{
            WeaponPrimaryFamily::Unknown
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
        // A part's name says little about how the gun cycles: a gas cylinder, a selector
        // lever, a stock hinge or a mesh still named Cylinder.123 by its modelling tool
        // all match. An automatic gun cycles itself, so no part name makes it manual.
        const bool automatic = signals.automaticWeaponFlag ||
            hasKeyword(WeaponKeywordFlag::Automatic);
        if (signals.cylinderPart && !automatic) {
            add(WeaponFamilyFlag::Revolver,
                WeaponFamilyEvidenceFlag::PartTopology);
            add(WeaponFamilyFlag::CylinderFed,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.slidePart) {
            add(WeaponFamilyFlag::SlideOperated,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        // A slide catch or takedown lever also matches by name. A real lever action has
        // no magazine part, and the game flags it as a bolt action anyway.
        if (signals.leverPart && !signals.magazinePart && !automatic) {
            add(WeaponFamilyFlag::LeverAction,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.pumpPart && !automatic) {
            add(WeaponFamilyFlag::PumpAction,
                WeaponFamilyEvidenceFlag::PartTopology);
        }
        if (signals.breakActionPart && !automatic) {
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
        // No part of a double-barrel is named for its action, and the game gives it
        // no bolt-action flag, so its animation keyword is what identifies it.
        if (signals.doubleBarrelAnimationKeyword) {
            add(WeaponFamilyFlag::BreakAction,
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
            anyIdentityContains(signals, "break action") ||
            anyIdentityContains(signals, "doublebarrel") ||
            anyIdentityContains(signals, "double-barrel") ||
            anyIdentityContains(signals, "double barrel");

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
            // ROCK's size class is a coarse physical bucket. Use it as a
            // semantic family only when stronger keyword, identity, runtime,
            // or part evidence found no family.
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
