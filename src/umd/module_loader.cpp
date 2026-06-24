// SCOPE: UMD-EVOLUTION
// STATUS: PROPOSED
// IMPLEMENTED: NO

#include "umd/module_loader.hpp"

namespace async_task::umd {

void* ModuleLoader::load(const char* path) {
    (void)path;
    return nullptr;
}

void* ModuleLoader::get_kernel(void* module, const char* name) {
    (void)module; (void)name;
    return nullptr;
}

}  // namespace async_task::umd
