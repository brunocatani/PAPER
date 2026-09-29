#pragma once
#include "AttachmentBindingPolicy.h"

namespace paper::attachment_bindings
{
    // Called only at BeforeRock with a coherent equipped-weapon scene lease.
    // Identity contains comparison values; no engine object is retained.
    void reconcile(const policy::Identity& identity) noexcept;
    void reset() noexcept;
}
