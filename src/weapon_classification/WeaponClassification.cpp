#include "PCH.h"

#include "weapon_classification/WeaponClassification.h"

#include "support/Fo4VrRuntime.h"
#include "weapon_classification/WeaponClassificationPolicy.h"

#include "RE/Bethesda/TESBoundObjects.h"

#include <string_view>

namespace paper::weapon_classification
{
    namespace
    {
        struct RuntimeWeaponFamilySignals
        {
            bool weaponDataAvailable{ false };
            bool automaticWeaponFlag{ false };
            bool boltActionWeaponFlag{ false };
            bool revolverAnimationKeyword{ false };
            bool doubleBarrelAnimationKeyword{ false };
        };

        [[nodiscard]] bool hasKeywordContaining(
            const RE::BGSKeywordForm* keywords,
            const std::string_view token,
            const std::string_view otherToken = {})
        {
            if (!keywords) {
                return false;
            }
            bool matched = false;
            keywords->ForEachKeyword(
                [&](const RE::BGSKeyword* keyword) {
                    const char* editorId =
                        keyword ? keyword->formEditorID.c_str() : nullptr;
                    const std::string_view value = editorId ?
                        std::string_view{ editorId } :
                        std::string_view{};
                    if (weapon_classification_policy::containsIgnoreCase(
                            value,
                            token) ||
                        (!otherToken.empty() &&
                            weapon_classification_policy::containsIgnoreCase(
                                value,
                                otherToken))) {
                        matched = true;
                        return RE::BSContainer::ForEachResult::kStop;
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            return matched;
        }

        [[nodiscard]] RuntimeWeaponFamilySignals runtimeWeaponFamilySignals(
            RE::TESObjectWEAP* weapon)
        {
            RuntimeWeaponFamilySignals result{};
            const RE::TESObjectWEAP::InstanceData* weaponData =
                &weapon->weaponData;
            if (auto* equipData = f4vr::getEquippedItem();
                equipData && equipData->item.object == weapon &&
                equipData->item.instanceData) {
                weaponData = static_cast<RE::TESObjectWEAP::InstanceData*>(
                    equipData->item.instanceData.get());
            }
            if (!weaponData) {
                return result;
            }

            result.weaponDataAvailable = true;
            result.automaticWeaponFlag = weaponData->flags.any(
                RE::WEAPON_FLAGS::kAutomatic);
            result.boltActionWeaponFlag = weaponData->flags.any(
                RE::WEAPON_FLAGS::kBoltAction);
            const auto namesRevolver = [](const RE::BGSKeywordForm* keywords) {
                return hasKeywordContaining(keywords, "revolver", "anims44") ||
                    hasKeywordContaining(keywords, "wasterev", "revshotgun");
            };
            result.revolverAnimationKeyword =
                namesRevolver(weapon) || namesRevolver(weaponData->keywords);
            // Only the animation keyword: the double-barrel's fire sound keyword is also
            // on a pump shotgun and a revolving one.
            result.doubleBarrelAnimationKeyword =
                hasKeywordContaining(weapon, "animsdoublebarrel") ||
                hasKeywordContaining(weaponData->keywords, "animsdoublebarrel");
            return result;
        }

        void addPartEvidence(
            weapon_classification_policy::WeaponFamilySignals& signals,
            const std::uint32_t partKindValue,
            const std::uint32_t actionRoleValue)
        {
            switch (static_cast<rock::api::weaponparts::PartKind>(partKindValue)) {
            case rock::api::weaponparts::PartKind::Magazine:
                signals.magazinePart = true;
                break;
            case rock::api::weaponparts::PartKind::Cylinder:
                signals.cylinderPart = true;
                break;
            case rock::api::weaponparts::PartKind::Slide:
                signals.slidePart = true;
                break;
            case rock::api::weaponparts::PartKind::Stock:
                signals.stockPart = true;
                break;
            case rock::api::weaponparts::PartKind::Handguard:
                signals.handguardPart = true;
                break;
            case rock::api::weaponparts::PartKind::Foregrip:
                signals.foregripPart = true;
                break;
            case rock::api::weaponparts::PartKind::Lever:
                signals.leverPart = true;
                break;
            case rock::api::weaponparts::PartKind::Pump:
                signals.pumpPart = true;
                break;
            case rock::api::weaponparts::PartKind::BreakAction:
                signals.breakActionPart = true;
                break;
            case rock::api::weaponparts::PartKind::LaserCell:
                signals.laserCellPart = true;
                break;
            case rock::api::weaponparts::PartKind::Shell:
                signals.shellPart = true;
                break;
            case rock::api::weaponparts::PartKind::Round:
                signals.looseRoundPart = true;
                break;
            default:
                break;
            }

            switch (static_cast<rock::api::weaponparts::ActionRole>(actionRoleValue)) {
            case rock::api::weaponparts::ActionRole::Slide:
                signals.slidePart = true;
                break;
            case rock::api::weaponparts::ActionRole::Cylinder:
                signals.cylinderPart = true;
                break;
            case rock::api::weaponparts::ActionRole::Lever:
                signals.leverPart = true;
                break;
            case rock::api::weaponparts::ActionRole::Pump:
                signals.pumpPart = true;
                break;
            case rock::api::weaponparts::ActionRole::BreakAction:
                signals.breakActionPart = true;
                break;
            default:
                break;
            }
        }
    }

    std::uint64_t classifyWeaponFamilyFlags(
        const std::uint32_t weaponFormId,
        const rock::api::weapon::Classification& classification,
        const std::span<const rock::api::weaponparts::EvidenceDetail> evidence)
    {
        weapon_classification_policy::WeaponFamilySignals signals{
            .rockClassificationAvailable = true,
            .rockClassificationValid = classification.valid != 0,
            .keywordFlags = classification.keywordFlags,
            .sizeClass = static_cast<std::uint32_t>(classification.sizeClass),
            .partEvidenceComplete = !evidence.empty(),
        };
        for (const auto& detail : evidence) {
            addPartEvidence(signals, detail.partKind, detail.actionRole);
        }

        // Identity text is read in place: plugin file names, editor IDs and
        // full names live as long as their form.
        auto* form = weaponFormId != 0 ? RE::TESForm::GetFormByID(weaponFormId) : nullptr;
        if (form) {
            if (auto* file = form->GetFile(0)) {
                signals.pluginName = file->GetFilename();
            }
            if (const char* editorId = form->GetFormEditorID()) {
                signals.editorId = editorId;
            }
            signals.displayName = RE::TESFullName::GetFullName(*form);
        }
        if (auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr) {
            const auto runtime = runtimeWeaponFamilySignals(weapon);
            signals.weaponDataAvailable = runtime.weaponDataAvailable;
            signals.automaticWeaponFlag = runtime.automaticWeaponFlag;
            signals.boltActionWeaponFlag = runtime.boltActionWeaponFlag;
            signals.revolverAnimationKeyword = runtime.revolverAnimationKeyword;
            signals.doubleBarrelAnimationKeyword = runtime.doubleBarrelAnimationKeyword;
        }
        return weapon_classification_policy::classifyWeaponFamily(signals).familyFlags;
    }
}
