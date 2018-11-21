#include "model.h"
#include "exporter.h"
#include "player.h"
#include "synth.h"

#include <graphics/gfxbase.h>

#include <proto/dos.h> // FIXME

#define kLibVerKick3 39
#define kClockFreqPAL 3546895 // PAL crystal / 8
#define kClockFreqNTSC 3579545 // NTSC crystal / 8
#define kDefOsc1Wave Wave_Square
#define kDefOsc2Wave Wave_Sawtooth
#define kDefOscMix 50
#define kDefOscDetune 12
#define kDefSampleRateSemitone Semi_C
#define kDefSampleRateOctave PTOct_3
#define kDefOctaveBase 3
#define kDefLengthMs 750
#define kDefCutoff 1100
#define kDefGainDb 0
#define kDefAmpEnvAttack (kUByteMax / 5)
#define kDefAmpEnvDecay (kUByteMax / 10)
#define kDefAmpEnvSustain ((kUByteMax / 2) + 5)
#define kDefAmpEnvRelease (kUByteMax / 5)

extern struct GfxBase* GfxBase;

static struct {
  ULONG clock_freq;
  Wave osc1_wave;
  Wave osc2_wave;
  UWORD osc_mix;
  UWORD osc_detune;
  PTNote sample_rate;
  UWORD octave_base;
  UWORD length_ms;
  UWORD cutoff;
  WORD gain_db;
  Envelope amp_env;
  BOOL samples_dirty;
  BYTE* samples;
  UWORD num_samples;
} g;

// C-8 to B-8 frequencies of equal-tempered scale, A-4 = 440Hz.
static UWORD Octave8Freqs[] = {
  4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902
};

// Paula sampling periods for ProTracker notes.
static UWORD PTNotePeriods[3][12] = {
  { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453 }, // C-1 to B-1
  { 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226 }, // C-2 to B-2
  { 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113 }, // C-3 to B-3
};

BOOL model_init() {
  g.osc1_wave = kDefOsc1Wave;
  g.osc2_wave = kDefOsc2Wave;
  g.osc_mix = kDefOscMix;
  g.osc_detune = kDefOscDetune;
  g.sample_rate.semitone = kDefSampleRateSemitone;
  g.sample_rate.pt_octave = kDefSampleRateOctave;
  g.octave_base = kDefOctaveBase;
  g.length_ms = kDefLengthMs;
  g.cutoff = kDefCutoff;
  g.gain_db = kDefGainDb;
  g.amp_env.attack = kDefAmpEnvAttack;
  g.amp_env.decay = kDefAmpEnvDecay;
  g.amp_env.sustain = kDefAmpEnvSustain;
  g.amp_env.release = kDefAmpEnvRelease;
  g.samples_dirty = TRUE;

  // >= Kickstart 3.0: Clock detection is accurate.
  //  < Kickstart 3.0: Clock is guessed from PAL/NTSC boot setting.
  UWORD pal_mask = (GfxBase->LibNode.lib_Version >= kLibVerKick3) ? REALLY_PAL : PAL;
  g.clock_freq = (GfxBase->DisplayFlags & pal_mask) ? kClockFreqPAL : kClockFreqNTSC;

  return TRUE;
}

VOID model_fini() {
}

Wave model_get_osc1_wave() {
  return g.osc1_wave;
}

VOID model_set_osc1_wave(Wave wave) {
  g.osc1_wave = wave;
  g.samples_dirty = TRUE;
}

Wave model_get_osc2_wave() {
  return g.osc2_wave;
}

VOID model_set_osc2_wave(Wave wave) {
  g.osc2_wave = wave;
  g.samples_dirty = TRUE;
}

PTNote* model_get_sample_rate() {
  return &g.sample_rate;
}

VOID model_set_sample_rate(PTNote* note) {
  g.sample_rate = *note;
  g.samples_dirty = TRUE;
}

UWORD model_get_octave_base() {
  return g.octave_base;
}

VOID model_set_octave_base(UWORD octave_base) {
  g.octave_base = octave_base;
  g.samples_dirty = TRUE;
}

UWORD model_get_length_ms() {
  return g.length_ms;
}

VOID model_set_length_ms(UWORD length_ms) {
  g.length_ms = length_ms;
  g.samples_dirty = TRUE;
}

UWORD model_get_cutoff() {
  return g.cutoff;
}

VOID model_set_cutoff(UWORD cutoff) {
  g.cutoff = cutoff;
  g.samples_dirty = TRUE;
}

UWORD model_get_gain_db() {
  return g.gain_db;
}

VOID model_set_gain_db(UWORD gain_db) {
  g.gain_db = gain_db;
  g.samples_dirty = TRUE;
}

Envelope* model_get_amp_env() {
  return &g.amp_env;
}

VOID model_set_amp_env(Envelope* amp_env) {
  g.amp_env = *amp_env;
  g.samples_dirty = TRUE;
}

UWORD model_get_osc_mix() {
  return g.osc_mix;
}

void model_set_osc_mix(UWORD osc_mix) {
  g.osc_mix = osc_mix;
  g.samples_dirty = TRUE;
}

UWORD model_get_osc_detune() {
  return g.osc_detune;
}

void model_set_osc_detune(UWORD osc_detune) {
  g.osc_detune = osc_detune;
  g.samples_dirty = TRUE;
}

static UWORD period_from_note(PTNote* note) {
  return PTNotePeriods[note->pt_octave][note->semitone];
}

static UWORD freq_from_note(PTNote* note) {
  return DIV_ROUND_NEAREST(g.clock_freq, period_from_note(note));
}

static BOOL make_sample() {
  BOOL ret = TRUE;

  if (g.samples_dirty) {
    g.samples_dirty = FALSE;

    UWORD rate_freq = freq_from_note(&g.sample_rate);
    UWORD oct8_freq = Octave8Freqs[g.sample_rate.semitone];
    UWORD osc1_octave = g.octave_base + g.sample_rate.pt_octave;
    UWORD osc1_freq = DIV_ROUND_NEAREST(oct8_freq, (1 << (8 - osc1_octave)));
    UWORD gain = db_scale_lookup(g.gain_db / (10 / kDbScaleSteps));

    UWORD osc1_total_semis = (osc1_octave * 12) + g.sample_rate.semitone;
    UWORD osc2_total_semis = osc1_total_semis + g.osc_detune;
    UWORD osc2_octave = osc2_total_semis / 12;
    UWORD osc2_semitone = osc2_total_semis % 12;
    UWORD oct8_freq_2 = Octave8Freqs[osc2_semitone];
    UWORD osc2_freq = DIV_ROUND_NEAREST(oct8_freq_2, (1 << (8 - osc2_octave)));

    CHECK(synth_generate(g.osc1_wave, g.osc2_wave, g.osc_mix, rate_freq,
                         osc1_freq, osc2_freq, g.length_ms, g.cutoff, gain,
                         &g.amp_env, &g.samples, &g.num_samples));
  }

 cleanup:
  return ret;
}

BOOL model_play_note(PTNote* note) {
  BOOL ret = TRUE;

  player_stop();
  CHECK(make_sample());
  player_start(g.samples, g.num_samples, period_from_note(note));

cleanup:
  return ret;
}

BOOL model_export_sample() {
  BOOL ret = TRUE;

  CHECK(make_sample());
  CHECK(exporter_save(g.samples, g.num_samples, freq_from_note(&g.sample_rate)));

cleanup:
  return ret;
}
