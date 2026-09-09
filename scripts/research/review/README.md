# Offline Hypha listening review

This is a development-only annotation tool, independent of the plugin runtime.
It does not implement or qualify SPACE, ATTACK 2MIX or PRE/POST Blind.
The pilot asks for representative marks, not exhaustive onset annotation.
Never calculate final precision/recall from this pilot's marks.

## Build

```sh
node scripts/research/review/build_review_pack.mjs \
  RESEARCH_ROOT NEW_PRIVATE_REVIEW_DIRECTORY NEW_PRIVATE_EVIDENCE_DIRECTORY
node --test scripts/research/review/review.test.mjs
node scripts/research/review/browser-test.mjs PACK_DIRECTORY PLAYWRIGHT_MODULE_DIRECTORY NEW_EVIDENCE_DIRECTORY
node scripts/research/review/evaluate_review_answers.mjs \
  ANSWERS_JSON PACK_DIRECTORY/manifest.json CORRECTION_SIDECAR_JSON \
  EVIDENCE_DIRECTORY/evidence-manifest.json NEW_PRIVATE_OUTPUT_JSON
```

The existing research root must contain the corpus proposal, exactly one development
directory per feature, and the associated float WAVs and provenance JSONs.
Only six explicitly selected development excerpts are opened; reserved evaluation
audio is never decoded or copied.
Creation refuses existing output directories and uses private file permissions.
The review and evidence directories must remain separate. Give only the review directory
to the annotator; retain the evidence directory for evaluation.
Do not add generated audio, provenance paths, reviewer identities or answers to Git.

## Source responsibilities

- `wav.mjs`: validates native float WAV structure and produces a display-only envelope.
- `build_review_pack.mjs`: verifies development partitions and provenance, copies unchanged PCM,
  embeds the UI and waveform data, and writes a separate detector-result evidence bundle.
- `model.js`: validates answers and exact pack identity; exports TSV with real separators.
- `wave.js`: native sample-coordinate selection and bounded canvas rendering.
- `app.js`: one audio player, manual playback, draft answers, persistence, import/export.
- `evaluate_review_answers.mjs`: verifies the answer, pack, audio, separate detector-result evidence and
  correction identities, then writes a privacy-reduced development diagnostic.
- `shell.html` and `review.css`: offline interface with no external resources.
- `review.test.mjs` and `browser-test.mjs`: model, parser, browser and error-path tests.

## Review contract

Each source is an existing 30-second excerpt starting 30 seconds into its original file.
The fixed central review window is `[10,20)` seconds in the excerpt, independent of detector candidates.
The waveform has approximately 1 ms bins and combines the minimum/maximum across channels,
without summing channels or altering audio.
Marks use integer native samples relative to the excerpt; add `source_start_sample`
to locate them in the original file.
SPACE marks use a strictly ordered start and end, ATTACK marks use equal start and end.
Waveform gain is display-only; browser playback volume is separate and initially 35%.
The native media control may apply output-device resampling, so browser playback is
not a bit-identical audio-output or plugin-timing qualification.

JSON is the portable answer format; the local manifest and its hash identify the
sources and question protocol.
Drafts survive navigation and reload, and changes invalidate a completed answer.
Unknown and unavailable answers remain separate from affirmative/negative judgements.
An import with a different pack or manifest is rejected before touching existing answers.
Corrupt browser storage is preserved, and failed storage writes produce a visible warning.
TSV quotes tabs, newlines and quotes and neutralizes spreadsheet formula prefixes.
Keep the original delivered audio folder unchanged.

The evaluation importer preserves raw marks and applies corrections as a separate authority.
SPACE interval derivatives use integer native-sample `[start, end)` boundaries. ATTACK uses
one-to-one nearest matching within an explicit tolerance, while unmatched detector candidates
remain unclassified because the pilot marks are representative rather than exhaustive.
The derived JSON omits reviewer names, notes and source paths and is created exclusively, so a
prior import is never overwritten. Keep it private with the answers and audio.

## Verification limits

The browser test uses Chrome with a fake output sink (`--disable-audio-output`).
It verifies real browser decoding, native rate/frame/channel counts, peak/RMS agreement,
playback progress, annotations, export/import, reload, layout and error paths.
This does not verify sound at a physical output device.
Use an ordinary browser without this switch for listening.
Generated test answers exist only in isolated browser profiles and the evidence directory.

Official API references checked during implementation:

- [Media playback promises](https://developer.mozilla.org/en-US/docs/Web/API/HTMLMediaElement/play)
- [Local storage and file URL limitations](https://developer.mozilla.org/en-US/docs/Web/API/Window/localStorage)
- [Playwright browser launch](https://playwright.dev/docs/api/class-browsertype)
- [Playwright page API](https://playwright.dev/docs/api/class-page)
- [Chromium audio switches](https://chromium.googlesource.com/chromium/src/+/main/media/base/media_switches.cc)
