  .globl _synth_asm
  .globl _synth_asm_square
  .globl _synth_asm_sawtooth
  .globl _synth_asm_triangle
  .globl _synth_asm_noise

  || Offsets from AsmParams structure
	.set Samples, 0x0             | Output sample buffer
	.set Osc1Func, 0x4            | Oscillator 1 generator
	.set Osc2Func, 0x8            | Oscillator 2 generator
	.set FilterCoeffs, 0xC        | Low-pass filter coefficients
	.set AmpEnvLUT, 0x10          | Amplitude envelope LUT
	.set NumSamples, 0x14         | Number of samples to generate, range [0x100,0x7FFF]
	.set Osc1PerInv, 0x16         | 0x10000 / (oscillator 1 period)
	.set Osc2PerInv, 0x18         | 0x10000 / (oscillator 2 period)
	.set NumSamplesInv, 0x1A      | 0x100 * 0x10000 / (number of samples)
  .set Osc1AmpScale, 0x1C       | Oscillator 1 amplitude scale, range [0x0, 0x7FFF] = [0, 1]
 	.set Osc2AmpScale, 0x1E       | Oscillator 2 amplitude scale, range [0x0, 0x7FFF] = [0, 1]

_synth_asm:
  movem.l d0-d7/a0-a6,-(sp)

  move.l Samples(a0),a5
  move.l Osc1Func(a0),a4
  move.l Osc2Func(a0),a3
  move.l FilterCoeffs(a0),a2
  move.l AmpEnvLUT(a0),a1
  move.w NumSamples(a0),d6
  move.l Osc1PerInv(a0),d5      | 0x10000 / (oscillator 1 and 2 period) in two words
  move.w NumSamplesInv(a0),d4

  moveq.l #0x0,d7               | Sample number = 0
  move.l d7,-(sp)               | Unused = 0, Random seed = 0
  move.l d7,-(sp)               | Filter state: x[n-1] = x[n-2] = 0
  move.l d7,-(sp)               | Filter state: y[n-1] = y[n-2] = 0

  move.l #0x20,d7

.sample_loop:
  || FIXME: Check whether [0x80, 0x7F] can overflow, generate [0x81, 0x7F] instead?

  || Oscillator 1
  move.l d7,d0
  swap d5                       | Oscillator 1 period to low word
  jsr (a4)
  move.w d0,d3

  || Oscillator 2
  move.l d7,d0
	swap d5                       | Oscillator 2 period to low word
  jsr (a3)

  || Oscillator mix
  muls.w Osc1AmpScale(a0),d3
 	muls.w Osc2AmpScale(a0),d0
  swap d3
 	swap d0
  lsl.w #0x1,d3
  lsl.w #0x1,d0
  add.w d3,d0

  || Low-pass 2nd order Butterworth filter
  move.l (sp)+,d1               | x[n-1], y[n-1]
  move.l (sp),d2                | x[n-2], y[n-2]
  move.l d1,(sp)                | Current x[n-1], y[n-1] becomes next x[n-2], y[n-2]
  move.w d0,-(sp)               | Current x[n] becomes next x[n-1]
  move.l d1,d3
  muls.w 0x4(a2),d0             | x[n]*b0
  muls.w 0x2(a2),d1             | x[n-1]*b1
  add.l d1,d0                   | x[n]*b0 + x[n-1]*b1
  move.l d2,d1
  muls.w (a2),d1                | x[n-2]*b2
  add.l d1,d0                   | x[n]*b0 + x[n-1]*b1 + x[n]*b2
  swap d3
  muls.w 0x8(a2),d3             | y[n-1]*(-a1)
  add.l d3,d0                   | x[n]*b0 + x[n-1]*b1 + x[n]*b2 - y[n-1]*a1
  swap d2
  muls.w 0x6(a2),d2             | y[n-2]*(-a2)
  add.l d2,d0                   | x[n]*b0 + x[n-1]*b1 + x[n]*b2 - y[n-1]*a1 - y[n-2]*a2
  swap d0
  lsl.w #0x2,d0                 | signed FP rescale >> 15, coefficient scale << 1
  move.w d0,-(sp)               | Current y[n] becomes next y[n-1]

  || Amplitude envelope
  move.l d7,d1
  mulu.w d4,d1                  | (i * 0x100 * 0x10000) / num_samples
  swap d1                       | x = (i * 0x100) / num_samples, range [0,FF]
  lsl.w #0x1,d1                 | x indexes words instead of bytes
  move.w (a1,d1.w),d1           | amp_word = amp_env_lut[x]
  muls.w d1,d0
  swap d0                       | sample_byte = (sample_word * amp_word) >> 16
  move.w d0,d1
  move.w #0xFF80,d2
  and.w d2,d1                   | check for positive overflow in upper byte
  beq .sample_out
  cmp.w d2,d1                   | check for negative overflow in upper byte
  beq .sample_out
  rol.w #0x1,d1                 | move sign bit to bit 0
  add.b #0x7F,d1                | clamp result to +/- 0x7F
  move.b d1,d0

  ||lsr.w #0x8,d0

.sample_out:
  move.b d0,(a5)+
  addq.w #0x1,d7
  cmp.w d7,d6
  bne .sample_loop

  add.l #0xC,sp                 | Pop state from stack
  movem.l (sp)+,d0-d7/a0-a6
  rts

_synth_asm_square:
  || Square wave oscillator
  mulu.w d5,d0                  | (i * 0x10000) / osc_per
  lsl.l #0x1,d0                 | (i * 0x10000) / osc_half_per
  swap d0                       | i / osc_half_per
  and.w #0x1,d0
  add.w #0x7FFF,d0              | 0x7FFF in phase 0-50%, 0x8000 in phase 50-100%
  rts

_synth_asm_sawtooth:
  || Sawtooth oscillator
  mulu.w d5,d0                  | (i * 0x10000) / osc_per
  rts

_synth_asm_triangle:
  || Triangle wave oscillator
  mulu.w d5,d0                  | (i * 0x10000) / osc_per
  move.w d0,d1
  lsl.l #0x1,d0                 | (i * 0x10000) / osc_half_per
  move.w #0xC000,d2             | Low dividend bit, high modulus bit for (/ osc_half_per)
  and.w d2,d1
  beq .done                     | Skip negation in phase 0-50%
  eor.w d2,d1
  beq .done                     | Skip negation in phase 150-200%
  not.w d0                      | Negate (not.b to handle 0x8000) sawtooth in phase 50%-150%
.done:
  rts

_synth_asm_noise:
  || Noise oscillator
  move.w d5,d1                  | 0x10000 / osc_per
  lsl.l #0x1,d1                 | 0x10000 / osc_half_per
 	mulu.w d1,d0                  | (i * 0x10000) / osc_half_per
  sub.w d1,d0                   | ((i - 1) * 0x10000) / osc_half_per
  bcs .prng                     | Update PRNG seed at beginning of each period
  move.w 0xC(sp),d0             | Current PRNG seed
  rts

.prng:
 	|| LFSR PRNG (http://codebase64.org/doku.php?id=base:small_fast_16-bit_prng)
  move.w 0xC(sp),d0             | Current PRNG seed
  beq .eor                      | Change zero seed to non-zero value
  lsl.w #0x1,d0
  beq .skip_eor                 | Change 0x8000 seed to zero
  bcc .skip_eor                 | If carry zero XOR is no-op
.eor:
  eor.w #0xC2DF,d0              | XOR with magic
.skip_eor:
  move.w d0,0xC(sp)             | Update PRNG seed
  rts
