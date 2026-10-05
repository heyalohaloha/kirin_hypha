// Class-scope declarations included by PluginProcessor.h; not a standalone header.
// Live PRE/POST compare (stage 1): explicit POST session control and status for the editor.
hypha::live_compare::StartResult startLiveCompare();
// LISTEN（reuseSession なら LIVE BLIND）を始めてよいか。出力の持ち主の表（outputDecision）の答え。
hypha::live_compare::StartResult liveCompareAdmission (bool reuseSession) const noexcept;
void stopLiveCompare (hypha::live_compare::RecoveryReason reason = hypha::live_compare::RecoveryReason::none);
void selectLiveComparePre (bool pre) noexcept;
void setLiveCompareGain (float linear) noexcept;
hypha::live_compare::MatchResult measureLiveCompare();
hypha::live_compare::MatchApplication applyLiveCompareMatch (const hypha::live_compare::MatchPlan&, hypha::live_compare::MatchChoice);
bool followLiveCompareGain (double preDb); // INV-LC16: AUTO moves PRE only
void kirinHostDelayCompensationStateChanged (bool enabled) override; // INV-LC8, AAX only
void kirinHostInstanceGroup (juce::uint64 group, bool valid) override; // INV-LC9, AAX only
void returnLiveComparePostToNormal() noexcept;
bool takeLiveCompareGuardTrip() noexcept;
hypha::live_compare::OffsetEstimate measureLiveCompareOffset();
void holdLiveCompareForContentJump (std::int64_t measuredLagFrames) noexcept;
std::uint32_t liveComparePlaybackRun() const noexcept;
hypha::live_compare::LivePinResult pinLiveCompareForBlind (hypha::meter_context::MeterContext);
hypha::live_compare::Status liveCompareStatus() const noexcept;
bool liveCompareSupported() const noexcept;
bool aaxMultiMonoMember() const noexcept;
bool takeLiveComparePreWait() noexcept;
bool serviceLiveCompare();
void finishLiveCompare();
bool liveCompareNeedsService() const noexcept;
hypha::live_compare::StartResult beginLiveBlind();
void serviceLiveBlind();
bool approveLiveBlindMatch (std::uint64_t generation);
hypha::live_compare::LiveBlindStatus liveBlindStatus() const;
bool selectLiveBlind (int stimulus);
bool revealLiveBlind();
void closeLiveBlind();
