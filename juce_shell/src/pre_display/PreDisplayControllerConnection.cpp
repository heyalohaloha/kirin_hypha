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
        acceptedWorkTitle = request.workTitle;
    }
    notify();
    return true;
}

juce::String Controller::connectedWorkTitle() const
{
    const juce::ScopedLock lock (identityLock);
    return acceptedWorkTitle;
}
}
