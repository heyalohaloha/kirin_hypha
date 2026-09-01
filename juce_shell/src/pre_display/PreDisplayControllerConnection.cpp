#include "PreDisplayController.h"

namespace hypha::pre_display
{
ConnectionRequest Controller::pendingConnection() const
{
    const juce::ScopedLock lock (connectionLock);
    return connectionRequest;
}

bool Controller::acceptPendingConnection()
{
    ConnectionRequest request;
    {
        const juce::ScopedLock lock (connectionLock);
        request = connectionRequest;
    }
    if (! request.validAt (juce::Time::currentTimeMillis()))
        return false;
    {
        const juce::ScopedLock lock (identityLock);
        if (! configured)
            return false;
        identity.workId = request.workId;
        identity.bindingId = request.bindingId;
        acceptedWorkReference.targetRole = identity.role;
        acceptedWorkReference.workId = identity.workId;
        acceptedWorkReference.bindingId = identity.bindingId;
        acceptedWorkReference.runtimeInstanceId = identity.runtimeInstanceId;
        acceptedWorkReference.displayTitle = request.workTitle;
    }
    notify();
    return true;
}

WorkReference Controller::connectedWorkReference() const
{
    const juce::ScopedLock lock (identityLock);
    return acceptedWorkReference;
}

juce::String Controller::connectedWorkTitle() const
{
    const juce::ScopedLock lock (identityLock);
    return acceptedWorkReference.displayTitle;
}
}
