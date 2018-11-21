#ifndef BEEP_COMMON_H
#define BEEP_COMMON_H

#include <exec/types.h>

// Divide and round to nearest integer.
#define DIV_ROUND_NEAREST(a, b) ((((a) >= 0) == ((b) >= 0) ? ((a) + ((b) / 2)) : ((a) - ((b) / 2))) / (b))

// Divide and round away from zero (b must be non-negative).
#define DIV_ROUND_LARGEST_NN(a, b) ((((a) >= 0) ? ((a) + ((b) - 1)) : ((a) - ((b) - 1))) / (b))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define CHECK(x)     \
  if (! (x)) {       \
    print_error(#x); \
    ret = FALSE;     \
    goto cleanup;    \
  }

#define kBitsPerByte 0x8
#define kBitsPerWord 0x10
#define kUByteMax ((1 << kBitsPerByte) - 1)
#define kWordMax ((1 << (kBitsPerWord - 1)) - 1)
#define kUWordMax ((1 << kBitsPerWord) - 1)
#define kSinTableSize 0x100
#define kTanTableSize 0x100
#define kDbScaleRange 40 // 0-N dB
#define kDbScaleSteps 5 // fractional steps per dB
#define kDbScaleTableSize (kDbScaleRange * kDbScaleSteps + 1)
#define kPenBG 0
#define kPenDark 1
#define kPenShadow 2
#define kPenColor 3

typedef enum {
  Semi_C, Semi_CS, Semi_D, Semi_DS, Semi_E, Semi_F, Semi_FS, Semi_G, Semi_GS, Semi_A, Semi_AS, Semi_B
} Semitone;

typedef enum {
  PTOct_1, PTOct_2, PTOct_3
} PTOctave;

typedef struct {
  Semitone semitone;
  PTOctave pt_octave;
} PTNote;

typedef struct {
  UBYTE attack;
  UBYTE decay;
  UBYTE sustain;
  UBYTE release;
} Envelope;

typedef enum {
  Wave_Square, Wave_Sawtooth, Wave_Triangle, Wave_Noise, kNumWaves
} Wave;

extern UWORD abs(WORD value);
extern UWORD str_len(STRPTR str);
extern VOID print_error(STRPTR msg);

extern WORD SinTable[kSinTableSize];
extern UWORD TanTable[kTanTableSize];
extern UWORD DbScaleTable[kDbScaleTableSize];

// kSinTableSize entries with range [0, (2*PI)-delta].
static WORD sin_lookup(UWORD entry) {
  return SinTable[entry & (kSinTableSize - 1)];
}

// kSinTableSize entries with range [0, (2*PI)-delta].
static WORD cos_lookup(UWORD entry) {
  return sin_lookup(entry + (kSinTableSize / 4));
}

// kTanTableSize entries with range [0, (PI/4)-delta].
static UWORD tan_lookup(UWORD entry) {
  return TanTable[entry];
}

// Decibel to linear amplitude scale factor lookup.
// kDbScaleTableSize entries with range [-kDbScaleTableRange, kDbScaleTableRange].
static UWORD db_scale_lookup(UWORD entry) {
  return DbScaleTable[entry];
}

#endif
