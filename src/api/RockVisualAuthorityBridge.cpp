#include "api/RockVisualAuthorityBridge.h"

#include "api/ApiTransform.h"
#include "api/RockApiClient.h"

#include <atomic>

namespace paper::frik_visual_authority
{
    namespace
    {
        struct Publication
        {
            RE::NiTransform world{};
            FingerLocalTransformOverride fingers{};
            int priority{ 0 };
            bool worldValid{ false };
            bool fingersValid{ false };
        };

        std::array<Publication, 2> s_publications{};
        std::atomic<bool> s_skeletonReady{ false };

        [[nodiscard]] constexpr std::size_t handIndex(const Hand hand)
        {
            return hand == Hand::Left ? 1u : 0u;
        }

        [[nodiscard]] constexpr rock::provider::RockProviderHand providerHand(
            const Hand hand)
        {
            return hand == Hand::Left ?
                rock::provider::RockProviderHand::Left :
                rock::provider::RockProviderHand::Right;
        }

        [[nodiscard]] bool publish(const Hand hand)
        {
            const auto& source = s_publications[handIndex(hand)];
            if (!source.worldValid && !source.fingersValid) {
                rockApiClient().clearHandVisualAuthority(providerHand(hand));
                return true;
            }

            rock::provider::RockProviderHandVisualAuthorityRequestV1 request{};
            request.hand = providerHand(hand);
            request.priority = source.priority;
            if (source.worldValid) {
                request.flags |= static_cast<std::uint32_t>(
                    rock::provider::RockProviderHandVisualAuthorityFlagV1::WorldTransform);
                api_transform::fromNi(source.world, request.worldTransform);
            }
            if (source.fingersValid && source.fingers.enabledMask != 0) {
                request.flags |= static_cast<std::uint32_t>(
                    rock::provider::RockProviderHandVisualAuthorityFlagV1::FingerLocalTransforms);
                request.fingerLocalTransformMask = source.fingers.enabledMask;
                for (std::size_t index = 0; index < source.fingers.localTransforms.size(); ++index) {
                    const auto bit = static_cast<std::uint16_t>(1u << index);
                    if ((source.fingers.enabledMask & bit) != 0) {
                        api_transform::fromNi(
                            source.fingers.localTransforms[index],
                            request.fingerLocalTransforms[index]);
                    }
                }
            }
            return rockApiClient().setHandVisualAuthority(request);
        }
    }

    void setSkeletonReadyHint(const bool ready)
    {
        s_skeletonReady.store(ready, std::memory_order_release);
        if (!ready) {
            for (auto& publication : s_publications) {
                publication = {};
            }
        }
    }

    bool isSkeletonReadyHint()
    {
        return s_skeletonReady.load(std::memory_order_acquire);
    }

    bool clearHandPose(const char*, const Hand hand)
    {
        auto& publication = s_publications[handIndex(hand)];
        publication.fingers = {};
        publication.fingersValid = false;
        return publish(hand);
    }

    bool setHandPoseCustomWithPriority(
        const char*,
        const Hand hand,
        const HandPoseData&,
        const int priority)
    {
        s_publications[handIndex(hand)].priority = priority;
        return rockApiClient().ready() && isSkeletonReadyHint();
    }

    bool setHandPoseCustomLocalTransformsWithPriority(
        const char*,
        const Hand hand,
        const FingerLocalTransformOverride* overrideData,
        const int priority)
    {
        if (!overrideData || !rockApiClient().ready() || !isSkeletonReadyHint()) {
            return false;
        }
        auto& publication = s_publications[handIndex(hand)];
        publication.fingers = *overrideData;
        publication.fingersValid = overrideData->enabledMask != 0;
        publication.priority = priority;
        // The native runtime publishes the matching world transform next;
        // defer the combined ROCK request so the owner has one atomic tag.
        return true;
    }

    bool applyExternalHandWorldTransform(
        const char*,
        const Hand hand,
        const RE::NiTransform& worldTarget,
        const int priority)
    {
        auto& publication = s_publications[handIndex(hand)];
        publication.world = worldTarget;
        publication.worldValid = true;
        publication.priority = priority;
        return publish(hand);
    }

    bool clearExternalHandWorldTransform(const char*, const Hand hand)
    {
        auto& publication = s_publications[handIndex(hand)];
        publication.world = {};
        publication.worldValid = false;
        return publish(hand);
    }

    RE::NiTransform getHandWorldTransform(const Hand hand)
    {
        RE::NiTransform result{};
        const auto* api = rockApiClient().api();
        if (!api || !api->getHandFrameV1) {
            return result;
        }
        rock::provider::RockProviderHandFrameV1 frame{};
        if (api->getHandFrameV1(providerHand(hand), &frame)) {
            result = api_transform::toNi(frame.transform);
        }
        return result;
    }
}
