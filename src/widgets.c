#include "widgets.h"
#include "ui.h"

#include <clib/alib_protos.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#define kBplDepth 2
#define kShadowGap 1
#define kKnobEdge 0x20
#define kKnobArea (kKnobEdge * kKnobEdge)
#define kKnobNumSteps 0x20
#define kKnobStepMin (kKnobNumSteps / 0x8)
#define kKnobStepMax (kKnobNumSteps - kKnobStepMin)
#define kKnobRotRange 0x4000
#define kKnobRotMax (kKnobRotRange - 1)
#define kEnvWidth 0x50
#define kEnvHeight 0x20
#define kEnvVertRange 0x1000
#define kEnvVertMax (kEnvVertRange - 1)
#define kEnvVertEnvScale (kEnvVertRange / kUByteMax)
#define kEnvVertDispXScale DIV_ROUND_LARGEST_NN(kEnvVertRange, kEnvWidth - kShadowGap)
#define kEnvVertDispYScale DIV_ROUND_LARGEST_NN(kEnvVertRange, kEnvHeight - kShadowGap)
#define kEnvNumVerts 3
#define kWaveEdge 0x20
#define kWaveBplHeight 0xE
#define kWaveBplGap (0x10 - kWaveBplHeight)
#define kWaveDragRange 0x2000
#define kWaveDragMax (kWaveDragRange - 1)
#define kAreaBufNumVecs 8 // longest chain of [Area*, AreaEnd] calls (AreaCircle = 2 vectors)
#define kAreaBufBytesPerVec 5
#define kTmpRasBufSizeB (kKnobArea / kBitsPerByte)

static struct {
  struct BitMap env_lines_bmap;
  struct RastPort env_lines_rport;
} g;

static UWORD __chip KnobBpls[kKnobNumSteps][kBplDepth][kKnobArea / kBitsPerWord];
static UWORD __chip TmpRasBuf[DIV_ROUND_LARGEST_NN(kTmpRasBufSizeB, sizeof(UWORD))];
static UWORD __chip EnvLinesBpls[kBplDepth][kEnvWidth * kEnvHeight / kBitsPerWord];
static UWORD __chip WaveBpls[kNumWaves][kBplDepth][kWaveEdge * kWaveBplHeight / kBitsPerWord];

static BOOL make_knob_bpls() {
  BOOL ret = TRUE;

  // Build a template bitmap consisting of the unhighlighted ring.
  struct BitMap tmpl_bmap = {
    .BytesPerRow = kKnobEdge / kBitsPerByte,
    .Rows = kKnobEdge,
    .Flags = 0,
    .Depth = kBplDepth,
  };

  for (UWORD i = 0; i < kBplDepth; ++ i) {
    tmpl_bmap.Planes[i] = (PLANEPTR)&KnobBpls[0][i][0];
  }

  struct AreaInfo area_info;
  UWORD area_info_buf[DIV_ROUND_LARGEST_NN(kAreaBufNumVecs * kAreaBufBytesPerVec, sizeof(UWORD))] = { 0 };
  InitArea(&area_info, area_info_buf, kAreaBufNumVecs);

  struct TmpRas tmp_ras;
  InitTmpRas(&tmp_ras, (VOID*)TmpRasBuf, kTmpRasBufSizeB);

  struct RastPort tmpl_rport;
  InitRastPort(&tmpl_rport);
  tmpl_rport.BitMap = &tmpl_bmap;
  tmpl_rport.AreaInfo = &area_info;
  tmpl_rport.TmpRas = &tmp_ras;

  // Ring conists of two filled circles, FG color inside and BG outside.
  UWORD knob_center = kKnobEdge / 2 - 1;
  UWORD ring_outer_radius = knob_center;
  UWORD ring_inner_radius = ring_outer_radius - 5;

  SetAPen(&tmpl_rport, kPenDark);
  CHECK(AreaCircle(&tmpl_rport, knob_center, knob_center, ring_outer_radius) == 0);
  AreaEnd(&tmpl_rport);

  SetAPen(&tmpl_rport, kPenBG);
  CHECK(AreaCircle(&tmpl_rport, knob_center, knob_center, ring_inner_radius) == 0);
  AreaEnd(&tmpl_rport);

  // Erase lower quadrant of the ring with a filled polygon.
  // Edges consist of radials to/from center and lines to/along bounding box.
  UWORD step_angle_start = kKnobStepMin * (kSinTableSize / kKnobNumSteps);
  UWORD step_angle_end = kKnobStepMax * (kSinTableSize / kKnobNumSteps);
  UWORD ring_outer_scale = DIV_ROUND_NEAREST(kWordMax, ring_outer_radius);
  UWORD ring_outer_start_x = knob_center - DIV_ROUND_NEAREST(sin_lookup(step_angle_start), ring_outer_scale);
  UWORD ring_outer_start_y = knob_center + DIV_ROUND_NEAREST(cos_lookup(step_angle_start), ring_outer_scale);
  UWORD ring_outer_end_x = knob_center - DIV_ROUND_NEAREST(sin_lookup(step_angle_end), ring_outer_scale);
  UWORD ring_outer_end_y = knob_center + DIV_ROUND_NEAREST(cos_lookup(step_angle_end), ring_outer_scale);

  CHECK(AreaMove(&tmpl_rport, ring_outer_start_x, kKnobEdge - 1) == 0);
  CHECK(AreaDraw(&tmpl_rport, ring_outer_start_x, ring_outer_start_y) == 0);
  CHECK(AreaDraw(&tmpl_rport, knob_center, knob_center) == 0);
  CHECK(AreaDraw(&tmpl_rport, ring_outer_end_x, ring_outer_end_y) == 0);
  CHECK(AreaDraw(&tmpl_rport, ring_outer_end_x, kKnobEdge - 1) == 0);
  AreaEnd(&tmpl_rport);

  // Generate a bitmap for each rotated instance of the knob.
  for (UWORD step = kKnobStepMin; step <= kKnobStepMax; ++ step) {
    // Copy plane 0 of the template to plane 1 of the new bitmap.
    // This changes the ring to the highlighted color.
    struct BitMap dest_bmap = tmpl_bmap;
    dest_bmap.Depth = 1;
    dest_bmap.Planes[0] = (PLANEPTR)&KnobBpls[step][1][0];

    WaitBlit();
    BltBitMap(&tmpl_bmap, 0, 0, &dest_bmap, 0, 0, kKnobEdge, kKnobEdge, 0xC0, 0x1, NULL);
    WaitBlit();

    // Erase the unhighlighted segment of the ring with a filled polygon.
    // Edges consist of radials from ring outer start, to center, to end of highlighted segment.
    struct RastPort dest_rport = tmpl_rport;
    dest_rport.BitMap = &dest_bmap;

    UWORD step_angle = step * (kSinTableSize / kKnobNumSteps);
    UWORD ring_outer_hl_end_x = knob_center - DIV_ROUND_NEAREST(sin_lookup(step_angle), ring_outer_scale);
    UWORD ring_outer_hl_end_y = knob_center + DIV_ROUND_NEAREST(cos_lookup(step_angle), ring_outer_scale);

    CHECK(AreaMove(&dest_rport, ring_outer_start_x, ring_outer_start_y) == 0);
    CHECK(AreaDraw(&dest_rport, knob_center, knob_center) == 0);
    CHECK(AreaDraw(&dest_rport, ring_outer_hl_end_x, ring_outer_hl_end_y) == 0);

    UWORD next_edge_x = ring_outer_hl_end_x;
    UWORD next_edge_y = ring_outer_hl_end_y;

    if (step < (kKnobNumSteps / 4)) {
      // In lower-left quadrant draw an edge to the left.
      next_edge_x = 0;
      CHECK(AreaDraw(&dest_rport, next_edge_x, next_edge_y) == 0);
    }

    if (step < (2 * kKnobNumSteps / 4)) {
      // In upper-left quadrant draw an edge to the top.
      next_edge_y = 0;
      CHECK(AreaDraw(&dest_rport, next_edge_x, next_edge_y) == 0);
    }

    if (step < (3 * kKnobNumSteps / 4)) {
      // In upper-right quadrant draw an edge to the right.
      next_edge_x = kKnobEdge - 1;
      CHECK(AreaDraw(&dest_rport, next_edge_x, next_edge_y) == 0);
    }

    // Finish with an edge to the bottom-right, connecting back to ring outer start.
    CHECK(AreaDraw(&dest_rport, kKnobEdge - 1, kKnobEdge - 1) == 0);
    AreaEnd(&dest_rport);
  }

  // Draw inner knob into the template, now that outer ring copy+cut is done.
  UWORD inner_knob_radius = ring_inner_radius - 2;

  SetAPen(&tmpl_rport, kPenDark);
  CHECK(AreaCircle(&tmpl_rport, knob_center, knob_center, inner_knob_radius) == 0);
  AreaEnd(&tmpl_rport);

  // Create a shadow in the second bitplane.
  struct BitMap shad_bmap = tmpl_bmap;
  shad_bmap.Planes[0] = (PLANEPTR)&KnobBpls[0][1][0];

  WaitBlit();
  BltBitMap(&tmpl_bmap, 0, 0, &shad_bmap, 1, 1, kKnobEdge - 1, kKnobEdge - 1, 0xC0, 1, NULL);
  BltBitMap(&tmpl_bmap, 0, 0, &shad_bmap, 0, 0, kKnobEdge, kKnobEdge, 0x20, 1, NULL);

  // Merge template bitplanes onto rotated knob instances.
  // Adds the unhighlighted ring segment and inner knob.

  for (UWORD step = kKnobStepMin; step <= kKnobStepMax; ++ step) {
    struct BitMap dest_bmap = tmpl_bmap;

    for (UWORD i = 0; i < kBplDepth; ++ i) {
      dest_bmap.Planes[i] = (PLANEPTR)&KnobBpls[step][i][0];
    }

    BltBitMap(&tmpl_bmap, 0, 0, &dest_bmap, 0, 0, kKnobEdge, kKnobEdge, 0xE0, (1 << kBplDepth) - 1, NULL);
  }

cleanup:
  return ret;
}

static BOOL make_wave_bpls() {
  struct BitMap wave_bmap = {
    .BytesPerRow = kWaveEdge / kBitsPerByte,
    .Rows = kWaveBplHeight,
    .Flags = 0,
    .Depth = kBplDepth,
  };

  struct RastPort wave_rport;
  InitRastPort(&wave_rport);
  wave_rport.BitMap = &wave_bmap;

  SetAPen(&wave_rport, kPenColor);

  for (UWORD wave_idx = 0; wave_idx < 4; ++ wave_idx) {
    for (UWORD i = 0; i < kBplDepth; ++ i) {
      wave_bmap.Planes[i] = (PLANEPTR)&WaveBpls[wave_idx][i][0];
    }

    UWORD line_x_gap = 3;//kWaveBplGap/* + kShadowGap*/;
    UWORD line_height = kWaveBplHeight - kShadowGap;

    if (wave_idx == Wave_Noise) {
      ULONG seed = 0x1337C0DE;

      Move(&wave_rport, line_x_gap, line_height / 2);

      for (UWORD x = line_x_gap; x < (kWaveEdge - 1 - line_x_gap); ++ x) {
        seed = FastRand(seed);
        Draw(&wave_rport, x, (UWORD)seed % line_height);
      }
    }
    else {
      WORD pts[4][2];

      UWORD line_x_mid = (kWaveEdge / 2) - 1;

      switch (wave_idx) {
      case Wave_Square:
        pts[0][0] = line_x_gap;                 pts[0][1] = 0;
        pts[1][0] = line_x_mid;                 pts[1][1] = 0;
        pts[2][0] = line_x_mid;                 pts[2][1] = line_height - 1;
        pts[3][0] = kWaveEdge - 2 - line_x_gap; pts[3][1] = line_height - 1;
        break;

      case Wave_Sawtooth:
        pts[0][0] = line_x_gap;                 pts[0][1] = line_height / 2;
        pts[1][0] = line_x_mid;                 pts[1][1] = 0;
        pts[2][0] = line_x_mid;                 pts[2][1] = line_height - 1;
        pts[3][0] = kWaveEdge - 2 - line_x_gap; pts[3][1] = line_height / 2;
        break;

      case Wave_Triangle:
        pts[0][0] = line_x_gap;                 pts[0][1] = line_height / 2;
        pts[1][0] = line_x_gap + 6;             pts[1][1] = 0;
        pts[2][0] = kWaveEdge - 8 - line_x_gap; pts[2][1] = line_height - 1;
        pts[3][0] = kWaveEdge - 2 - line_x_gap; pts[3][1] = line_height / 2;
        break;
      }

      Move(&wave_rport, pts[0][0], pts[0][1]);
      PolyDraw(&wave_rport, 2, (WORD*)&pts[1]);
      Move(&wave_rport, pts[3][0], pts[3][1]);
      Draw(&wave_rport, pts[2][0], pts[2][1]);
    }

    // Create a shadow in the second bitplane.
    struct BitMap shad_bmap = wave_bmap;
    shad_bmap.Planes[0] = (PLANEPTR)&WaveBpls[wave_idx][1][0];

    WaitBlit();
    BltBitMap(&wave_bmap, 0, 0, &shad_bmap, 1, 1, kWaveEdge - 1, kWaveBplHeight - 1, 0xE0, 1, NULL);
  }

  return TRUE;
}

BOOL widgets_init() {
  BOOL ret = TRUE;

  g.env_lines_bmap.BytesPerRow = kEnvWidth / kBitsPerByte;
  g.env_lines_bmap.Rows = kEnvHeight;
  g.env_lines_bmap.Flags = 0;
  g.env_lines_bmap.Depth = kBplDepth;

  for (UWORD i = 0; i < kBplDepth; ++ i) {
    g.env_lines_bmap.Planes[i] = (PLANEPTR)&EnvLinesBpls[i][0];
  }

  InitRastPort(&g.env_lines_rport);
  g.env_lines_rport.BitMap = &g.env_lines_bmap;

  CHECK(make_knob_bpls());
  CHECK(make_wave_bpls());

cleanup:
  return ret;
}

VOID widgets_fini() {
}

static VOID knob_render(Widget* widget) {
  KnobWidget* knob = (KnobWidget*)widget;

  UWORD step = kKnobStepMin + ((knob->rotation * (kKnobStepMax - kKnobStepMin + 1)) / kKnobRotRange);

  if (step != knob->last_render_step) {
    knob->last_render_step = step;

    for (UWORD i = 0; i < kBplDepth; ++ i) {
      knob->bitmap.Planes[i] = (PLANEPTR)&KnobBpls[step][i][0];
    }

    BltBitMap(&knob->bitmap, 0, 0, ui_get_window()->RPort->BitMap,
              widget->pos_tl[0], widget->pos_tl[1],
              kKnobEdge, kKnobEdge, 0xC0, (1 << kBplDepth) - 1, NULL);
  }
}

static VOID knob_dragged(Widget* widget,
                         WORD delta_x,
                         WORD delta_y) {
  KnobWidget* knob = (KnobWidget*)widget;

  // Adjust knob rotation proportionally to vertical mouse movement.
  knob->rotation = MAX(0, MIN(kKnobRotMax, knob->rotation - delta_y));
  widget->render(widget);

  // Calculate and propagate new knob value.
  WORD value = knob->value_min + (((knob->rotation * knob->value_scale) / kKnobRotRange) * knob->value_step);

  if (value != knob->value) {
    knob->value = value;
    knob->value_changed(widget, value);
  }
}

static VOID knob_free(Widget* widget) {
  FreeMem(widget, sizeof(KnobWidget));
}

BOOL widgets_make_knob(UWORD pos_x,
                       UWORD pos_y,
                       WORD value_min,
                       WORD value_max,
                       UWORD value_step,
                       WORD value,
                       VOID (*value_changed)(Widget* widget,
                                             WORD value),
                       Widget** out_widget) {
  BOOL ret = TRUE;

  // Initial rotation half-way through step corresponding to initial value.
  UWORD rotation = ((value - value_min) * kKnobRotRange + (kKnobRotRange / 2)) / (value_max - value_min + 1);

  KnobWidget knob_widget = {
    .widget = {
      .pos_tl = { pos_x, pos_y },
      .pos_br = { pos_x + kKnobEdge - 1, pos_y + kKnobEdge - 1 },
      .render = knob_render,
      .clicked = NULL,
      .released = NULL,
      .dragged = knob_dragged,
      .free = knob_free,
    },
    .bitmap = {
      .BytesPerRow = kKnobEdge / kBitsPerByte,
      .Rows = kKnobEdge,
      .Flags = 0,
      .Depth = kBplDepth,
    },
    .rotation = rotation,
    .value = value,
    .value_min = value_min,
    .value_step = value_step,
    .value_scale = (value_max - value_min) / value_step + 1,
    .last_render_step = 0,
    .value_changed = value_changed,
  };

  CHECK(*out_widget = (Widget*)AllocMem(sizeof(KnobWidget), 0));
  CopyMem((APTR)&knob_widget, (APTR)*out_widget, sizeof(KnobWidget));

  value_changed(*out_widget, value);

cleanup:
  return ret;
}

static VOID env_redraw_lines(Widget* widget) {
  EnvWidget* env = (EnvWidget*)widget;
  WORD env_lines[kEnvNumVerts + 1][2];

  for (UWORD i = 0; i < kEnvNumVerts; ++ i) {
    env_lines[i][0] = env->vertices[i][0] / kEnvVertDispXScale;
    env_lines[i][1] = env->vertices[i][1] / kEnvVertDispYScale;
  }

  env_lines[kEnvNumVerts][0] = kEnvWidth - 1 - kShadowGap;
  env_lines[kEnvNumVerts][1] = kEnvHeight - 1 - kShadowGap;

  BltClear((VOID*)EnvLinesBpls, sizeof(EnvLinesBpls), 1);
  SetAPen(&g.env_lines_rport, kPenColor);
  SetDrPt(&g.env_lines_rport, 0xFFFF);
  Move(&g.env_lines_rport, 0, kEnvHeight - 1 - kShadowGap);
  PolyDraw(&g.env_lines_rport, ARRAY_SIZE(env_lines), (WORD*)env_lines);

  // Create a shadow in the second bitplane.
  struct BitMap shad_bmap = g.env_lines_bmap;
  shad_bmap.Planes[0] = (PLANEPTR)g.env_lines_bmap.Planes[1];

  WaitBlit();
  BltBitMap(&g.env_lines_bmap, 0, 0, &shad_bmap, 1, 1,
            kEnvWidth - 1, kEnvHeight - 1, 0xE0, 1, NULL);

  // Copy lines into the window.
  BltBitMap(&g.env_lines_bmap, 0, 0, ui_get_window()->RPort->BitMap,
            widget->pos_tl[0], widget->pos_tl[1] - 1,
            kEnvWidth, kEnvHeight, 0xC0, (1 << kBplDepth) - 1, NULL);

}

static VOID env_render(Widget* widget) {
  env_redraw_lines(widget);
}

static VOID env_clicked(Widget* widget,
                        UWORD mouse_rel[2]) {
  EnvWidget* env = (EnvWidget*)widget;
  UWORD nearest_vert = 0;
  UWORD nearest_dist = kUWordMax;

  for (UWORD i = 0; i < kEnvNumVerts; ++ i) {
    WORD dist_x = mouse_rel[0] - (env->vertices[i][0] / kEnvVertDispXScale);
    WORD dist_y = mouse_rel[1] - (env->vertices[i][1] / kEnvVertDispYScale);
    UWORD dist = (dist_x * dist_x) + (dist_y * dist_y);

    if (dist < nearest_dist) {
      nearest_vert = i;
      nearest_dist = dist;
    }
  }

  env->selected_vert = nearest_vert;
}

static VOID env_dragged(Widget* widget,
                        WORD delta_x,
                        WORD delta_y) {
  EnvWidget* env = (EnvWidget*)widget;
  UWORD* vertex = &env->vertices[env->selected_vert][0];

  switch(env->selected_vert) {
  case 0:
    // Attack vertex lies between left edge and decay vertex.
    vertex[0] = MAX(kEnvVertDispXScale,
                    MIN(env->vertices[1][0] - kEnvVertDispXScale, vertex[0] + delta_x));
    break;
  case 1:
    // Decay vertex lies between attack and sustain vertices.
    vertex[0] = MAX(env->vertices[0][0] + kEnvVertDispXScale,
                    MIN(env->vertices[2][0] - kEnvVertDispXScale, vertex[0] + delta_x));
    vertex[1] = MAX(0, MIN(kEnvVertMax, vertex[1] + delta_y));

    // Decay height linked to sustain height.
    env->vertices[2][1] = vertex[1];
    break;
  case 2:
    // Sustain vertex lies between sustain vertex and right edge.
    vertex[0] = MAX(env->vertices[1][0] + kEnvVertDispXScale,
                    MIN(kEnvVertMax - kEnvVertDispXScale, vertex[0] + delta_x));
    vertex[1] = MAX(0, MIN(kEnvVertMax, vertex[1] + delta_y));

    // Sustain height linked to decay height.
    env->vertices[1][1] = vertex[1];
    break;
  }

  env_redraw_lines(widget);

  Envelope envelope = {
    .attack = (env->vertices[0][0] * kUByteMax) / kEnvVertRange,
    .decay = ((env->vertices[1][0] - env->vertices[0][0]) * kUByteMax) / kEnvVertRange,
    .sustain = ((kEnvVertRange - env->vertices[1][1]) * kUByteMax) / kEnvVertRange,
    .release = ((kEnvVertRange - env->vertices[2][0]) * kUByteMax) / kEnvVertRange,
  };

  env->env_changed(widget, &envelope);
}

static VOID env_released(Widget* widget,
                         UWORD out_rel[2]) {
  EnvWidget* env = (EnvWidget*)widget;

  out_rel[0] = 1 + (env->vertices[env->selected_vert][0] / kEnvVertDispXScale);
  out_rel[1] = 1 + (env->vertices[env->selected_vert][1] / kEnvVertDispYScale);
}

static VOID env_free(Widget* widget) {
  FreeMem(widget, sizeof(EnvWidget));
}

BOOL widgets_make_env(UWORD pos_x,
                      UWORD pos_y,
                      Envelope* env,
                      VOID (*env_changed)(Widget* widget,
                                          Envelope* env),
                      Widget** out_widget) {
  BOOL ret = TRUE;

  EnvWidget env_widget = {
    .widget = {
      .pos_tl = { pos_x, pos_y },
      .pos_br = { pos_x + kEnvWidth - 1, pos_y + kEnvHeight - 1 },
      .render = env_render,
      .clicked = env_clicked,
      .released = env_released,
      .dragged = env_dragged,
      .free = env_free,
    },
    .vertices = {
      { env->attack * kEnvVertEnvScale, 0 },
      { (env->attack + env->decay) * kEnvVertEnvScale, (kUByteMax - env->sustain) * kEnvVertEnvScale },
      { (kUByteMax - env->release) * kEnvVertEnvScale, (kUByteMax - env->sustain) * kEnvVertEnvScale },
    },
    .selected_vert = 0,
    .env_changed = env_changed,
  };

  CHECK(*out_widget = (Widget*)AllocMem(sizeof(EnvWidget), 0));
  CopyMem((APTR)&env_widget, (APTR)*out_widget, sizeof(EnvWidget));

  env_changed(*out_widget, env);

cleanup:
  return ret;
}

static VOID wave_render(Widget* widget) {
  WaveWidget* wave_widget = (WaveWidget*)widget;

  struct BitMap wave_bmap = {
    .BytesPerRow = kWaveEdge / kBitsPerByte,
    .Rows = kWaveBplHeight,
    .Flags = 0,
    .Depth = kBplDepth,
  };

  for (UWORD osc_idx = 0; osc_idx < 2; ++ osc_idx) {
    for (UWORD i = 0; i < kBplDepth; ++ i) {
      wave_bmap.Planes[i] = (PLANEPTR)&WaveBpls[wave_widget->waves[osc_idx]][i][0];
    }

    BltBitMap(&wave_bmap, 0, 0, ui_get_window()->RPort->BitMap,
              widget->pos_tl[0], widget->pos_tl[1] + (osc_idx * (kWaveBplHeight + kWaveBplGap)),
              kWaveEdge, kWaveBplHeight, 0xC0, (1 << kBplDepth) - 1, NULL);
  }
}

static VOID wave_clicked(Widget* widget,
                         UWORD mouse_rel[2]) {
  WaveWidget* wave = (WaveWidget*)widget;
  wave->selected_half = (mouse_rel[1] < (kWaveEdge / 2)) ? 0 : 1;
}

static VOID wave_dragged(Widget* widget,
                         WORD delta_x,
                         WORD delta_y) {
  WaveWidget* wave_widget = (WaveWidget*)widget;

  UWORD* drag_amt = &wave_widget->drag_amt[wave_widget->selected_half];
  *drag_amt = MAX(0, MIN(kWaveDragMax, *drag_amt - delta_y));

  UWORD wave = (*drag_amt * kNumWaves) / kWaveDragRange;

  if (wave_widget->waves[wave_widget->selected_half] != wave) {
    wave_widget->waves[wave_widget->selected_half] = wave;
    widget->render(widget);
    wave_widget->value_changed(widget, wave_widget->waves[0], wave_widget->waves[1]);
  }
}

static VOID wave_free(Widget* widget) {
  FreeMem(widget, sizeof(WaveWidget));
}

BOOL widgets_make_wave(UWORD pos_x,
                       UWORD pos_y,
                       Wave osc1_wave,
                       Wave osc2_wave,
                       VOID (*value_changed)(Widget* widget,
                                             Wave osc1_wave,
                                             Wave osc2_wave),
                       Widget** out_widget) {
  BOOL ret = TRUE;

  WaveWidget wave_widget = {
    .widget = {
      .pos_tl = { pos_x, pos_y },
      .pos_br = { pos_x + kWaveEdge - 1, pos_y + kWaveEdge - 1 },
      .render = wave_render,
      .clicked = wave_clicked,
      .released = NULL,
      .dragged = wave_dragged,
      .free = wave_free,
    },
    .waves = {
      osc1_wave, osc2_wave
    },
    .drag_amt = { 0, 0 },
    .selected_half = 0,
    .value_changed = value_changed,
  };

  // Initial drag half-way through step corresponding to initial value.
  for (UWORD i = 0; i < 2; ++ i) {
    wave_widget.drag_amt[i] = ((wave_widget.waves[i] * kWaveDragRange) + (kWaveDragRange / 2)) / kNumWaves;
  }

  CHECK(*out_widget = (Widget*)AllocMem(sizeof(WaveWidget), 0));
  CopyMem((APTR)&wave_widget, (APTR)*out_widget, sizeof(WaveWidget));

  value_changed(*out_widget, wave_widget.waves[0], wave_widget.waves[1]);

 cleanup:
  return ret;
}
