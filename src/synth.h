#ifndef BEEP_SYNTH_H
#define BEEP_SYNTH_H

#include "common.h"

BOOL synth_init();
VOID synth_fini();
BOOL synth_generate(Wave osc1_wave,
                    Wave osc2_wave,
                    UWORD osc_mix,
                    UWORD rate_freq,
                    UWORD osc1_freq,
                    UWORD osc2_freq,
                    UWORD duration_ms,
                    UWORD cutoff,
                    UWORD gain,
                    Envelope* amp_env,
                    BYTE** out_samples,
                    UWORD* out_num_samples);

#endif
