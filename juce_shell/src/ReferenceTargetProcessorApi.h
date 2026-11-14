// Public, non-RT Reference controls, included inside KirinHyphaProcessorBase.
    hypha::reference_audition::TargetSnapshot referenceTargetSnapshot() const;
    bool changeReferenceTarget (const hypha::reference_audition::TargetRequest&);
    void setReferenceViewPresented (bool);
    bool selectReferenceC();
    bool selectReferenceVersion (const juce::String&, bool automatic = false);
    bool selectReferenceB();
    bool selectReferenceRef(); bool selectReferenceSong (const juce::String&); bool selectReferenceSongSet (const juce::String&);
    void setReferenceFollowA (bool); bool nudgeReferenceFollow (int slot, double seconds); bool resetReferenceFollow (int slot); // B・C の流し方
    bool takeReferenceCheckAttempt (std::uint64_t attempt) noexcept;
    hypha::reference_audition::WindowLoudnessCache referenceWindowLoudness (int slot) const; hypha::reference_audition::RematchResult rematchReferenceCheck();
    hypha::reference_audition::VersionIdentity identifyReferenceVersion(); // V の自動特定（メッセージスレッド）
    void selectReferenceA();
    bool selectReferencePreset (const juce::String&);
    bool retryReferencePresetSelection();
    bool selectReferenceCheck (const juce::String&);
    bool selectReferenceCandidate (const juce::String&);
    bool retryReferenceCandidatePreparation();
    bool selectReferenceCue (const juce::String&);
    bool selectReferenceVisualSlot (int); bool selectReferenceAWindow (hypha::reference_audition::AWindowTarget, hypha::reference_audition::AWindowChoice, int seconds = 10); bool resetReferenceAWindow (hypha::reference_audition::AWindowTarget);
    bool requestReferenceRecovery();
    bool setReferenceOverlay (const hypha::reference_audition::BalanceOverlay&); // B の Balance の重ね比べ（保存し、host に知らせる）
    bool setReferenceCheckSong (bool); // C の比べる値 CUE／SONG（保存し、host に知らせる）
    bool startReferenceBlind (double aIntegratedLoudness, double aMaximumTruePeakDbtp);
    bool approveReferenceBlindLowerA (double aIntegratedLoudness,
                                      double aMaximumTruePeakDbtp);
    hypha::reference_audition::LowerAApproval approveReferenceLowerA (const hypha::reference_audition::LowerAOffer&); // R-12
    void returnReferenceLevelToNormal();     // RETURN：役を止めてから A を通常の音量へ
    double referenceHeldAttenuationDb() const noexcept; // 承認して A を下げている量（0 以下）
    int referenceOutputSlot() const noexcept; // 直前の block で出力を持った B・C・V（1 V、2 C、3 B、無ければ 0）
    bool selectReferenceBlindStimulus (int stimulus);
    bool answerReferenceBlind (int stimulus);
    bool revealReferenceBlind();
    void endReferenceBlind();
