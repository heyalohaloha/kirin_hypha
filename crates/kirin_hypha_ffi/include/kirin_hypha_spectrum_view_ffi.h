#pragma once

/* Same-candidate POST Spectrum view. Missing SHAPE bins have explicit validity, not zero delta. */
typedef struct {
  uint8_t status;
  uint8_t has_data;
  uint8_t channel_mode;
  uint8_t channels;
  uint32_t sample_rate;
  float min_hz;
  float max_hz;
  float pre_dbfs[KIRIN_SPECTRUM_BAND_COUNT];
  float post_dbfs[KIRIN_SPECTRUM_BAND_COUNT];
  float display_db[KIRIN_SPECTRUM_BAND_COUNT];
  int64_t presentation_end_samples;
  uint32_t aperture_samples;
  uint32_t fft_size;
  float approximate_below_hz;
  uint8_t post_has_data;
  uint8_t post_reserved[3];
  uint8_t shape_has_energy;
  uint8_t analysis_view;
  uint8_t shape_reserved[2];
  float shape_energy_delta_db;
  float shape_db[KIRIN_SPECTRUM_BAND_COUNT];
  uint8_t shape_valid[KIRIN_SPECTRUM_BAND_COUNT];
} KirinSpectrumView;

#define KIRIN_SPECTRUM_BATCH_CAPACITY 8

typedef struct {
  KirinSpectrumView latest;
  uint32_t count;
  uint32_t reserved;
  KirinSpectrumView frames[KIRIN_SPECTRUM_BATCH_CAPACITY];
} KirinSpectrumBatch;
