// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#pragma once

namespace async_task::umd {

// ELF + CUBIN parser (PoC only — NOT IMPLEMENTED)
class ModuleLoader {
public:
    // Load module from file. Returns nullptr (PoC not implemented).
    void* load(const char* path);

    // Get kernel handle by name. Returns nullptr (PoC not implemented).
    void* get_kernel(void* module, const char* name);
};

}  // namespace async_task::umd
