// Included in Lifecycle::timerCallback: rendered peak and AUTO retention regressions.
            case 15:
                if (blocks.load() < stoppedAt + 26 || ! post->liveCompareStatus().preAudible) break;
                {
                    pause.store (true); waitWithoutMessageService ([this] { return paused.load(); });
                    const auto evidence = post->measureLiveCompare();
                    if (! evidence.ok()) { pause.store (false); break; }
                    const auto on = post->setLiveCompareMatchOn (true);
                    const auto now = post->liveCompareStatus();
                    const auto sampleDb = 20.0 * std::log10 (0.63095734448 / std::sqrt (2.0));
                    const auto expected = -1.0 - evidence.prePeakDbtp;
                    require (std::abs (evidence.prePeakDbtp + 4.0) < 0.2 && evidence.prePeakDbtp > sampleDb + 1.0
                        && evidence.prePeakDbtp > keptMatch.peakDbtp + 0.5,
                             "current intersample true peak exceeds sample and kept peaks");
                    require (on.turned && now.matchLimited && std::abs (on.gainDb - expected) < 0.05
                        && std::abs (on.shortDb - (20.0 * std::log10 (2.0) - expected)) < 0.05
                        && std::abs (20.0 * std::log10 (now.gain) - expected) < 0.05,
                             "MATCH on caps at the current true peak and reports the short amount");
                    captureRamp.store (true); pause.store (false); stage = 17;
                }
                break;
            case 17:
                if (rampBlocks.load (std::memory_order_acquire) < rampBlockCount) break;
                captureRamp.store (false); verifyRampPeak();
                require (post->setLiveCompareMatchOn (false).turned, "MATCH off before AUTO peak retention check");
                { auto& memory = LiveTimingFixtureAccess::matchMemory (*post);
                  memory.pre = 2.0f; memory.peakDbtp = -10.0; memory.ceiling = std::pow (10.0f, -1.0f / 20.0f); memory.limited = false; }
                end_match_test::autoSetup (*editor, {}); end_match_test::toggle (*editor);
                retainedCap = post->liveCompareStatus().gain;
                require (end_match_test::autoOn (*editor, {}) && ! post->liveCompareStatus().matchOff && retainedCap < 1.6f
                    && end_match_test::autoPeak (*editor, {}) > -5.0, "AUTO retains the new peak when MATCH on caps the boost");
                end_match_test::toggle (*editor);
                require (post->liveCompareStatus().matchOff, "MATCH off before AUTO next measures");
                intersample.store (false); inputScale.store (0.1f); chainGain.store (5.0f); stoppedAt = blocks.load(); stage = 18;
                break;
            case 18:
                if (blocks.load() < stoppedAt + 26 || post->liveCompareStatus().postActual < 1.0f) break;
                end_match_test::toggle (*editor);
                require (end_match_test::autoOn (*editor, {}) && ! post->liveCompareStatus().matchOff, "AUTO and MATCH on after the transient");
                stage = 19;
                break;
            case 19:
                if (! post->liveCompareStatus().matchReady) break;
                {
                    const auto evidence = post->measureLiveCompare();
                    if (! evidence.ok()) break;
                    require (evidence.prePeakDbtp < -10.0 && evidence.measuredDb - 8.0 > 20.0 * std::log10 (retainedCap) + 2.0,
                             "transient left the window and AUTO would otherwise raise gain");
                    const auto withoutPeak = followStep (evidence, -8.0, 20.0 * std::log10 (2.0), -1.0,
                                                        20.0 * std::log10 (retainedCap), -10.0);
                    require (withoutPeak.action == FollowAction::move && withoutPeak.preGainDb > 20.0 * std::log10 (retainedCap) + 2.0,
                             "old AUTO peak would allow the original boost");
                    end_match_test::autoDue (*editor, {}); end_match_test::follow (*editor, post->liveCompareStatus());
                    require (end_match_test::autoOn (*editor, {}) && end_match_test::autoNext (*editor, {}) > 0.0
                        && post->liveCompareStatus().gain <= retainedCap * 1.001f,
                             "AUTO never raises PRE above the cap after the transient leaves");
                    require (post->setLiveCompareMatchOn (false).turned, "MATCH turns off after AUTO check");
                    LiveTimingFixtureAccess::matchMemory (*post) = keptMatch;
                    require (post->setLiveCompareMatchOn (true).turned, "the kept match returns");
                    inputScale.store (1.0f); chainGain.store (0.5f); stoppedAt = blocks.load(); capChecked = true; stage = 16;
                }
                break;
