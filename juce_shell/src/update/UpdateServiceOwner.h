#pragma once
#include "UpdateChecker.h"

namespace hypha::update
{
// A processor's non-realtime lifetime, not an audio operation. The editor lazily
// acquires the service here. Closing the editor only releases its own reference;
// destroying the last processor drains the worker BEFORE the host unloads its DLL.
// The module's registry holds weak references only and never joins during CRT detach.
class ServiceOwner
{
public:
    using Factory = std::function<std::shared_ptr<Checker> (const juce::String&)>;
    explicit ServiceOwner (Factory factory = Checker::shared) : create (std::move (factory)) {}
    std::shared_ptr<Checker> get (const juce::String& format)
    {
        if (! service) service = create (format);
        return service;
    }
    ServiceOwner (const ServiceOwner&) = delete;
    ServiceOwner& operator= (const ServiceOwner&) = delete;
private:
    Factory create;
    std::shared_ptr<Checker> service;
};
}
