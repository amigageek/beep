#ifndef BEEP_MODEL_H
#define BEEP_MODEL_H

#include "common.h"

BOOL model_init();
VOID model_fini();
Wave model_get_osc1_wave();
VOID model_set_osc1_wave(Wave wave);
Wave model_get_osc2_wave();
VOID model_set_osc2_wave(Wave wave);
PTNote* model_get_sample_rate();
VOID model_set_sample_rate(PTNote* sample_rate);
UWORD model_get_octave_base();
VOID model_set_octave_base(UWORD octave_base);
UWORD model_get_cutoff();
VOID model_set_cutoff(UWORD cutoff);
UWORD model_get_gain_db();
VOID model_set_gain_db(UWORD gain_db);
UWORD model_get_length_ms();
VOID model_set_length_ms(UWORD length_ms);
Envelope* model_get_amp_env();
VOID model_set_amp_env(Envelope* amp_env);
BOOL model_play_note(PTNote* note);
BOOL model_export_sample();
UWORD model_get_osc_mix();
void model_set_osc_mix(UWORD osc_mix);
UWORD model_get_osc_detune();
void model_set_osc_detune(UWORD osc_detune);

#endif
