#include "player.h"

#include <clib/alib_protos.h>
#include <devices/audio.h>
#include <proto/exec.h>

#define kNumChans 2
#define kAudioVol 0x20//0x40
#define kAudioPri 50
#define kChanLeft1 (1 << 1)
#define kChanLeft2 (1 << 2)
#define kChanRight1 (1 << 0)
#define kChanRight2 (1 << 3)

static struct {
  BOOL any_audio_sent;
  struct MsgPort* audio_mp[kNumChans];
  struct IOAudio* audio_io[kNumChans];
} g;

BOOL player_init() {
  BOOL ret = TRUE;

  g.any_audio_sent = FALSE;

  for (UWORD ch = 0; ch < kNumChans; ++ ch) {
    CHECK(g.audio_mp[ch] = CreatePort(NULL, 0));
    CHECK(g.audio_io[ch] = (struct IOAudio*)CreateExtIO(g.audio_mp[ch], sizeof(struct IOAudio)));
    g.audio_io[ch]->ioa_Request.io_Message.mn_Node.ln_Pri = kAudioPri;
  }

  UBYTE chan_masks[] = {
    kChanLeft1 | kChanRight1,
    kChanLeft1 | kChanRight2,
    kChanLeft2 | kChanRight1,
    kChanLeft2 | kChanRight2,
  };

  g.audio_io[0]->ioa_Request.io_Command = ADCMD_ALLOCATE;
  g.audio_io[0]->ioa_Request.io_Flags = ADIOF_NOWAIT;
  g.audio_io[0]->ioa_AllocKey = 0;
  g.audio_io[0]->ioa_Data = chan_masks;
  g.audio_io[0]->ioa_Length = sizeof(chan_masks);

  CHECK(OpenDevice("audio.device", 0, (struct IORequest*)g.audio_io[0], 0) == 0);

  g.audio_io[1]->ioa_Request.io_Device = g.audio_io[0]->ioa_Request.io_Device;
  g.audio_io[1]->ioa_AllocKey = g.audio_io[0]->ioa_AllocKey;

  ULONG chan_mask = (ULONG)g.audio_io[0]->ioa_Request.io_Unit;
  g.audio_io[0]->ioa_Request.io_Unit = (struct Unit*)(chan_mask & (kChanLeft1 | kChanLeft2));
  g.audio_io[1]->ioa_Request.io_Unit = (struct Unit*)(chan_mask & (kChanRight1 | kChanRight2));

cleanup:
  return ret;
}

VOID player_fini() {
  if (g.audio_io[0]) {
    player_stop();
    CloseDevice((struct IORequest*)g.audio_io[0]);
  }

  for (UWORD ch = 0; ch < kNumChans; ++ ch) {
    DeleteExtIO((struct IORequest*)g.audio_io[ch]);

    if (g.audio_mp[ch]) {
      DeletePort(g.audio_mp[ch]);
    }
  }
}

VOID player_start(BYTE* samples,
                  UWORD num_samples,
                  UWORD period) {
  for (UWORD ch = 0; ch < kNumChans; ++ ch) {
    g.audio_io[ch]->ioa_Request.io_Command = CMD_WRITE;
    g.audio_io[ch]->ioa_Request.io_Flags = ADIOF_PERVOL;
    g.audio_io[ch]->ioa_Data = samples;
    g.audio_io[ch]->ioa_Length = num_samples;
    g.audio_io[ch]->ioa_Period = period;
    g.audio_io[ch]->ioa_Volume = kAudioVol;
    g.audio_io[ch]->ioa_Cycles = 1;
  }

  for (UWORD ch = 0; ch < kNumChans; ++ ch) {
    BeginIO((struct IORequest*)g.audio_io[ch]);
  }

  g.any_audio_sent = TRUE;
}

VOID player_stop() {
  // Only abort/wait if at least one audio command has been sent.
  // Otherwise WaitIO will hang due to ln_Type set by OpenDevice.
  if (g.any_audio_sent) {
    for (UWORD ch = 0; ch < kNumChans; ++ ch) {
      AbortIO((struct IORequest*)g.audio_io[ch]);
      WaitIO((struct IORequest*)g.audio_io[ch]);
    }
  }
}
