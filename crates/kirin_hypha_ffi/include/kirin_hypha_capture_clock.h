#ifndef KIRIN_HYPHA_CAPTURE_CLOCK_H
#define KIRIN_HYPHA_CAPTURE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include "kirin_hypha_channels.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIRIN_HYPHA_CLOCK_UNKNOWN 0
#define KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE 1
#define KIRIN_HYPHA_CLOCK_AUDIO_RENDER_TIMELINE 2
#define KIRIN_HYPHA_PRESENTATION_SOURCE_UNKNOWN 0
#define KIRIN_HYPHA_PRESENTATION_SOURCE_VST3 1
#define KIRIN_HYPHA_PRESENTATION_SOURCE_AUDIO_UNIT_V2 2
#define KIRIN_HYPHA_AUXILIARY_SOURCE_UNKNOWN 0
#define KIRIN_HYPHA_AUXILIARY_SOURCE_VST3_CONTINUOUS 1
#define KIRIN_HYPHA_AUXILIARY_SOURCE_AUDIO_UNIT_RENDER 2
#define KIRIN_HYPHA_AUXILIARY_SOURCE_AAX_NATIVE 3

/* Stage one host-clock descriptor immediately before its matching push_samples transaction. */
void kirin_hypha_note_capture_window(KirinHypha* handle, bool position_valid,
                                     int64_t position_samples, uint64_t num_frames,
                                     uint8_t clock_source, uint8_t presentation_source,
                                     bool input_presentation_valid,
                                     uint32_t input_presentation_samples,
                                     bool output_presentation_valid,
                                     uint32_t output_presentation_samples,
                                     uint8_t auxiliary_source, bool auxiliary_valid,
                                     int64_t auxiliary_samples, bool force_new_epoch);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KIRIN_HYPHA_CAPTURE_CLOCK_H */
