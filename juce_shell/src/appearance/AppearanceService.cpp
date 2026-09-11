#include "AppearanceService.h"

#include <utility>

namespace hypha::appearance
{
namespace
{
constexpr auto refreshInterval = std::chrono::seconds (1);

bool samePresentation (const Snapshot& left, const Snapshot& right)
{
    return left.revision == right.revision
        && left.activationSeen == right.activationSeen
        && left.enabled == right.enabled
        && left.noticePending == right.noticePending
        && left.choice == right.choice
        && left.persistent == right.persistent;
}

UserActionState actionState (TransactionState state)
{
    switch (state)
    {
        case TransactionState::persisted:
        case TransactionState::unchanged: return UserActionState::persisted;
        case TransactionState::unsupported: return UserActionState::unsupported;
        case TransactionState::lockBusy:
        case TransactionState::ioFailure: return UserActionState::failed;
    }
    return UserActionState::failed;
}
}

Service::Service (juce::File sharedPluginDataRoot)
    : storage (std::move (sharedPluginDataRoot)), current (makeSnapshot (defaultPreference(), true))
{
}

Service::~Service()
{
    {
        const std::lock_guard<std::mutex> lock (stateMutex);
        exiting = true;
        wake.notify_all();
    }
    if (worker.joinable())
        worker.join();
}

Service& Service::shared()
{
    static Service service;
    return service;
}

void Service::ensureWorkerStarted()
{
    if (! worker.joinable())
        worker = std::thread ([this] { run(); });
}

void Service::editorBecameVisible()
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    ++visibleEditors;
    ensureWorkerStarted();
    refreshRequested = true;
    lastRefreshScheduled = std::chrono::steady_clock::now();
    wake.notify_one();
}

void Service::editorBecameHidden()
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    if (visibleEditors > 0)
        --visibleEditors;
    if (visibleEditors == 0 && ! pendingChoice.has_value() && ! pendingAcknowledgement)
        refreshRequested = false;
}

void Service::pulse()
{
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock (stateMutex);
    if (visibleEditors == 0 || jobActive || refreshRequested
        || now - lastRefreshScheduled < refreshInterval)
        return;
    lastRefreshScheduled = now;
    refreshRequested = true;
    wake.notify_one();
}

Snapshot Service::snapshot() const
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    return current;
}

UserActionReceipt Service::setChoice (Choice choice)
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    if (! current.activationSeen)
        return lastUserAction = { nextActionId++, UserActionState::unsupported };
    ensureWorkerStarted();
    pendingChoice = choice;
    pendingActionId = nextActionId++;
    lastUserAction = { pendingActionId, UserActionState::pending };
    refreshRequested = true;
    wake.notify_one();
    return lastUserAction;
}

UserActionReceipt Service::acknowledgeNotice()
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    if (! current.activationSeen)
        return lastUserAction = { nextActionId++, UserActionState::unsupported };
    ensureWorkerStarted();
    pendingAcknowledgement = true;
    pendingActionId = nextActionId++;
    lastUserAction = { pendingActionId, UserActionState::pending };
    refreshRequested = true;
    wake.notify_one();
    return lastUserAction;
}

UserActionReceipt Service::latestUserAction() const
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    return lastUserAction;
}

void Service::publishSnapshot (const Preference& preference, bool persistent)
{
    auto next = makeSnapshot (preference, persistent, current.generation);
    if (! samePresentation (current, next))
        ++next.generation;
    current = next;
}

void Service::performWork (
    std::optional<Choice> choice, bool acknowledge, std::uint64_t actionId)
{
    const auto activation = storage.readActivation();
    const auto preference = storage.readPreference();
    if (preference.state == StoredState::futureSchema)
    {
        if (actionId != 0)
        {
            const std::lock_guard<std::mutex> lock (stateMutex);
            if (lastUserAction.id <= actionId)
                lastUserAction = { actionId, UserActionState::unsupported };
        }
        return;
    }
    const auto activationValue = activation.state == StoredState::valid
        ? activation.value : std::optional<Activation> {};
    const bool needsActivationProjection = activationValue.has_value()
        && (preference.state != StoredState::valid || ! preference.value->activationSeen);
    const bool needsPreferenceRecovery = preference.state == StoredState::valid
        && preference.fromBackup;

    TransactionResult transaction;
    bool attemptedTransaction = false;
    if (needsActivationProjection || needsPreferenceRecovery || choice.has_value() || acknowledge)
    {
        attemptedTransaction = true;
        transaction = storage.updatePreference (activationValue, choice, acknowledge);
    }

    Preference resolved = preference.value.value_or (defaultPreference());
    bool persistent = preference.state == StoredState::missing
        || preference.state == StoredState::valid;
    if (attemptedTransaction
        && (transaction.state == TransactionState::persisted
            || transaction.state == TransactionState::unchanged))
    {
        resolved = transaction.preference;
        persistent = true;
    }
    else if (activationValue.has_value() && ! resolved.activationSeen)
    {
        resolved.activationSeen = true;
        resolved.firstActivationId = activationValue->id;
        persistent = false;
    }

    const std::lock_guard<std::mutex> lock (stateMutex);
    if (choice.has_value()
        && attemptedTransaction
        && (transaction.state == TransactionState::persisted
            || transaction.state == TransactionState::unchanged))
    {
        volatileChoice.reset();
    }
    if (resolved.revision > volatileAtRevision)
    {
        volatileChoice.reset();
    }
    if (attemptedTransaction
        && transaction.state != TransactionState::persisted
        && transaction.state != TransactionState::unchanged
        && transaction.state != TransactionState::unsupported)
    {
        volatileAtRevision = resolved.revision;
        if (choice.has_value())
            volatileChoice = choice;
    }
    if (volatileChoice.has_value() && resolved.activationSeen)
    {
        resolved.choice = *volatileChoice;
        persistent = false;
    }
    publishSnapshot (resolved, persistent);
    if (actionId != 0 && lastUserAction.id <= actionId)
        lastUserAction = { actionId, attemptedTransaction
            ? actionState (transaction.state) : UserActionState::failed };
}

void Service::run()
{
    for (;;)
    {
        std::optional<Choice> choice;
        bool acknowledge = false;
        std::uint64_t actionId = 0;
        {
            std::unique_lock<std::mutex> lock (stateMutex);
            wake.wait (lock, [this]
            {
                return exiting || refreshRequested || pendingChoice.has_value()
                    || pendingAcknowledgement;
            });
            if (exiting)
                return;
            if (visibleEditors == 0 && ! pendingChoice.has_value() && ! pendingAcknowledgement)
            {
                refreshRequested = false;
                continue;
            }
            choice = pendingChoice;
            acknowledge = pendingAcknowledgement;
            actionId = pendingActionId;
            pendingChoice.reset();
            pendingAcknowledgement = false;
            pendingActionId = 0;
            refreshRequested = false;
            jobActive = true;
        }
        performWork (choice, acknowledge, actionId);
        {
            const std::lock_guard<std::mutex> lock (stateMutex);
            jobActive = false;
        }
    }
}

void Service::refreshSynchronouslyForTest()
{
    performWork (std::nullopt, false, 0);
}
}
