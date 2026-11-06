#pragma once
#include <cstdint>

#if defined(_WIN32)
 #include <windows.h>
#else
 #include <dlfcn.h>
 #if defined(__APPLE__)
  #include <mach-o/loader.h>
 #else
  #include <link.h>
 #endif
#endif

namespace hypha::reference_audition::receipt_detail
{
// A detached store may return after the host closes its last plugin instance. Retain the
// code, including its static data and thread-exit destructors, until process exit. One
// pin per loaded image; never one per receipt or instance. No call runs on the audio thread.
static bool retainModule() noexcept
{
    static int anchor;
    static const bool retained = []
    {
       #if defined(_WIN32)
        HMODULE module = nullptr;
        return GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                  reinterpret_cast<LPCWSTR> (&anchor), &module) != 0;
       #else
        Dl_info image {};
        if (dladdr (&anchor, &image) == 0 || image.dli_fname == nullptr) return false;
       #if defined(__APPLE__)
        if (static_cast<const mach_header*> (image.dli_fbase)->filetype == MH_EXECUTE) return true;
       #else
        struct Executable { const void* address; bool contains = false; } executable { &anchor };
        dl_iterate_phdr ([] (dl_phdr_info* info, size_t, void* context)
        {
            if (info->dlpi_name != nullptr && info->dlpi_name[0] != '\0') return 0;
            auto& main = *static_cast<Executable*> (context);
            const auto address = reinterpret_cast<std::uintptr_t> (main.address);
            for (int index = 0; index < info->dlpi_phnum; ++index)
            {
                const auto& segment = info->dlpi_phdr[index];
                const auto begin = info->dlpi_addr + segment.p_vaddr;
                if (segment.p_type == PT_LOAD && address >= begin && address - begin < segment.p_memsz)
                    main.contains = true;
            }
            return 1;
        }, &executable);
        if (executable.contains) return true;
       #endif
        // Keep this single loader reference as well as NODELETE. Releasing it on a job's
        // completion would still race the thread's return and destruction of its captures.
        return dlopen (image.dli_fname, RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD | RTLD_NODELETE) != nullptr;
       #endif
    }();
    return retained;
}
}
