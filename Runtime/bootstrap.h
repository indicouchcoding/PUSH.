#pragma once
#include <cstdint>

namespace dingosdk::runtime {
// Installs only process/platform guards that do not depend on Skate's native
// function addresses. Safe for a new executable while its gameplay contracts
// are still being ported.
bool initialize_compatibility_bootstrap(std::uintptr_t base);

// Call after image validation, before executable entry, outside the loader lock.
// Failure leaves initialization incomplete; the launcher must abort startup.
bool initialize_bootstrap(std::uintptr_t base);
}
