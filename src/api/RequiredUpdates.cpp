#include "api/RequiredUpdates.h"

#include "PaperLog.h"

#include <PAPER/Provider.h>
#include <RPS/Runtime/RequiredUpdate.h>

#include <string>

namespace paper::required_updates
{
    namespace
    {
        namespace required_update = RPS::Runtime::RequiredUpdate;

        std::wstring paperVersion()
        {
            return std::wstring(Version::NAME.begin(), Version::NAME.end());
        }

        // DLL file names for the log; characters outside ASCII become '?'.
        std::string narrowForLog(const std::wstring& text)
        {
            std::string result;
            result.reserve(text.size());
            for (const wchar_t c : text)
                result.push_back(c > 0 && c < 0x80 ? static_cast<char>(c) : '?');
            return result;
        }

        struct Caller
        {
            std::wstring dll;      // for the error box
            std::string dllForLog;
        };

        Caller identify(const void* callerAddress)
        {
            Caller caller{ required_update::moduleFileNameAt(callerAddress), {} };
            caller.dllForLog = caller.dll.empty() ? std::string{ "an unidentified module" } : narrowForLog(caller.dll);
            if (caller.dll.empty())
                caller.dll = L"A mod (see PAPER.log)";
            return caller;
        }

        void reportConsumerOlder(const Caller& caller)
        {
            required_update::report(caller.dll + L" was built for an older PAPER and does not work with PAPER " +
                paperVersion() + L". Update it or remove it.");
        }

        // Consumers request the exact major, minor and table size their headers
        // declare (PAPER/Client.h), so every version refusal is a real mismatch.
        bool isVersionRefusal(const rock::api::Status status) noexcept
        {
            using rock::api::Status;
            return status == Status::UnknownInterface || status == Status::UnsupportedMajor ||
                status == Status::UnsupportedMinor || status == Status::TableTooSmall || status == Status::InvalidSize;
        }

        void reportRockMismatch()
        {
            required_update::report(L"PAPER " + paperVersion() +
                L" does not work with the installed ROCK: they were built for different ROCK versions. Install "
                L"matching versions of ROCK and PAPER.");
        }

        void flushLog() noexcept
        {
            try {
                if (logger::instance)
                    logger::instance->flush();
            } catch (...) {}
        }
    }

    void noteDiscoveryRefusal(const api::Status status, const api::InterfaceId id, const std::uint32_t requestedMajor,
        const void* callerAddress) noexcept
    {
        using api::Status;
        bool consumerOlder = false;
        switch (status) {
        case Status::UnsupportedMajor:
        case Status::UnknownInterface:
            consumerOlder = requestedMajor < api::provider::kMajor;
            break;
        case Status::UnsupportedMinor:
        case Status::TableTooSmall:
            break;
        default:
            return;  // not a version refusal
        }
        if (!required_update::firstFromCaller(callerAddress))
            return;
        try {
            const auto caller = identify(callerAddress);
            if (consumerOlder) {
                PAPER_LOG_CRITICAL(Api,
                    "{} asked for interface {} major {}: it was built for an older PAPER. The game stops at the main "
                    "menu until it is updated or removed.",
                    caller.dllForLog, static_cast<std::uint32_t>(id), requestedMajor);
                reportConsumerOlder(caller);
            } else {
                PAPER_LOG_CRITICAL(Api,
                    "{} asked for interface {} major {} (status {}): it needs a newer PAPER than {}. The game stops at "
                    "the main menu until PAPER is updated.",
                    caller.dllForLog, static_cast<std::uint32_t>(id), requestedMajor, static_cast<std::uint32_t>(status),
                    Version::NAME);
                required_update::report(
                    caller.dll + L" needs a newer PAPER than " + paperVersion() + L". Update PAPER.");
            }
        } catch (...) {}
    }

    void noteRetiredExport(const char* exportName, const void* callerAddress) noexcept
    {
        if (!required_update::firstFromCaller(callerAddress))
            return;
        try {
            const auto caller = identify(callerAddress);
            PAPER_LOG_CRITICAL(Api,
                "{} called {}, PAPER's retired pre-2.0 API: it was built for an older PAPER. The game stops at the "
                "main menu until it is updated or removed.",
                caller.dllForLog, exportName ? exportName : "a retired export");
            reportConsumerOlder(caller);
        } catch (...) {}
    }

    void noteRockMissingDiscovery() noexcept
    {
        try {
            PAPER_LOG_CRITICAL(Api,
                "ROCK.dll does not export ROCKAPI_QueryInterfaceV1: it is older than PAPER supports. The game stops at "
                "the main menu until ROCK and PAPER match.");
            reportRockMismatch();
        } catch (...) {}
    }

    void noteRockRefusal(const rock::api::Status status, const char* interfaceName) noexcept
    {
        if (!isVersionRefusal(status))
            return;
        try {
            PAPER_LOG_CRITICAL(Api,
                "ROCK refused PAPER's {} request with status {}: ROCK and PAPER were built for different ROCK API "
                "versions. The game stops at the main menu until they match.",
                interfaceName ? interfaceName : "discovery", static_cast<std::uint32_t>(status));
            reportRockMismatch();
        } catch (...) {}
    }

    void watch() noexcept
    {
        if (!required_update::watchMainMenu(L"PAPER: update required", &flushLog)) {
            try {
                PAPER_LOG_ERROR(Api, "The UI is unavailable; version mismatches cannot stop the game at the main menu");
            } catch (...) {}
        }
    }
}
