// Class-scope declarations included by PluginProcessor.h; not a standalone header.
// Live PRE/POST compare (stage 1): explicit POST session control and status for the editor.
// A press that waited passes the restore and pair ticket of its press; a new press takes the latest one.
hypha::live_compare::StartResult startLiveCompare (std::optional<std::uint64_t> pressedTicket = std::nullopt);
// LISTEN（reuseSession なら LIVE BLIND）を始めてよいか。出力の持ち主の表（outputDecision）の答え。
hypha::live_compare::StartResult liveCompareAdmission (bool reuseSession) const noexcept;
// INV-LC16：窓を閉じた間の AUTO（message thread だけ）。開き直した窓が同じ session なら続ける。
hypha::live_compare::ProcessorState::AutoRest& liveCompareAutoRest() noexcept { return liveCompare.autoRest; }
void stopLiveCompare (hypha::live_compare::RecoveryReason reason = hypha::live_compare::RecoveryReason::none);
void invalidateLiveComparePair() noexcept;
// A restore or a pair change moves this ticket. A press that waits (Keep, LISTEN after RETURN) keeps the
// ticket of its press and never starts across a change of it, even back to the same PRE id and name.
std::uint64_t restoreOrPairTicket() const noexcept { return liveCompare.authority.ticket(); }
void selectLiveComparePre (bool pre) noexcept;
void setLiveCompareGain (float linear) noexcept;
hypha::live_compare::MatchResult measureLiveCompare();
void requestLiveCompareMatch();
void serviceLiveCompareMatch();
bool takeLiveCompareMatch (hypha::live_compare::MatchPlan&, bool& applied);
hypha::live_compare::MatchApplication applyLiveCompareMatch (const hypha::live_compare::MatchPlan&, hypha::live_compare::MatchChoice);
bool followLiveCompareGain (double preDb); // INV-LC16: AUTO moves PRE only
void kirinHostDelayCompensationStateChanged (bool enabled) override; // INV-LC8, AAX only
void kirinHostInstanceGroup (juce::uint64 group, bool valid) override; // INV-LC9, AAX only
void returnLiveComparePostToNormal() noexcept;
bool takeLiveCompareGuardTrip() noexcept;
hypha::live_compare::OffsetEstimate measureLiveCompareOffset();
void serviceLiveContentTiming();
hypha::live_compare::OffsetMonitor::Step liveContentTimingObservation() const noexcept;
bool holdLiveCompareForContentJump (const hypha::live_compare::OffsetEstimate&) noexcept;
void kirinHostTimelineReset (bool transportKnown, bool playing) override;
std::uint32_t liveComparePlaybackRun() const noexcept;
hypha::live_compare::LivePinResult pinLiveCompareForBlind (hypha::meter_context::MeterContext);
hypha::live_compare::Status liveCompareStatus() const noexcept;
bool analysisPairingLoopBlocked() const noexcept;
hypha::live_compare::LoopPairingBlock analysisPairingLoopBlock() const noexcept;
bool liveCompareSupported() const noexcept;
hypha::live_compare::ChainTimingView chainTimingView() const noexcept; // POST, display only
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
