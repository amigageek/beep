#include "exporter.h"

#include <datatypes/soundclass.h>
#include <proto/dos.h>

extern struct DosLibrary* DOSBase;

static struct {
  UWORD dummy;
} g;

BOOL exporter_save(BYTE* samples,
                   UWORD num_samples,
                   UWORD rate_freq) {
  BOOL ret = TRUE;
  BPTR file = NULL;

  struct VoiceHeader vhdr = {
    .vh_OneShotHiSamples = num_samples,
    .vh_RepeatHiSamples = 0,
    .vh_SamplesPerHiCycle = 0,
    .vh_SamplesPerSec = rate_freq,
    .vh_Octaves = 1,
    .vh_Compression = CMP_NONE,
    .vh_Volume = Unity,
  };

  ULONG vhdr_hdr[] = { ID_VHDR, sizeof(vhdr) };
  ULONG body_hdr[] = { ID_BODY, num_samples };
  ULONG form_hdr[] = { ID_FORM, sizeof(vhdr_hdr) + sizeof(vhdr) + sizeof(body_hdr) + num_samples };

  CHECK(file = Open("beep.8svx", MODE_NEWFILE));
  CHECK(Write(file, (VOID*)form_hdr, sizeof(form_hdr)) >= 0);
  CHECK(Write(file, (VOID*)vhdr_hdr, sizeof(vhdr_hdr)) >= 0);
  CHECK(Write(file, (VOID*)&vhdr, sizeof(vhdr)) >= 0);
  CHECK(Write(file, (VOID*)body_hdr, sizeof(body_hdr)) >= 0);
  CHECK(Write(file, (VOID*)samples, num_samples) >= 0);

cleanup:
  if (file) {
    Close(file);
  }

  return ret;
}
