#include <math.h>
#include <stdio.h>

#define kSinTableSize 0x100
#define kTanTableSize 0x100
#define kDbScaleRange 40 // 0-N dB
#define kDbScaleSteps 5 // N fractional steps per dB
#define kDbScaleTableSize (kDbScaleRange * kDbScaleSteps + 1)

int main() {
  printf("WORD SinTable[kSinTableSize] = {");

  for (int i = 0; i < kSinTableSize; ++ i) {
    double ang = (double)i / (double)kSinTableSize * 2.0 * M_PI;
    double ang_sin = sin(ang);
    short ang_sin_fix = (short)round(ang_sin * 32767.0);

    if ((i & 7) == 0) {
      printf("\n ");
    }

    printf(" 0x%04hX,", ang_sin_fix);
  }

  printf("\n};\n\n");
  printf("UWORD TanTable[kTanTableSize] = {");

  for (int i = 0; i < kTanTableSize; ++ i) {
    double ang = (double)i / (double)kTanTableSize * M_PI / 4.0;
    double ang_tan = tan(ang);
    short ang_tan_fix = (short)round(ang_tan * 65535.0);

    if ((i & 7) == 0) {
      printf("\n ");
    }

    printf(" 0x%04hX,", ang_tan_fix);
  }
  printf("\n};\n\n");
  printf("UWORD DbScaleTable[kDbScaleTableSize] = {");

  for (int i = 0; i < kDbScaleTableSize; ++ i) {
    double db = i / (double)kDbScaleSteps;
    double scale = pow(10.0, db / 20.0);
    unsigned short scale_fix = (unsigned short)round(scale * 256.0);

    if ((i & 7) == 0) {
      printf("\n ");
    }

    printf(" 0x%04hX,", scale_fix);
  }

  printf("\n};\n");
}
