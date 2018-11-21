#include "common.h"
#include "model.h"
#include "player.h"
#include "synth.h"
#include "ui.h"
#include "widgets.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>

#define kOSLibVer 33 // Kickstart 1.2

struct DosLibrary* DOSBase;
struct IntuitionBase* IntuitionBase;
struct GfxBase* GfxBase;
struct ExecBase* SysBase;

int main() {
  BOOL ret = TRUE;

  CHECK(DOSBase = (struct DosLibrary*)OpenLibrary("dos.library", kOSLibVer));
  CHECK(IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library", kOSLibVer));
  CHECK(GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", kOSLibVer));
  CHECK(SysBase = (struct ExecBase*)OpenLibrary("exec.library", kOSLibVer));

  CHECK(synth_init());
  CHECK(player_init());
  CHECK(model_init());
  CHECK(widgets_init());
  CHECK(ui_init());

  CHECK(ui_handle_events());

cleanup:
  ui_fini();
  widgets_fini();
  model_fini();
  player_fini();
  synth_fini();

  CloseLibrary((struct Library*)SysBase);
  CloseLibrary((struct Library*)GfxBase);
  CloseLibrary((struct Library*)IntuitionBase);
  CloseLibrary((struct Library*)DOSBase);

  return (ret ? RETURN_OK : RETURN_FAIL);
}
