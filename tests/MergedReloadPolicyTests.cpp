#include "compat/MergedReloadPolicy.h"

#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

int main()
{
    namespace policy = paper::merged_reload_policy;
    int failures = 0;
    const auto expect = [&](const bool passed, const char* label) {
        if (!passed) { std::fprintf(stderr, "%s\n", label); ++failures; }
    };
    policy::Path result{};
    expect(policy::normalReloadPath("Animations\\44Pistol\\WPNReloadReserve.hkt", result) &&
        std::string_view(result.data()) == "Animations\\44Pistol\\WPNReload.hkt",
        "Shared graph reserve uses its matching normal slot");
    expect(policy::normalReloadPath("Animations/CustomGun/Magazine/WPNReloadReserve.hkx", result) &&
        std::string_view(result.data()) == "Animations/CustomGun/Magazine/WPNReload.hkx",
        "Custom weapon directories, variants and extension are preserved");
    expect(policy::normalReloadPath("animations/CUSTOM/WPNRELOADRESERVE.HKT", result) &&
        std::string_view(result.data()) == "animations/CUSTOM/WPNReload.hkT",
        "Case insensitive matching retains the authored parent path");
    for (const auto unrelated : { "", "WPNReload.hkt", "WPNReloadLoop.hkt", "WPNReloadReserveExtra.hkt",
             "WPNReloadReserve.hkt.bak", "WPNReloadReserve.hkt/Fire.hkt", "WPNReloadReserve.txt" }) {
        expect(!policy::normalReloadPath(unrelated, result), "Unrelated/native/custom actions are not rewritten");
    }
    const std::string oversized(policy::kPathCapacity, 'a');
    expect(!policy::normalReloadPath(oversized + "/WPNReloadReserve.hkt", result),
        "An oversized path fails closed instead of matching a truncated name");
    expect(policy::playableReload(2.166667f, 94), "Ordinary loaded reload is eligible");
    expect(!policy::playableReload(0.0f, 94), "Native zero-duration placeholder is rejected");
    expect(!policy::playableReload(-1.0f, 94), "Negative duration is rejected");
    expect(!policy::playableReload(std::numeric_limits<float>::quiet_NaN(), 94), "NaN duration is rejected");
    expect(!policy::playableReload(std::numeric_limits<float>::infinity(), 94), "Infinite duration is rejected");
    expect(!policy::playableReload(2.0f, 0), "No animation tracks cannot repair a reload");
    return failures == 0 ? 0 : 1;
}
