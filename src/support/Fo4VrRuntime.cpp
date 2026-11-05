#include "support/Fo4VrRuntime.h"

namespace paper::fo4vr
{
    RE::PlayerCharacter* getPlayer() noexcept
    {
        return RE::PlayerCharacter::GetSingleton();
    }

    BSFlattenedBoneTree* getFlattenedBoneTree() noexcept
    {
        auto* player = getPlayer();
        auto* worldRoot = player && player->loadedData ?
            player->loadedData->data3D.get() : nullptr;
        auto* worldNode = worldRoot ? worldRoot->IsNode() : nullptr;
        if (!worldNode || worldNode->children.empty() || !worldNode->children[0]) {
            return nullptr;
        }
        return reinterpret_cast<BSFlattenedBoneTree*>(
            worldNode->children[0]->IsNode());
    }

    BSFlattenedBoneTree* getFirstPersonBoneTree() noexcept
    {
        auto* player = getPlayer();
        auto* skeleton = player ? player->firstPerson3D.get() : nullptr;
        if (!skeleton || skeleton->children.empty() || !skeleton->children[0]) {
            return nullptr;
        }
        return reinterpret_cast<BSFlattenedBoneTree*>(
            skeleton->children[0]->IsNode());
    }

    RE::NiNode* getFirstPersonWeaponNode() noexcept {
        auto* player=getPlayer();
        auto* root=player?player->firstPerson3D.get():nullptr;
        std::uint32_t visited=0;
        const auto find=[&](auto&& self,RE::NiAVObject* object,std::uint32_t depth)->RE::NiNode* {
            if (!object || depth>64 || ++visited>8192) return nullptr;
            if (const auto* name=object->name.c_str(); name && _stricmp(name,"Weapon")==0) return object->IsNode();
            if (auto* node=object->IsNode()) for (const auto& child:node->children) {
                if (auto* match=self(self,child.get(),depth+1)) return match;
            }
            return nullptr;
        };
        return find(find,root,0);
    }

    const RE::NiAVObject* getControllerNode(const bool isLeft) noexcept
    {
        // PlayerCharacter's VR node block in Fallout4VR.exe 1.2.72.
        constexpr std::uintptr_t kPrimaryWandNode = 0x6F0;
        constexpr std::uintptr_t kSecondaryWandNode = 0x768;

        // The setting object lives as long as the game; only its value changes.
        static RE::Setting* leftHandedMode = nullptr;
        if (!leftHandedMode) {
            constexpr const char* kName = "bLeftHandedMode:VR";
            auto* preferences = RE::INIPrefSettingCollection::GetSingleton();
            leftHandedMode = preferences ? preferences->GetSetting(kName) : nullptr;
            if (!leftHandedMode) {
                auto* settings = RE::INISettingCollection::GetSingleton();
                leftHandedMode = settings ? settings->GetSetting(kName) : nullptr;
            }
        }
        auto* player = getPlayer();
        if (!player || !leftHandedMode) {
            return nullptr;
        }
        const bool offhand = leftHandedMode->GetBinary() != isLeft;
        return *reinterpret_cast<RE::NiNode* const*>(
            reinterpret_cast<std::uintptr_t>(player) + (offhand ? kSecondaryWandNode : kPrimaryWandNode));
    }

    RE::EquippedItem* getEquippedItem() noexcept
    {
        auto* player = getPlayer();
        auto* middleHigh = player && player->currentProcess ?
            player->currentProcess->middleHigh : nullptr;
        if (!middleHigh || middleHigh->equippedItems.empty()) {
            return nullptr;
        }
        return std::addressof(middleHigh->equippedItems[0]);
    }

    void updateTransforms(RE::NiAVObject* node) noexcept
    {
        if (!node || !node->parent) {
            return;
        }
        const auto& parent = node->parent->world;
        const auto& local = node->local;
        node->world.translate = parent.translate +
            parent.rotate.Transpose() * (local.translate * parent.scale);
        node->world.rotate = local.rotate * parent.rotate;
        node->world.scale = parent.scale * local.scale;
    }

    void updateTransformsDown(RE::NiAVObject* node, const bool updateSelf) noexcept
    {
        if (!node) {
            return;
        }
        if (updateSelf) {
            updateTransforms(node);
        }
        auto* parent = node->IsNode();
        if (!parent) {
            return;
        }
        for (const auto& child : parent->children) {
            if (!child) {
                continue;
            }
            if (auto* childNode = child->IsNode()) {
                updateTransformsDown(childNode, true);
            } else if (auto* shape = child->IsTriShape()) {
                updateTransforms(shape);
            }
        }
    }
}
