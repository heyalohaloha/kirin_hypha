// Class-scope declarations included by PluginProcessor.h; not a standalone header.
// Live PRE/POST compare (stage 1): explicit POST session control and status for the editor.
// A press that waited passes the restore and pair ticket of its press; a new press takes the latest one.
hypha::live_compare::StartResult startLiveCompare (std::optional<std::uint64_t> pressedTicket = std::nullopt);
// LISTEN（reuseSession なら LIVE BLIND）を始めてよいか。出力の持ち主の表（outputDecision）の答え。
hypha::live_compare::StartResult liveCompareAdmission (bool reuseSession) const noexcept;
// INV-LC16：窓を閉じた間の AUTO（message thread だけ）。開き直した窓が同じ session なら続ける。
hypha::live_compare::ProcessorState::AutoRest& liveCompareAutoRest() noexcept { return liveCompare.autoRest; }
// No DAW tells a plug-in that a realtime bounce runs: true once per plug-in, at the first started LISTEN, BLIND or A/B.
bool takeRealtimeBounceNotice() noexcept { return ! std::exchange (liveCompare.realtimeBounceNoticeGiven, true); }
// LISTEN の開始と終わりだけで進む番号。ループの折り返しや再生の途切れでは変わらない（sessionGeneration と別）。
std::uint64_t liveCompareListenSession() const noexcept { return liveCompare.listenSession.load (std::memory_order_acquire); }
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
bool alignLiveCompare (const hypha::live_compare::OffsetMonitor::Step&, const hypha::live_compare::OffsetEstimate&) noexcept;
bool liveCompareAlignPending() const noexcept;
bool alignConfirmationExpired (const hypha::live_compare::Status&, std::uint64_t now) noexcept;
bool candidateAlignUnconfirmed() const noexcept;
bool liveCompareAutoAlign() const noexcept; // INV-LC28, on unless the user turned it off
void setLiveCompareAutoAlign (bool enabled) noexcept;
std::int64_t liveCompareAlignFrames() const noexcept; // frames PRE is read later than K alone says
double liveCompareAlignMs() const noexcept;           // the same, at the rate the ring counts in
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
hypha::live_compare::StartResult beginLiveBlind (std::optional<std::uint64_t> pressedTicket = std::nullopt);
void serviceLiveBlind();
bool approveLiveBlindMatch (std::uint64_t generation);
hypha::live_compare::LiveBlindStatus liveBlindStatus() const;
bool selectLiveBlind (int stimulus);
bool revealLiveBlind();
void closeLiveBlind();

// Explicit, unsaved A/B selection; the normal PRE pair is never changed.
hypha::live_compare::StartResult startCandidateCompare (const juce::String& pre2, bool placementConfirmed,
    std::optional<std::uint64_t> pressedTicket = std::nullopt);
bool candidateReturning() const noexcept { return liveCompare.candidate.returning.load (std::memory_order_acquire); }
bool candidateCompareActive() const noexcept { return liveCompare.candidate.token.load (std::memory_order_acquire) != 0 && ! candidateReturning(); }
int candidateMatchSecondsRemaining() const noexcept { return liveCompare.renderer.matchSecondsRemaining(); }
bool candidateBypassed() const noexcept;
bool candidateHostInactive() const noexcept;
bool candidateMeasurementIsolated() const noexcept { return liveCompare.candidate.isolated.load (std::memory_order_acquire); }
bool candidateTrackMismatch() const noexcept { return liveCompare.candidate.trackMismatch; }
void serviceCandidateCompare();
void endCandidateCompare() noexcept;
bool returnBlindToCandidateListen();
bool takeCandidateBlindReturned() noexcept { return liveCompare.candidate.blindReturned.exchange (false, std::memory_order_acq_rel); }
void stopCandidateReceiver();
juce::String liveComparePreInstance() const;
void processCandidateReceiver (hypha::live_compare::Ring&, const hypha::live_compare::BlockClock&,
                               juce::AudioBuffer<float>&, bool usable, bool offline) noexcept;

// The scope belongs to this ended LIVE trial, not an unrelated capture or comparison.
std::uint64_t liveCompareReturningScope() const noexcept
{ return candidateReturning() ? liveCompare.candidate.scope
    : !liveCompare.sessionActive.load() ? liveCompare.blindScope : 0; }
