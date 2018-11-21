#ifndef BEEP_EXPORTER_H
#define BEEP_EXPORTER_H

#include "common.h"

BOOL exporter_save(BYTE* samples,
                   UWORD num_samples,
                   UWORD rate_freq);

#endif
