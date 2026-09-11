#ifndef KIRIN_HYPHA_SPECTRUM_MID_SIDE_FFI_H
#define KIRIN_HYPHA_SPECTRUM_MID_SIDE_FFI_H

#define KIRIN_SPECTRUM_SELECTION_MID_SIDE 3u

/* One POST-local stereo aperture. Mid and Side are absolute dBFS facts, never PRE/POST delta. */
typedef struct {
  uint8_t status;
  uint8_t has_data;
  uint8_t channels;
  uint8_t reserved;
  uint32_t sample_rate;
  float min_hz;
  float max_hz;
  float mid_dbfs[KIRIN_SPECTRUM_BAND_COUNT];
  float side_dbfs[KIRIN_SPECTRUM_BAND_COUNT];
  int64_t presentation_end_samples;
  uint32_t aperture_samples;
  uint32_t fft_size;
  float approximate_below_hz;
} KirinMidSideSpectrumView;

bool kirin_hypha_set_mid_side_spectrum_visible(KirinHypha* handle, bool visible);
bool kirin_hypha_poll_mid_side_spectrum(KirinHypha* handle, KirinMidSideSpectrumView* out);

#endif
