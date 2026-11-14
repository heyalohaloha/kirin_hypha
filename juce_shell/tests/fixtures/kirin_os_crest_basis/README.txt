Hypha 1.1.57 へ渡す Reference の記録の受け渡しの例（Kirin OS 1.6.1、Crest = sample peak ÷ RMS）。

- measurement_sample_peak_over_rms.json：measurement の views.dynamics は 5 つの鍵
  start_sample, hop_samples, psr_millidb, crest_millidb, crest_basis（"sample_peak_over_rms"）。
- profile_sample_peak_over_rms.json：中に入る全部の曲の Crest が新しい式の時だけ views.dynamics に
  crest_basis を足した profile（4 つの鍵）。
- profile_mixed_formulas.json：前の式（True Peak ÷ RMS）の曲が混ざる時の profile。crest_basis は無い（3 つの鍵）。

最上位・他の view・version（"2.0"）は変えない。schema は schemas/kirin_hypha_reference_measurement.schema.json と
schemas/kirin_hypha_reference_profile.schema.json。確かめる試験は src/services/__tests__/hyphaReferenceCrestBasisExamples.test.mjs。
