#ifndef BEEP_WIDGETS_H
#define BEEP_WIDGETS_H

#include "common.h"

#include <graphics/gfx.h>

typedef struct Widget Widget;

struct Widget {
  UWORD pos_tl[2];
  UWORD pos_br[2];
  VOID (*render)(Widget* widget);
  VOID (*clicked)(Widget* widget,
                  UWORD mouse_rel[2]);
  VOID (*released)(Widget* widget,
                   UWORD out_rel[2]);
  VOID (*dragged)(Widget* widget,
                  WORD delta_x,
                  WORD delta_y);
  VOID (*free)(Widget* widget);
};

typedef struct {
  Widget widget;
  struct BitMap bitmap;
  UWORD rotation;
  WORD value_min;
  UWORD value_step;
  UWORD value_scale;
  WORD value;
  UWORD last_render_step;
  VOID (*value_changed)(Widget* widget,
                        WORD value);
} KnobWidget;

typedef struct {
  Widget widget;
  UWORD vertices[3][2];
  UWORD selected_vert;
  VOID (*env_changed)(Widget* widget,
                      Envelope* env);
} EnvWidget;

typedef struct {
  Widget widget;
  Wave waves[2];
  UWORD drag_amt[2];
  UWORD selected_half;
  VOID (*value_changed)(Widget* widget,
                        Wave osc1_wave,
                        Wave osc2_wave);
} WaveWidget;

BOOL widgets_init();
VOID widgets_fini();
BOOL widgets_make_knob(UWORD pos_x,
                       UWORD pos_y,
                       WORD value_min,
                       WORD value_max,
                       UWORD value_step,
                       WORD value,
                       VOID (*value_changed)(Widget* widget,
                                             WORD value),
                       Widget** out_widget);
BOOL widgets_make_env(UWORD pos_x,
                      UWORD pos_y,
                      Envelope* value,
                      VOID (*value_changed)(Widget* widget,
                                            Envelope* env),
                      Widget** out_widget);
BOOL widgets_make_wave(UWORD pos_x,
                       UWORD pos_y,
                       Wave osc1_wave,
                       Wave osc2_wave,
                       VOID (*value_changed)(Widget* widget,
                                             Wave osc1_wave,
                                             Wave osc2_wave),
                       Widget** out_widget);
#endif
