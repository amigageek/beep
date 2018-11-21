#include "synth.h"

#include <proto/exec.h>
#include <stdio.h>

#include <proto/dos.h> // FIXME

#define kSampleSizeAlignMask 0xFFF
#define kFilterOrder 2
#define kAmpEnvLUTSize 0x100
#define kFPUWordShift kBitsPerWord      // fixed-point unsigned WORDs << before divide >> after multiply
#define kFPWordShift (kBitsPerWord - 1) // fixed-point   signed WORDs << before divide >> after multiply

typedef struct {
  WORD v[2];
} Complex;

typedef struct {
  APTR samples;
  APTR osc1_func;
  APTR osc2_func;
  APTR filter_coeffs;
  APTR amp_env_lut;
  UWORD num_samples;
  UWORD osc1_per_inv;
  UWORD osc2_per_inv;
  UWORD num_samples_inv;
  UWORD osc1_amp_scale;
  UWORD osc2_amp_scale;
  UWORD pad;
} AsmParams;

extern VOID synth_asm(/*__reg("a6") */AsmParams* asm_params);
extern VOID* synth_asm_sawtooth;
extern VOID* synth_asm_square;
extern VOID* synth_asm_triangle;
extern VOID* synth_asm_noise;

static struct {
  AsmParams asm_params;
  VOID* synth_funcs[kNumWaves];
  UWORD amp_env_lut[kAmpEnvLUTSize];
  WORD filter_coeffs[2][1 + kFilterOrder];
  UWORD samples_size_b;
} g;

BOOL synth_init() {
  printf("synth_asm: %p\n", synth_asm);

  g.synth_funcs[Wave_Square] = &synth_asm_square;
  g.synth_funcs[Wave_Sawtooth] = &synth_asm_sawtooth;
  g.synth_funcs[Wave_Triangle] = &synth_asm_triangle;
  g.synth_funcs[Wave_Noise] = &synth_asm_noise;

  g.asm_params.filter_coeffs = (WORD*)g.filter_coeffs;
  g.asm_params.amp_env_lut = g.amp_env_lut;

  return TRUE;
}

VOID synth_fini() {
  if (g.asm_params.samples) {
    FreeMem(g.asm_params.samples, g.samples_size_b);
  }
}

static VOID make_amp_env_lut(Envelope* amp_env,
                             UWORD gain) {
  UBYTE attack_end = amp_env->attack;
  UBYTE decay_end = attack_end + amp_env->decay;
  UBYTE sustain_end = kUByteMax - amp_env->release;

  for (UWORD i = 0; i < kAmpEnvLUTSize; ++ i) {
    UBYTE amp;

    if (i < attack_end) {
      amp = (i * kUByteMax) / amp_env->attack;
    }
    else if (i < decay_end) {
      amp = kUByteMax - (((kUByteMax - amp_env->sustain) * (i - attack_end)) / amp_env->decay);
    }
    else if (i < sustain_end) {
      amp = amp_env->sustain;
    }
    else {
      amp = amp_env->sustain - ((amp_env->sustain * (i - sustain_end)) / amp_env->release);
    }

    g.amp_env_lut[i] = MIN(kUWordMax, (amp * gain) >> 8);
  }
}

static UWORD log2_ceil(WORD value) {
  UWORD result = 0;
  -- value;

  while (value) {
    value >>= 1;
    ++ result;
  }

  return result;
}

static Complex complex_add(Complex a,
                           Complex b) {
  return (Complex){ a.v[0] + b.v[0], a.v[1] + b.v[1] };
}

static Complex complex_mult_neg(Complex a,
                                Complex b,
                                UWORD shift) {
  return (Complex){
    - ((a.v[0] * b.v[0]) >> shift) + ((a.v[1] * b.v[1]) >> shift),
    - ((a.v[0] * b.v[1]) >> shift) - ((a.v[1] * b.v[0]) >> shift)
  };
}

static Complex complex_asr(Complex a,
                           UWORD shift) {
  return (Complex){ a.v[0] >> shift, a.v[1] >> shift };
}

static VOID make_filter_coeffs(UWORD rate_freq,
                               UWORD cutoff) {
  // Butterworth lowpass filter math summarized at: https://www.dsprelated.com/showarticle/1119.php

  // Clamp cutoff to 1/4 sample rate for tan lookup.
  cutoff = MIN(cutoff, rate_freq / 4);

  // Prewarp cutoff from s-plane axis to z-plane unit circle.
  // Fc = fs/PI * tan(PI*fc/fs)
  // *fs/PI cancelled with s-plane pole scale (*PI) and bilinear transform (/fs)
  // tan LUT range is [0,kTanTableSize-1] = [0,PI/4-delta]
  // cutoff_prewarped range is [0,0xFFFF] = [0,1]
  UWORD cutoff_prewarped = tan_lookup(((cutoff * 4 * kTanTableSize) - 1) / rate_freq);

  // Compute stable filter poles in the s-plane.
  Complex s_poles[kFilterOrder];
  UWORD next_s_pole = 0;

  for (UWORD pole_idx = 0; pole_idx < (kFilterOrder * 2); ++ pole_idx) {
    // Distribute s-plane poles evenly around the unit circle.
    // theta = ((pole_idx + ((kFilterOrder & 1) ? 0.0 : 0.5)) * PI) / kFilterOrder;
    // theta range is [0,kSinTableSize-1] = [0,2*PI]
    UWORD theta = DIV_ROUND_NEAREST((pole_idx * 2 + ((kFilterOrder & 1) ? 0 : 1)) * kSinTableSize, 4 * kFilterOrder);

    // s_pole_norm range is [-0x7FFF,0x7FFF] = [-1,1]
    Complex s_pole_norm = { cos_lookup(theta), sin_lookup(theta) };

    // Retain stable (negative real) s-plane poles only.
    if (s_pole_norm.v[0] < 0) {
      for (UWORD i = 0; i < 2; ++ i) {
        // Scale cutoff from 1/(2*PI) to Fc
        // *(2*PI) cancelled with bilinear transform (/2) and cutoff prewarp (/PI)
        // s_pole range is [-0x7FFF,0x7FFF] = [-1,1]
        s_poles[next_s_pole].v[i] = (s_pole_norm.v[i] * cutoff_prewarped) >> kFPUWordShift;
      }

      ++ next_s_pole;
    }
  }

  // Bilinear transform to calculate z-plane poles.
  // zp = (1 + sp/(2*fs)) / (1 - sp/(2*fs))
  // /(2*fs) cancelled with cutoff prewarp (*fs) and s-plane pole scale (*2)
  // z_pole range is [-0x7FFF,0x7FFF] = [-1,1]
  Complex z_poles[kFilterOrder];

  for (UWORD pole_idx = 0; pole_idx < kFilterOrder; ++ pole_idx) {
    // (1+sp) / (1-sp):
    //   real part:      zp[r] = (1-sp[r]^2-sp[i]^2) / ((1-sp[r])^2 + sp[i]^2)
    //   imaginary part: zp[i] = (2*sp[i]          ) / ((1-sp[r])^2 + sp[i]^2)
    // Numerators in [-1,1] and [-2,2], denominator in [1,4], rescale by /4
    //   real part:      zp[r] = ((1-sp[r]^2-sp[i]^2)/4) / ((0.5-(sp[r]/2))^2 + (sp[i]^2)/4)
    //   imaginary part: zp[i] = (sp[i]/2              ) / ((0.5-(sp[r]/2))^2 + (sp[i]^2)/4)
    LONG numer_real = (0x3FFFFFFF - (s_poles[pole_idx].v[0] * s_poles[pole_idx].v[0])
                                  - (s_poles[pole_idx].v[1] * s_poles[pole_idx].v[1])) >> 2;
    LONG numer_imag = s_poles[pole_idx].v[1] << (kFPWordShift - 1);
    WORD denom_term1 = 0x3FFF - (s_poles[pole_idx].v[0] >> 1);
    WORD denom_term2 = (s_poles[pole_idx].v[1] * s_poles[pole_idx].v[1]) >> (kFPWordShift + 2);
    WORD denom = ((denom_term1 * denom_term1) >> kFPWordShift) + denom_term2;

    z_poles[pole_idx].v[0] = numer_real / denom;
    z_poles[pole_idx].v[1] = numer_imag / denom;
  }

  // Expand transfer function numerator/denominator to find polynomial coefficients.
  //   H(z) = K * (z+1)^N / ((z-p0)*(z-p1)*...)
  // becomes:
  //   H(z) = K * (1 + b1*z + b2*z^2 + ... + bN*z^N) / (a0 + a1*z + a2*z^2 + ... + aN*z^N)

  // Low-pass butterworth filter has [order] zeros at z=-1.
  // Instead of expanding polynomial, compute directly as a row of Pascal's triangle.
  WORD coeffs_b[1 + kFilterOrder] = { 1 };

  for (UWORD i = 0; i < kFilterOrder; ++ i) {
    coeffs_b[i + 1] = (coeffs_b[i] * (kFilterOrder - i)) / (i + 1);
  }

  // coeffs_a range is [-0x7FFF,0x7FFF] = [-N,N] where N is largest coeff_b real value.
  UWORD coeff_a_shift = log2_ceil(coeffs_b[kFilterOrder / 2]);

  // coeffs_a[0] = 1
  Complex coeffs_a[1 + kFilterOrder] = { 0 };
  coeffs_a[0].v[0] = 0x7FFF >> coeff_a_shift;

  for (UWORD pole_idx = 0; pole_idx < kFilterOrder; ++ pole_idx) {
    Complex z_pole = complex_asr(z_poles[pole_idx], coeff_a_shift);

    for (WORD coeff_idx = kFilterOrder; coeff_idx >= 0; -- coeff_idx) {
      // coeffs_a[N] = (coeffs_a[N] * (- z_pole)) + coeffs_a[N-1]
      coeffs_a[coeff_idx] = complex_mult_neg(coeffs_a[coeff_idx], z_pole, kFPWordShift - coeff_a_shift);

      if (coeff_idx > 0) {
        coeffs_a[coeff_idx] = complex_add(coeffs_a[coeff_idx], coeffs_a[coeff_idx - 1]);
      }
    }
  }

  // Transform z-domain transfer function H(z) into time-domain recurrence equation.
  // x[] is input sample history, y[] is filtered sample history.
  //
  // y[n] = x[n]*b0 + x[n-1]*b1 + x[n-2]*b2 + ... + x[0]*bN
  //                - y[n-1]*a1 - y[n-2]*a2 - ... - y[0]*aN
  //
  // Division by coeffs_a[kFilterOrder].real optimized away (always 1).
  for (UWORD i = 0; i < ARRAY_SIZE(g.filter_coeffs[0]); ++ i) {
    g.filter_coeffs[0][i] = coeffs_b[i];
    g.filter_coeffs[1][i] = - coeffs_a[i].v[0];
  }

  // Scale transfer function H(z) numerator to normalize gain at 0Hz.
  // K = sum(ai, i=0..N) / sum(bi, i=0..N)
  WORD scale_numer = 0;
  WORD scale_denom = 0;

  for (UWORD i = 0; i < ARRAY_SIZE(g.filter_coeffs[0]); ++ i) {
    scale_numer += g.filter_coeffs[1][i];
    scale_denom += g.filter_coeffs[0][i];
  }

  UWORD scale = abs(scale_numer / scale_denom);

  scale = (scale * 26213) >> 16;

  for (UWORD i = 0; i < ARRAY_SIZE(g.filter_coeffs[0]); ++ i) {
    g.filter_coeffs[0][i] *= scale;
    //printf("recur[%u]: (%ld, %ld)\n", i, g.filter_coeffs[0][i], g.filter_coeffs[1][i]);
  }
}

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
                    UWORD* out_num_samples) {
  BOOL ret = TRUE;

  // FIXME: document range restriction.
  g.asm_params.num_samples = MAX(0x100, MIN(kWordMax, DIV_ROUND_NEAREST(rate_freq * duration_ms, 1000) & ~1UL));

  // Allocate sample memory in chunks to minimize fragmentation.
  ULONG samples_size_b = (g.asm_params.num_samples + kSampleSizeAlignMask) & ~kSampleSizeAlignMask;

  if (g.samples_size_b != samples_size_b) {
    if (g.asm_params.samples) {
      FreeMem(g.asm_params.samples, g.samples_size_b);
    }

    g.samples_size_b = samples_size_b;
    CHECK(g.asm_params.samples = (BYTE*)AllocMem(g.samples_size_b, MEMF_CHIP));
  }

  // Generat amplitude envelope lookup table.
  // FIXME: if needed
  make_amp_env_lut(amp_env, gain);

  // Calculate lowpass filter coefficients.
  // FIXME: if needed
  make_filter_coeffs(rate_freq, cutoff);

  // Quantize oscillator period for the current sample rate.
  // A fractional period would cause variation in waveform repetition,
  // creating audible low-frequency harmonics.
  UWORD osc1_per = DIV_ROUND_NEAREST(rate_freq, osc1_freq);
  UWORD osc2_per = DIV_ROUND_NEAREST(rate_freq, osc2_freq);

  // Calculate 1/osc_per.
  // Quantize close to 0x10000 to again minimize low-frequency harmonics.
  g.asm_params.osc1_per_inv = DIV_ROUND_NEAREST(1 << kFPUWordShift, osc1_per);
  g.asm_params.osc2_per_inv = DIV_ROUND_NEAREST(1 << kFPUWordShift, osc2_per);

  // Calculate 0x100/num_samples for amplitude envelope table lookup.
  g.asm_params.num_samples_inv = (0x100 << kFPUWordShift) / g.asm_params.num_samples;

  /* g.filter_coeffs[0][2] = 129; */
  /* g.filter_coeffs[0][1] = 259; */
  /* g.filter_coeffs[0][0] = 129; */
  /* g.filter_coeffs[1][1] = 28398; */
  /* g.filter_coeffs[1][0] = -12532; */
  /* printf("osc1_freq: %u\n", osc1_freq); */
  /* printf("osc1_per: %u\n", osc1_per); */

  /* printf("coeff b0: %ld\n", g.filter_coeffs[0][2]); */
  /* printf("coeff b1: %ld\n", g.filter_coeffs[0][1]); */
  /* printf("coeff b2: %ld\n", g.filter_coeffs[0][0]); */
  /* printf("coeff a1: %ld\n", g.filter_coeffs[1][1]); */
  /* printf("coeff a2: %ld\n", g.filter_coeffs[1][0]); */
  /* printf("samples: %p\n", g.asm_params.samples); */

  g.asm_params.osc1_func = g.synth_funcs[osc1_wave];
  g.asm_params.osc2_func = g.synth_funcs[osc2_wave];

  g.asm_params.osc1_amp_scale = (kWordMax * (100 - osc_mix)) / 100;
  g.asm_params.osc2_amp_scale = kWordMax - g.asm_params.osc1_amp_scale;

  synth_asm(&g.asm_params);

  *out_samples = g.asm_params.samples;
  *out_num_samples = g.asm_params.num_samples;

cleanup:
  return ret;
}
