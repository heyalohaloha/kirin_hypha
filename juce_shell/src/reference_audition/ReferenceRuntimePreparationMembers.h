        struct Configuration
        {
            RuntimeIdentity identity;
            double sampleRate = 0.0;
            int channels = 0;
            std::uint64_t generation = 0;
        };

        struct RequestedSelection
        {
            juce::String presetId;
            juce::String checkId;
            juce::String candidateId;
            juce::String cueId;
            std::uint64_t generation = 0;
        };

        struct PreparedNormalSelection
        {
            std::uint64_t auditionEpoch = 0;
            std::uint64_t selectionGeneration = 0;
            float linearGain = 1.0f;
            double appliedGainDb = 0.0;
            double aIntegratedLoudness = 0.0;
            double aMaximumTruePeakDbtp = 0.0;
            double adjustedBIntegratedLoudness = 0.0;
            double adjustedBMaximumTruePeakDbtp = 0.0;
            double loudnessDeltaBMinusA = 0.0;
            double truePeakDeltaBMinusA = 0.0;
            bool gainLimited = false;
            bool comparisonFallbackOriginal = false;
            TrackingState tracking = TrackingState::none;
            double anchorGainDb = 0.0; // 利用者の MATCH の gain。追従はここから ±6 dB まで（戻すときも同じ値）
            double peakShortfallDb = 0.0;  // 上限で届かない量
            double rangeShortfallDb = 0.0; // MATCH の幅で残る差（正：A より小さい、負：大きい）
            double requiredGainDb = 0.0;   // 上限が無ければ掛ける gain（A を下げる申し出の量）
            bool valid = false;
        };

        struct MatchLevels { double loudness = 0.0, peak = 0.0; };
        MatchLevels matchLevels (const RuntimeSource&, bool cueLevel, double cueLoudness, double cuePeak, double loudnessOffset) const noexcept;
        TrackingState trackingFor (const juce::String& comparisonMode) const noexcept;
        MatchFailure requiredMatchGain (const juce::String& mode, const MatchLevels&, double aLoudness, double aPeak, double& requiredGain) const noexcept;
        bool matchedSelection (const juce::String& mode, double requiredGain, const MatchLevels&, double aLoudness, double aPeak,
                               PreparedNormalSelection&, MatchFailure&) const noexcept;
        void applyPreparedFactsLocked (const PreparedNormalSelection&) noexcept;

        // V を鳴らしたまま確かめた、新しい Version の位置。替えた後に同じ A の窓を探し直さない。
        // 探すのは別の thread（作業スレッドを止めると A の窓を受け取れず、窓が消えて探し直しになる）。
        struct StagedVersionPosition
        {
            std::shared_ptr<const RuntimeACaptureAudio> capture;
            std::shared_ptr<const RuntimeContentAlignment> match;
            std::future<RuntimeContentAlignment> search;
            int misses = 0;
        };
        // 鳴っている間の定期の位置の確かめ。これも別の thread で探し、終わるまで今の写像のまま鳴らす。
        struct PositionSearch
        {
            std::shared_ptr<const RuntimeACaptureAudio> capture;
            std::optional<std::int64_t> expected;
            std::future<RuntimeContentAlignment> search;
            juce::String reason;  // 合わなかった理由。次の探索の間も同じ理由を見せる
            bool announced = false;
        };
        // 初めて選ぶ音源の確かめ（file 全体の hash）。数秒かかるので、これも作業スレッドで計算しない。
        struct SourceVerification
        {
            juce::String key;
            std::uint64_t generation = 0;
            std::shared_ptr<const RuntimeSource> source;
            std::future<juce::String> check;
        };
        struct WorkspaceSelection
        {
            Configuration configuration;
            RequestedSelection selection;
            Snapshot next;
            RuntimeCandidate candidate;
            RuntimeCue cue;
            RuntimeEventContext event;
            RuntimeSourcePresetReceipt sourceTemplateArtifact;
            std::shared_ptr<const RuntimeSource> source;
            juce::String sourceKey, adoptionKey;
            std::int64_t preparedAtMs = 0;
            bool staged = false, returnStarted = false, songEntry = false;
            std::int64_t matchRetryAtMs = 0;  // 新しい曲の MATCH ができない間の次の確かめ
            StagedVersionPosition versionPosition;
        };
        // 押す前に確かめておく曲。音源の全体の hash と private copy を、押した曲より後に 1 曲ずつ進める。
        struct SongPreload
        {
            std::map<juce::String, std::shared_ptr<const RuntimeSource>> held;  // receipt の hash → 確かめ済みの音源
            std::map<juce::String, std::int64_t> retryAtMs;
            std::shared_ptr<const RuntimeSource> source;
            juce::String key;
            std::future<juce::String> check;
            std::int64_t heldBytes = 0, nextScanMs = 0;
        };
        static constexpr size_t preloadSongLimit = 8;
        static constexpr std::int64_t preloadByteLimit = 1024 * 1024 * 1024;
        static constexpr size_t retiredSourceCheckLimit = 1;  // 押下の確かめは同時に 2 本まで。超えた押下は空きを待つ
        struct LiveLevelReading
        {
            double loudness = std::numeric_limits<double>::quiet_NaN();
            double peak = std::numeric_limits<double>::quiet_NaN();
        };
        enum class SongSwitch { notAudible, waiting, switched };
        SongSwitch switchAudibleSong (WorkspaceSelection&, AudioPages&);
        bool serviceSwitchRetirement();
        void serviceSongPreload();
        void startSongPreload (const std::vector<juce::String>&, std::int64_t nowMs);
        void clearSongPreload();
        bool beginSelectionPreparationLocked() noexcept;
        bool holdSelectionForSourceRetry (const juce::String&, bool confirmed = false);
        void clearPreparedWorkspace();
        bool servicePreparedWorkspace (const Configuration&);
        bool stagedVersionReady (WorkspaceSelection&, AudioPages&);
        void serviceVersionHold();
        std::future<RuntimeContentAlignment> searchAlignment (std::shared_ptr<const RuntimeACaptureAudio>, std::shared_ptr<const RuntimeSource>,
                                                              std::shared_ptr<const RuntimeDetailedMeasurement>, std::optional<std::int64_t>);
        void retireVersionSearch (std::future<RuntimeContentAlignment>&);
        void retirePositionSearch();
        bool finishPositionSearch (const std::shared_ptr<const RuntimeACaptureAudio>&, const std::shared_ptr<const RuntimeSource>&,
                                   const std::shared_ptr<const RuntimeDetailedMeasurement>&, std::optional<std::int64_t>, RuntimeContentAlignment&);
        bool positionSearchJustFinished();
        juce::String verifySourceBehindWorker (std::shared_ptr<const RuntimeSource>&, const juce::String& key,
                                               std::uint64_t generation, bool freshAttempt);
        bool verifyingSource (std::uint64_t generation) const;
        bool sourceCheckSlotFreed();
        void retireSourceVerification();
        void finishWorkspaceSelection (WorkspaceSelection, const PreparedNormalSelection* audible = nullptr);
        std::shared_ptr<const RuntimeSource> loadVerifiedSelectionSource (const RuntimeCandidate&, const RequestedSelection&, Snapshot&, std::int64_t);
        enum class PreparedLevel { none, loudness, peak };
        struct PreparedReturn
        {
            std::uint64_t generation = 0;
            int windowBlocks = 0;
            PreparedLevel level = PreparedLevel::none;
            bool pending = false, available = false;
        };
