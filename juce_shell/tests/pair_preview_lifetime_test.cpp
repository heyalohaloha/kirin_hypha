#include "ValidationStorageSandbox.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif
namespace {
void require(bool value,const char* why) { if(!value) { std::cerr<<why<<'\n'; std::exit(1); } }
bool mapped(const juce::File& file) {
 #if JUCE_WINDOWS
    return GetModuleHandleW(file.getFullPathName().toWideCharPointer())!=nullptr;
 #else
    auto* handle=dlopen(file.getFullPathName().toRawUTF8(),RTLD_LAZY|RTLD_NOLOAD);
    if(handle) dlclose(handle);
    return handle!=nullptr;
 #endif
}
}
int main(int argc,char** argv)
{
    require(argc==2,"module path required"); ValidationStorageSandbox sandbox;
    // dyld retains images containing Rust TLV data; this counter must outlive retained images.
    static std::atomic<int> unloaded {0};
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("hypha-module-probe",{},false);
    require(root.createDirectory().wasOk(),"isolated module folder");
    std::array<juce::File,3> modules;
    for(int i=0;i<3;++i) {
        modules[size_t(i)]=root.getChildFile(juce::String(i)+juce::File(argv[1]).getFileExtension());
        require(juce::File(argv[1]).copyFileTo(modules[size_t(i)]),"three independent loaded modules");
    }
    for(int cycle=0;cycle<12;++cycle) {
        std::array<juce::DynamicLibrary,3> libraries;
        for(size_t i=0;i<3;++i) {
            require(libraries[i].open(modules[i].getFullPathName()),"load module");
            using Start=bool(*)(std::atomic<int>*);
            auto start=reinterpret_cast<Start>(libraries[i].getFunction("startPairPreviewProbe"));
            require(start && start(&unloaded),"submit after engine destruction");
            if(cycle%3==1) {
                auto cancel=reinterpret_cast<void(*)()>(libraries[i].getFunction("cancelPairPreviewProbe"));
                require(cancel!=nullptr,"cancel export"); cancel();
            }
        }
        if(cycle%3==2) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for(auto& library:libraries) {
            auto cancel=reinterpret_cast<void(*)()>(library.getFunction("cancelPairPreviewProbe"));
            require(cancel!=nullptr,"instance retirement export"); cancel();
           #if JUCE_MAC
            // Exercise the exact native unload guard even when dyld retains this TLV image.
            auto drain=reinterpret_cast<void(*)()>(library.getFunction("drainPairPreviewProbe"));
            require(drain!=nullptr,"non-RT module drain export"); drain();
           #endif
            library.close();
        }
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
       #if !JUCE_MAC
        while(unloaded.load()!=(cycle+1)*3) {
            require(std::chrono::steady_clock::now()<until,"module callbacks drained before unload");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for(const auto& module:modules) {
            while(mapped(module)) {
                require(std::chrono::steady_clock::now()<until,"module really unmaps after callback retirement");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
       #else
        (void)until;
        for(const auto& module:modules) require(mapped(module),"dyld retains TLV-bearing Rust module");
       #endif
    }
    require(root.deleteRecursively(),"module fixture cleanup");
   #if JUCE_MAC
    std::cout<<"Pair preview lifetime: 36 non-RT guard drains and close/reopen cycles, 3 independent modules PASS; actual unmap SKIP (dyld retains Rust TLV images)\n";
   #else
    std::cout<<"Pair preview lifetime: 36 actual module unloads, 3 independent modules, pending/cancelled/idle, engine already destroyed PASS\n";
   #endif
}
