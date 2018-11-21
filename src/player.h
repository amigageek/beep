#ifndef BEEP_PLAYER_H
#define BEEP_PLAYER_H

#include "common.h"

BOOL player_init();
VOID player_fini();
VOID player_start(BYTE* samples,
                  UWORD num_samples,
                  UWORD period);
VOID player_stop();

#endif
