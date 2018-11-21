#ifndef BEEP_UI_H
#define BEEP_UI_H

#include "common.h"

#include <intuition/intuition.h>

BOOL ui_init();
VOID ui_fini();
BOOL ui_handle_events();
struct Window* ui_get_window();

#endif
