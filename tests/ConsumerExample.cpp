#include <PAPER/Client.h>
#include <PAPER/PhysicalReload.h>

// A mod that drives reloads with its own hands: it reads PAPER's runtime
// state and leases physical reload control of the equipped weapon. Resolve
// the query with GetProcAddress(GetModuleHandleW(L"PAPER.dll"),
// paper::api::kQueryExportName).
namespace consumer_example {
    class PhysicalReloadOwner {
        paper::api::Client _client;
        const paper::api::physical_reload::Api* _reload{};
    public:
        paper::api::Status connect(paper::api::QueryInterfaceV1 query) {
            constexpr auto capabilities=static_cast<std::uint32_t>(paper::api::ConsumerCapability::RuntimeState)|
                static_cast<std::uint32_t>(paper::api::ConsumerCapability::AnimationAuthority);
            auto status=_client.connect(query,"Example reload owner",capabilities);
            if (status!=paper::api::Status::Ok) return status;
            status=_client.acquire(_reload);
            if (status!=paper::api::Status::Ok) (void)_client.close();
            return status;
        }
        // Each frame of physical control, with the ROCK frame the hands acted on.
        paper::api::Status renew(std::uint32_t weaponFormId, std::uint64_t rockFrame, bool manualCycle) {
            if (!_reload) return paper::api::Status::NotReady;
            paper::api::physical_reload::Request request;
            request.weaponFormId=weaponFormId;
            request.observedFrame=rockFrame;
            if (manualCycle) request.flags=static_cast<std::uint32_t>(paper::api::physical_reload::Flag::ManualCycle);
            return _reload->acquire(_client.owner(),&request);
        }
        paper::api::Status release() {
            return _reload ? _reload->clear(_client.owner()) : paper::api::Status::Ok;
        }
        paper::api::Status close() {
            const auto status=_client.close();
            if (status==paper::api::Status::Ok) _reload=nullptr;
            return status;
        }
    };
}
