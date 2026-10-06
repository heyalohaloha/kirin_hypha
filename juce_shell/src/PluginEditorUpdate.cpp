#include "PluginEditor.h"
#include "HyphaPluginFormat.h"

namespace
{
constexpr int automaticUpdateAction = 800;
constexpr int manualUpdateAction = 801;

std::int64_t updateNow()
{
    return juce::Time::currentTimeMillis() / 1000;
}

juce::String updateTime (std::int64_t seconds)
{
    return juce::Time (seconds * 1000).formatted ("%Y-%m-%d %H:%M");
}

juce::String updateStatusText (const hypha::update::Snapshot& state)
{
    using Status = hypha::update::Status;
    if (state.busy) return "Checking for updates...";
    switch (state.status)
    {
        case Status::unchecked: return "No official version has been checked";
        case Status::current: return "This Hypha version is up to date";
        case Status::available: return "New official version available: v" + state.version;
        case Status::development: return "Development build; official version: v" + state.version;
        case Status::failed: return "Update check failed; use the official downloads page";
        case Status::unavailable: return "Official version information is unavailable";
        case Status::withdrawn: return "Previously announced version is no longer offered";
    }
    return {};
}

juce::String updateResultToast (const hypha::update::Snapshot& state)
{
    // Short action answers fit the compact footer; full facts remain in the information menu.
    using Status = hypha::update::Status;
    switch (state.status)
    {
        case Status::failed: return "Update check failed";
        case Status::unavailable: return "Official version unavailable";
        case Status::withdrawn: return "Update offer withdrawn";
        case Status::development: return "Development build";
        case Status::available: return "Hypha v" + state.version + " is available";
        case Status::unchecked:
        case Status::current: return updateStatusText (state);
    }
    return {};
}
}

void KirinHyphaEditor::configureUpdateChecking()
{
    // Standalone render/native tests must not reach the user's saved setting or update storage.
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Undefined) return;
    // Loading preferences and cached facts belongs to the shared worker, never to this editor.
    // OFF is the default. request(false) does not opt in and does not bypass the daily cap.
    updateChecker = processorRef.updateServicesForEditor().get (
        hypha::plugin_format::name (processorRef.wrapperType));
    updateChecker->request (false);
}

void KirinHyphaEditor::refreshUpdateChecking()
{
    const auto now = nowSecs();
    if (updateChecker == nullptr || now < nextUpdateSnapshotAt) return;
    nextUpdateSnapshotAt = now + 1.0;
    // This only copies in-memory facts. No request, filesystem access or network retry here.
    const auto state = updateChecker->snapshot();
    const bool mayPresent = isShowing() && ! informationBlockedByBlind() && now >= toastUntil
        && observatoryView.informationAnchor().isShowing();
    if (updatePreferenceToken != 0 && mayPresent
        && state.preferenceCompletion >= updatePreferenceToken)
    {
        const bool superseded = state.preferenceCompletion > updatePreferenceToken;
        updatePreferenceToken = 0;
        if (superseded)
            showToast ("Update check setting changed in another Hypha window");
        else if (! state.preferenceSaved || state.preferenceValue != requestedUpdatePreference)
            showToast ("Update check preference could not be saved");
        else
            showToast (state.preferenceValue ? "Automatic update checks enabled"
                                            : "Automatic update checks disabled");
        return;
    }
    if (manualUpdateToken != 0 && state.manualCompletion >= manualUpdateToken && mayPresent)
    {
        manualUpdateToken = 0;
        // A manual result is an answer, not a repeated automatic notification. Even an
        // already announced version must answer this explicit action; consume any fresh
        // ticket so the automatic notice does not repeat it in this or another editor.
        if (state.status == hypha::update::Status::available)
            updateChecker->claimNotification();
        showToast (updateResultToast (state));
        return;
    }
    if (mayPresent && observatoryView.feedback().isEmpty()
        && state.status == hypha::update::Status::available
        && updateChecker->claimNotification())
    {
        // Claim only when this visible window can show it: all editors and later launches share
        // the worker's persisted dismissal. Never interrupt Blind, Record errors or action feedback.
        showToast ("Hypha v" + state.version + " available; open PRE / POST for downloads");
    }
}

void KirinHyphaEditor::addUpdateCheckMenu (juce::PopupMenu& menu) const
{
    if (updateChecker == nullptr) return;
    const auto state = updateChecker->snapshot();
    menu.addSeparator();
    menu.addSectionHeader ("Update check");
    menu.addItem (810, updatePreferenceToken != 0 ? "Changing update check preference..."
                                                : updateStatusText (state), false);
    if (state.status == hypha::update::Status::current && state.version.isNotEmpty())
        menu.addItem (811, "Latest official version: v" + state.version, false);
    if (state.checkedAt > 0)
        menu.addItem (812, "Last update check: " + updateTime (state.checkedAt), false);
    menu.addItem (automaticUpdateAction,
                  "Automatic update checks (at most once per 24 hours)", updatePreferenceToken == 0,
                  updatePreferenceToken != 0 ? requestedUpdatePreference : state.enabled);
    const bool dailyLimit = state.nextAttemptAt > updateNow();
    menu.addItem (manualUpdateAction, "Check for updates now", ! state.busy && ! dailyLimit);
    if (dailyLimit)
        menu.addItem (813, "Next permitted check: " + updateTime (state.nextAttemptAt), false);
    menu.addItem (814, "Update requests are limited to once per 24 hours", false);
}

bool KirinHyphaEditor::handleUpdateCheckMenu (int result)
{
    if (result != automaticUpdateAction && result != manualUpdateAction) return false;
    // Recheck after an asynchronous menu, as with every existing information action.
    if (informationBlockedByBlind())
    {
        showToast ("Available after Blind Compare");
        return true;
    }
    if (updateChecker == nullptr) return true;
    const auto state = updateChecker->snapshot();
    if (result == automaticUpdateAction)
    {
        if (updatePreferenceToken == 0)
        {
            requestedUpdatePreference = ! state.enabled;
            updatePreferenceToken = updateChecker->setEnabled (requestedUpdatePreference);
            if (updatePreferenceToken == 0)
            {
                showToast ("Update check preference could not be saved");
                return true;
            }
        }
        showToast ("Changing update check preference...");
        return true;
    }
    if (state.busy)
        showToast ("Update check is already in progress");
    else if (state.nextAttemptAt > updateNow())
        showToast ("Update requests are limited to once per 24 hours");
    else
    {
        manualUpdateToken = updateChecker->request (true);
        showToast (manualUpdateToken != 0 ? "Update check requested"
                                        : "Update check could not be requested");
    }
    return true;
}
