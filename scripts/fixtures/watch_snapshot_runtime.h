#pragma once
#include "HyphaObservationEquality.h"
#include "HyphaObservatoryContract.h"
#include "kirin_hypha_level_snapshot.h"
#include "kirin_hypha_navigation_snapshot_ffi.h"
#include <array>
#include <cassert>
#include <vector>
#include <utility>

// External UI, settings and processor ports for the complete shipping methods.
// No Watch assignment, publication decision, or display equality is copied here.
namespace juce {
struct String { bool isNotEmpty() const { return false; } };
template <typename T> T jlimit(T low, T high, T value) { return std::max(low, std::min(high, value)); }
template <typename T> T jmin(T a, T b) { return std::min(a, b); }
}
namespace hypha::meter_context { inline bool drumAttackAvailable(int) { return true; } }
namespace hypha::comparison_presentation { inline juce::String spectrumStatusText(int, int) { return {}; } }
using Domain = hypha::observatory::Domain;
using Target = hypha::observatory::ObservationTarget;
namespace observation_equality = hypha::observation_equality;
struct Bounds { int getWidth() const { return 300; } bool operator!=(const Bounds&) const { return false; } };
struct View {
    KirinWatchDisplay watchDisplay {}; bool watchDisplayAvailable = false;
    Domain selectedDomain = Domain::level; Target selectedTarget = Target::absolute;
    int bodyArea = 0, paints = 0;
    void repaint(int) { ++paints; }
    void setWatchDisplay(const KirinWatchDisplay&, bool);
    void setSurroundMeasurementOnly(bool) {} void setMeasurementFormatHeld(bool) {}
    bool setHybridVuOnRecordEnabled(bool) { return false; } bool setHostRecording(bool) { return false; }
    void setVuCalibration(int, bool) {} bool hybridVuVisible() const { return false; } void toFront(bool) {}
    bool isReferenceOwned() const { return false; } void setReferenceOwned(bool) {}
    Target target() const { return selectedTarget; } Target preferredTarget() const { return selectedTarget; }
    void setTarget(Target value) { selectedTarget = value; }
    hypha::observatory::TimeRange selectedTimeRange() const { return hypha::observatory::TimeRange::seconds30; }
    void setTimeRange(hypha::observatory::TimeRange) {}
    int meterContext() const { return 0; } void setMeterContext(int) {}
    int scaleMode() const { return 0; } void setScaleMode(int) {}
    KirinObservatoryFrame frameForPoll() const { return {}; }
    bool fullCockpit() const { return false; } Bounds bodyBounds() const { return {}; }
    uint64_t levelChainRevision(bool) const { return 0; }
    template <typename... T> void setLevelObservation(T&&...) {}
    void setObservatoryFrame(const KirinObservatoryFrame&, bool) {} void clearChainObservation() {}
    void setShortTermLoudness(bool) {} void setSessionCoverage(const KirinMeterSessionV2&) {}
    struct Capabilities { bool historyRange = false; }; Capabilities capabilities() const { return {}; }
    void setHistory(std::vector<KirinMeterHistoryEntry>) {} void setConnection(int, int, int) {} void clearGuide() {}
};
struct Processor {
    KirinWatchDisplay next {}; bool available = false, levelReady = false;
    Target preferredTarget = Target::absolute;
    bool pollWatchDisplay(KirinWatchDisplay& value) { value = next; return available; }
    template <typename... T> bool pollLevelSnapshot(T&&...) { return levelReady; }
    bool pollObservatoryFrame(KirinObservatoryFrame&) { return true; }
    bool surroundMeasurementOnly() const { return false; } bool measurementFormatHeld() const { return false; }
    uint64_t hostProcessHeartbeatValue() const { return 0; } bool isHostRecording() const { return false; }
    bool hybridVuOnRecordPreference() const { return false; } int refreshHybridVuCalibration() { return 0; }
    juce::String hybridVuCalibrationScope() const { return {}; }
    bool stereoWorkflowsSupported() const { return true; } bool licenseIsOs() const { return false; }
    uint8_t observatoryDomainPreference() const { return hypha::observatory::stateValue(Domain::level); }
    uint8_t observatoryTargetPreference() const { return hypha::observatory::stateValue(preferredTarget); }
    int observatoryTimeRangePreference() const { return 0; } int meterContextPreference() const { return 0; }
    int scaleModePreference() const { return 0; } int spectrumSizePreference() const { return 0; }
    int observatoryEditorSizePreference() const { return 0; } bool useShortTermLoudness() const { return false; }
    int pairStatus() const { return 0; } juce::String pairDisplayName() const { return {}; }
    juce::String preName() const { return {}; }
    template <typename... T> bool pollMeterDeltaHistory(T&&...) { return false; }
    bool localBlindProductSupported() const { return true; }
};
namespace hypha::snapshots {
struct Source { explicit Source(Processor&) {} uint32_t session(KirinMeterSessionV2&) { return KIRIN_SNAPSHOT_BUSY; } };
}
inline int observatoryPairText(bool, int, juce::String) { return 0; }
inline int observatoryPairColour(bool, int) { return 0; }
inline int observatoryConnectionState(bool, int) { return 0; }
struct SpectrumView { void setAbsoluteObservation(bool) {} void setComparisonStatus(juce::String) {} };
struct KirinHyphaEditor {
    Processor processorRef; View observatoryView; SpectrumView spectrumView;
    KirinWatchDisplay observatoryWatchDisplay {}; bool haveObservatoryWatchDisplay = false;
    Domain observatoryDomain = Domain::level; bool isPost = !KIRIN_HYPHA_PRE_DISPLAY;
    bool localBlindOpen = false, liveBlindOpen = false;
    double localBlindPresentationAt = 0, observedHostProcessHeartbeatAt = 0;
    uint64_t observedHostProcessHeartbeat = 0; size_t observatorySizeIndex = 0;
    enum class AnalysisPage { spectrum, attack, meters }; AnalysisPage analysisPage = AnalysisPage::meters;
    std::array<KirinChainPoint, KIRIN_CHAIN_CAPACITY> chainPoints {};
    struct Constrainer { hypha::observatory::EditorSize allowedSize(hypha::observatory::EditorSize value) { return value; } } sizeConstrainer;
    void refreshWatchSnapshot(); void refreshObservatory();
    void refreshLocalBlindProduct() {} void syncAnalysisDemand() {} void refreshLiveBlind() {}
    void setObservatoryDomain(Domain value) { observatoryDomain = value; observatoryView.selectedDomain = value; }
    double nowSecs() const { return 1; } void resized() {} void configureSpectrumAnalysis() {}
    void setAnalysisPage(AnalysisPage value) { analysisPage = value; } void updateTimePageNavigation() {}
    void updateResizeLimits() {} int getWidth() const { return 300; } int getHeight() const { return 200; }
    void setSize(int, int) {} void refreshComparisonNotice(const KirinObservatoryFrame&) {}
    void refreshReferenceAudition(const KirinObservatoryFrame&, bool) {} void refreshTimeSnapshot() {}
};
