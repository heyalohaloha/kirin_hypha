#pragma once
#include "ValidationStorageSandbox.h"
#include "../src/reference_audition/ReferenceRuntimeV2Repository.h"

class ReferenceValidationStorageSandbox : public ValidationStorageSandbox
{
public:
    ReferenceValidationStorageSandbox()
    {
        referenceRoot = directory().getChildFile ("reference-fixture");
        if (referenceRoot.createDirectory().failed())
            throw std::runtime_error ("could not create Reference fixture");
        hypha::reference_audition::RuntimeV2Repository::setTransportRootForTesting (referenceRoot);
    }
    ~ReferenceValidationStorageSandbox()
    {
        hypha::reference_audition::RuntimeV2Repository::setTransportRootForTesting ({});
    }
private:
    juce::File referenceRoot;
};
