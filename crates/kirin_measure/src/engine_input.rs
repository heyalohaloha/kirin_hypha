//! PCM feeding, separated from engine ownership and result queries.
use super::*;

impl MeasureEngine {
    /// The Meter Session's 10/100 ms facts, with the Max M confirmed up to the same boundary,
    /// and qualified content-grid candidates are read from the same EBU filter input. The latter
    /// never changes Session time. The TIME raw frame also freezes the Session summary at that boundary.
    pub(crate) fn push_observed_with_session_facts_at(
        &mut self,
        samples: &[f64],
        clock: MeterClockStart,
        observe: impl FnMut(u64, &MeasureResult, &[f64], Option<f64>, Option<SessionSummary>),
        observe_content: impl FnMut(ContentWindowObservation),
    ) -> Option<MeasureResult> {
        self.push_observed_internal(samples, clock, true, observe, observe_content)
    }

    pub(super) fn push_observed_internal(
        &mut self,
        samples: &[f64],
        clock: MeterClockStart,
        session_summary: bool,
        mut observe: impl FnMut(u64, &MeasureResult, &[f64], Option<f64>, Option<SessionSummary>),
        mut observe_content: impl FnMut(ContentWindowObservation),
    ) -> Option<MeasureResult> {
        if let Some(grid) = self.content_grid.as_mut() {
            grid.push_span((samples.len() / self.n_channels) as u64, clock);
        }
        if let Some(clock_tracker) = self.publication_clock.as_mut() {
            clock_tracker.push_span((samples.len() / self.n_channels) as u64, clock);
        }
        self.accum.extend_from_slice(samples);

        // EBU/maxima advance in at-most-10 ms segments; native 100 ms cells publish facts.
        let mut result: Option<MeasureResult> = None;
        loop {
            let target = self
                .publication_clock
                .as_ref()
                .map_or(self.analysis_target, |clock| {
                    clock.publication_segment_frames(
                        (self.analysis_target / self.n_channels) as u64,
                        (self.publish_target / self.n_channels) as u64,
                        (self.publish_buf.len() / self.n_channels) as u64,
                    ) as usize
                        * self.n_channels
                });
            if self.accum.len() < target {
                break;
            }
            let chunk_frames = (target / self.n_channels) as u64;
            // 再利用バッファへdrainし、同じPCMを100ms公開バッファにも保持する。
            self.chunk_buf.clear();
            self.chunk_buf.extend(self.accum.drain(..target));
            self.publish_buf.extend_from_slice(&self.chunk_buf);

            // Crest/PSRの400ms窓も解析時刻に合わせて10msずつ進める。
            self.window_400ms.extend(self.chunk_buf.iter().copied());
            while self.window_400ms.len() > self.window_400ms_cap {
                self.window_400ms.pop_front();
            }
            // EBUへは各sampleを一度だけ投入する。10ms内にcontent格子やclock runの
            // 境界で分割する。Meter SessionはDAW格子へ戻し、Watch/Recordのcadenceは保持する。
            let mut processed = 0;
            let mut chunk_tp = 0.0_f64;
            while processed < self.chunk_buf.len() {
                let remaining_frames = (self.chunk_buf.len() - processed) / self.n_channels;
                let segment_frames = self.content_grid.as_ref().map_or(remaining_frames, |grid| {
                    grid.segment_frames(remaining_frames as u64) as usize
                });
                let end = processed + segment_frames * self.n_channels;
                let added = self.ebu.add_frames_f64(&self.chunk_buf[processed..end]);
                if let Err(error) = &added {
                    log::warn!(
                        "[engine] add_frames_f64 failed ({error:?}): segment is not measured"
                    );
                }
                let segment_tp = (0..self.n_channels as u32)
                    .filter_map(|ch| self.ebu.prev_true_peak(ch).ok())
                    .fold(0.0_f64, f64::max);
                chunk_tp = chunk_tp.max(segment_tp);
                if let Some(grid) = self.content_grid.as_mut() {
                    let observed_frames = self.analysis_frames + (end / self.n_channels) as u64;
                    if let Some(mut point) = grid.accept_segment(
                        segment_frames as u64,
                        observed_frames,
                        added.is_ok().then_some(segment_tp),
                    ) {
                        point.lufs_m =
                            self.ebu.loudness_momentary_cached().ok().filter(|value| {
                                value.is_finite() && *value > LUFS_VALID_FLOOR_LUFS
                            });
                        observe_content(point);
                    }
                }
                processed = end;
            }

            // フレーム基準の時刻を進めてから、prev_true_peak をタイムスタンプ付きで窓に追加。
            // prev_true_peak は直近 add_frames チャンク内のピークのみを返す（running max でない）。
            self.analysis_frames += chunk_frames;
            self.tp_window.push_back((chunk_tp, self.analysis_frames));

            // フレーム基準で 400ms より古いエントリを前から失効させる。
            while let Some(&(_, f)) = self.tp_window.front() {
                if self.analysis_frames - f >= self.tp_window_frames {
                    self.tp_window.pop_front();
                } else {
                    break;
                }
            }

            self.update_loudness_maxima();
            self.analysis_phase = (self.analysis_phase + 1) % 10;
            self.analysis_target =
                analysis_chunk_frames(self.publish_target / self.n_channels, self.analysis_phase)
                    * self.n_channels;
            let grid_boundary = self.publication_clock.as_mut().is_some_and(|clock| {
                clock.complete_publication_segment(
                    chunk_frames,
                    (self.publish_target / self.n_channels) as u64,
                )
            });
            if !grid_boundary && self.publish_buf.len() < self.publish_target {
                continue;
            }
            debug_assert!(self.publish_buf.len() <= self.publish_target);
            self.total_frames += (self.publish_buf.len() / self.n_channels) as u64;

            let computed = self.compute();
            let summary = session_summary.then(|| self.cached_session_summary());
            observe(
                self.total_frames,
                &computed,
                &self.publish_buf,
                self.max_lufs_m,
                summary,
            );
            result = Some(computed);
            self.publish_buf.clear();
        }
        result
    }
}
