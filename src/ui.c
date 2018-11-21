#include "ui.h"
#include "build/images.h"
#include "model.h"
#include "widgets.h"

#include <clib/alib_protos.h>
#include <devices/input.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

// FIXME: filter toggle

#define kScreenWidth 320
#define kScreenHeight 200
#define kScreenDepth 2
#define kFontWidth 6
#define kFontHeight 6
#define kFontNGlyphsX 0x10
#define kFontNGlyphsY 0x6
#define kFontNGlyphs (kFontNGlyphsX * kFontNGlyphsY)
#define kNumWidgets 14
#define kWidgetTitleGap 4
#define kColorBG 0x222
#define kColorDark 0x555
#define kColorShadow 0x000
#define kColorColor 0x0D4
#define kUINumCols 6
#define kUINumRows 3
#define kUIColStride (kScreenWidth / kUINumCols)
#define kUIRowStride (kScreenHeight / kUINumRows)
#define kUIGapLeft ((kUIColStride - 0x20 + 1) / 2)
#define kUIGapTop ((kScreenHeight - (kUIRowStride * kUINumRows)) / 2)
#define kUIGapRowTop (kUIRowStride - 0x20 - (2 * kFontHeight) - kWidgetTitleGap)
#define kKnobOscMixRange 0, 100, 1
#define kKnobOscDetuneRange 0, 24, 1
#define kKnobOctaveRange 1, 5, 1
#define kKnobLFORange 1, 100, 1
#define kKnobLFOModRange 0, 3, 1
#define kKnobLFOAmtRange 0, 100, 1
#define kKnobCutoffRange 100, 4000, 10
#define kKnobEchoLagRange 1, 50, 1
#define kKnobEchoMixRange 1, 99, 1
#define kKnobLengthRange 100, 1500, 10
#define kKnobRateRange 0, 33, 1
#define kKnobGainRange 0, kDbScaleRange * 10, 10 / kDbScaleSteps
#define kPtrSprEdge 0x10
#define kPtrSprDepth 2
#define kPtrSprHdrSizeW 2
#define kPtrSprOffX -6
#define kPtrSprOffY -1
#define kDragDeltaScale 10
#define kTitleTextGap 4

typedef enum {
  WTT_Title, WTT_Value
} WidgetTextType;

static struct {
  struct Screen* screen;
  struct Window* window;
  struct BitMap shadow_font_bmaps[2];
  Widget* widgets[kNumWidgets];
  struct MsgPort* input_mp;
  struct IOStdReq* input_io;
  struct InputEvent mouse_move_ev;
} g;

static UWORD __chip ShadowFontBpls[2][kScreenDepth][(kFontNGlyphs * kFontWidth * kFontHeight) / kBitsPerWord];
static UWORD __chip PointerSprs[2][(2 * kPtrSprHdrSizeW) + ((kPtrSprEdge * kPtrSprEdge * kPtrSprDepth) / kBitsPerWord)];

// Bits [0:3] = semitone
//      [6]   = key has a valid note assigned
//      [7]   = 0 for low octave, 1 for high octave
static UBYTE KeyDecodeTable[0x80] = {
  0, 0,
  Semi_CS | (3 << 6), // key: 2
  Semi_DS | (3 << 6), // key: 3
  0,
  Semi_FS | (3 << 6), // key: 5
  Semi_GS | (3 << 6), // key: 6
  Semi_AS | (3 << 6), // key: 7
  0, 0, 0, 0, 0, 0, 0, 0,
  Semi_C  | (3 << 6), // key: Q
  Semi_D  | (3 << 6), // key: W
  Semi_E  | (3 << 6), // key: E
  Semi_F  | (3 << 6), // key: R
  Semi_G  | (3 << 6), // key: T
  Semi_A  | (3 << 6), // key: Y
  Semi_B  | (3 << 6), // key: U
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  Semi_CS | (1 << 6), // key: S
  Semi_DS | (1 << 6), // key: D
  0,
  Semi_FS | (1 << 6), // key: G
  Semi_GS | (1 << 6), // key: H
  Semi_AS | (1 << 6), // key: J
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  Semi_C  | (1 << 6), // key: Z
  Semi_D  | (1 << 6), // key: X
  Semi_E  | (1 << 6), // key: C
  Semi_F  | (1 << 6), // key: V
  Semi_G  | (1 << 6), // key: B
  Semi_A  | (1 << 6), // key: N
  Semi_B  | (1 << 6), // key: M
};

static STRPTR SemitoneNames[12] = {
  "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

static BOOL make_shadow_font() {
  BOOL ret = TRUE;

  // FIXME: Put font in chip memory?

  UWORD bpl_width = kFontWidth * kFontNGlyphsX;
  UWORD bpl_height = kFontHeight * kFontNGlyphsY;

  struct BitMap src_bmap = {
    .BytesPerRow = bpl_width / kBitsPerByte,
    .Rows = bpl_height,
    .Flags = 0,
    .Depth = 1,
    .Planes = { (PLANEPTR)font_bpls },
  };

  for (UWORD font_color = 0; font_color < 2; ++ font_color) {
    struct BitMap dest_bmap = src_bmap;

    dest_bmap.Planes[0] = (PLANEPTR)&ShadowFontBpls[font_color][0];
    BltBitMap(&src_bmap, 0, 0, &dest_bmap, 0, 0, bpl_width, bpl_height, 0xC0, 0x1, NULL);

    dest_bmap.Planes[0] = (PLANEPTR)&ShadowFontBpls[font_color][1];
    BltBitMap(&src_bmap, 0, 0, &dest_bmap, 1, 1, bpl_width - 1, bpl_height - 1, 0xC0, 0x1, NULL);
    BltBitMap(&src_bmap, 0, 0, &dest_bmap, 0, 0, bpl_width, bpl_height, (font_color == 0 ? 0x20 : 0xE0), 0x1, NULL);

    g.shadow_font_bmaps[font_color] = dest_bmap;
    g.shadow_font_bmaps[font_color].Depth = kScreenDepth;

    for (UWORD plane = 0; plane < kScreenDepth; ++ plane) {
      g.shadow_font_bmaps[font_color].Planes[plane] = (PLANEPTR)&ShadowFontBpls[font_color][plane];
    }
  }

cleanup:
  return ret;
}

static VOID draw_shadow_text(STRPTR text,
                             UWORD text_len,
                             UWORD text_x,
                             UWORD text_y,
                             UWORD pen) {
  for (UWORD char_idx = 0; char_idx < text_len; ++ char_idx) {
    UWORD glyph_idx = text[char_idx] - 0x20;
    UWORD glyph_x = kFontWidth * (glyph_idx & (kFontNGlyphsX - 1));
    UWORD glyph_y = kFontHeight * (glyph_idx / kFontNGlyphsX);

    BltBitMap(&g.shadow_font_bmaps[pen == kPenDark ? 0 : 1], glyph_x, glyph_y,
              g.window->RPort->BitMap, text_x + (char_idx * kFontWidth), text_y - kFontHeight + 1,
              kFontWidth, kFontHeight, 0xC0, 0x3, NULL);
  }
}

static VOID draw_widget_text(Widget* widget,
                             STRPTR text,
                             UWORD text_len,
                             WORD center_off,
                             WidgetTextType text_type) {
  UWORD text_x =
    ((widget->pos_tl[0] + widget->pos_br[0] + 1) / 2) -
    (((text_len - center_off) * kFontWidth) / 2);

  UWORD text_y = (text_type == WTT_Title) ?
    widget->pos_br[1] + kFontHeight + 1 :
    widget->pos_br[1] + (2 * kFontHeight) + 1 + kWidgetTitleGap;

  draw_shadow_text(text, text_len, text_x, text_y, (text_type == WTT_Title) ? kPenDark : kPenColor);
}

static VOID draw_widget_frames() {
  UWORD frame_y_off = kUIGapTop + (kUIGapRowTop / 2);

  for (UWORD pass = 0; pass < 2; ++ pass) {
    // First draw shadow at (1,1) offset, then frame at (0,0).
    UWORD offset = 1 - pass;
    SetAPen(g.window->RPort, (pass == 0 ? kPenShadow : kPenDark));

    // Full-width frame header for each row of widgets.
    for (UWORD row = 0; row < kUINumRows; ++ row) {
      UWORD row_y = offset + (row * kUIRowStride) + frame_y_off;
      Move(g.window->RPort, offset, row_y);
      Draw(g.window->RPort, offset + kScreenWidth - 1, row_y);
    }

    // Vertical center dividing line.
    UWORD center_x = offset + kScreenWidth / 2 - 1;
    Move(g.window->RPort, center_x, offset + frame_y_off);
    Draw(g.window->RPort, center_x, offset + kScreenHeight - 1);

    // Veritcal diving line at row 2, column 1.
    Move(g.window->RPort, offset + kUIColStride, offset + (1 * kUIRowStride) + frame_y_off);
    Draw(g.window->RPort, offset + kUIColStride, offset + (2 * kUIRowStride) + frame_y_off);
  }

  // Frame heading titles.
  STRPTR frame_titles[] = {
    "VFO SOURCE", "LFO MODULATOR", "FILTER",
    "ECHO", "AMPLITUDE", "SAMPLE OUTPUT"
  };

  UWORD title_centers[][2] = {
    { kScreenWidth / 4,       0                },
    { (kScreenWidth * 3) / 4, 0                },
    { kUIColStride / 2,       kUIRowStride     },
    { kUIColStride * 2,       kUIRowStride     },
    { (kScreenWidth * 3) / 4, kUIRowStride     },
    { kScreenWidth / 4,       kUIRowStride * 2 },
  };

  for (UWORD title_idx = 0; title_idx < ARRAY_SIZE(frame_titles); ++ title_idx) {
    STRPTR text = frame_titles[title_idx];
    UWORD text_len = str_len(text);
    WORD text_w = (text_len * kFontWidth) - 1;
    UWORD text_x = title_centers[title_idx][0] - (text_w / 2);
    UWORD text_y = title_centers[title_idx][1] + frame_y_off + ((kFontHeight + 1) / 2);

    // Clear padded area around text.
    SetAPen(g.window->RPort, kPenBG);
    RectFill(g.window->RPort,
             text_x - kTitleTextGap, text_y - (kFontHeight - 1) - kTitleTextGap,
             text_x + kTitleTextGap + text_w - 1, text_y + kTitleTextGap);
    WaitBlit();

    draw_shadow_text(text, text_len, text_x, text_y, kPenDark);
  }
}

static VOID int_to_str(WORD value,
                       STRPTR str,
                       UWORD width) {
  BOOL negative = (value < 0);

  if (negative) {
    value = - value;
  }

  do {
    UWORD rem = value % 10;
    value /= 10;

    str[-- width] = '0' + rem;
  } while (value != 0);

  if (negative) {
    str[-- width] = '-';
  }
}

static VOID osc_wave_changed(Widget* widget,
                             Wave osc1_wave,
                             Wave osc2_wave) {
  model_set_osc1_wave(osc1_wave);
  model_set_osc2_wave(osc2_wave);
}

static VOID osc_mix_changed(Widget* widget,
                            WORD value) {
  model_set_osc_mix(value);

  BYTE value_str[4] = "   %";
  int_to_str(value, value_str, 3);
  draw_widget_text(widget, value_str, sizeof(value_str), -1, WTT_Value);
}

static VOID osc_detune_changed(Widget* widget,
                               WORD value) {
  model_set_osc_detune(value);

  BYTE value_str[5] = "  /12";
  int_to_str(value, value_str, 2);
  draw_widget_text(widget, value_str, sizeof(value_str), 0, WTT_Value);
}

static VOID lfo_changed(Widget* widget,
                        WORD value) {
  BYTE value_str[6] = "    HZ";
  int_to_str(value, value_str, 3);
  draw_widget_text(widget, value_str, sizeof(value_str), -1, WTT_Value);
}

static VOID lfo_mod_changed(Widget* widget,
                            WORD value) {
  BYTE value_str[3] = "OFF";
  draw_widget_text(widget, value_str, sizeof(value_str), 0, WTT_Value);
}

static VOID lfo_amt_changed(Widget* widget,
                            WORD value) {
  BYTE value_str[4] = "   %";
  int_to_str(value, value_str, 3);
  draw_widget_text(widget, value_str, sizeof(value_str), -1, WTT_Value);
}

static VOID cutoff_changed(Widget* widget,
                           WORD value) {
  model_set_cutoff(value);

  BYTE value_str[7] = "     HZ";
  int_to_str(value, value_str, 4);
  draw_widget_text(widget, value_str, sizeof(value_str), 0, WTT_Value);
}

static VOID echo_lag_changed(Widget* widget,
                             WORD value) {
  BYTE value_str[3] = "  %";
  int_to_str(value, value_str, 2);
  draw_widget_text(widget, value_str, sizeof(value_str), 0, WTT_Value);
}

static VOID echo_mix_changed(Widget* widget,
                             WORD value) {
  BYTE value_str[3] = "  %";
  int_to_str(value, value_str, 2);
  draw_widget_text(widget, value_str, sizeof(value_str), 0, WTT_Value);
}

static VOID gain_changed(Widget* widget,
                         WORD value) {
  model_set_gain_db(value);

  BYTE value_str[5] = "   DB";
  int_to_str(value / 10, value_str, 2);
  draw_widget_text(widget, value_str, sizeof(value_str), -1, WTT_Value);
}

static VOID amp_env_changed(Widget* widget,
                            Envelope* env) {
  model_set_amp_env(env);
}

static VOID octave_changed(Widget* widget,
                           WORD value) {
  model_set_octave_base(value);

  BYTE octave_str[] = { '0' + value, '-', '0' + value + 2 };
  draw_widget_text(widget, octave_str, sizeof(octave_str), 0, WTT_Value);
}

static VOID length_changed(Widget* widget,
                           WORD value) {
  model_set_length_ms(value);

  BYTE value_str[7] = "     MS";
  int_to_str(value, value_str, 4);
  draw_widget_text(widget, value_str, sizeof(value_str), -1, WTT_Value);
}

static VOID rate_changed(Widget* widget,
                         WORD value) {
  PTNote rate = {
    .semitone = value % 12,
    .pt_octave = value / 12,
  };

  model_set_sample_rate(&rate);

  STRPTR semi_name = SemitoneNames[rate.semitone];
  BYTE rate_str[] = { semi_name[0], semi_name[1], '1' + rate.pt_octave };
  draw_widget_text(widget, rate_str, sizeof(rate_str), 0, WTT_Value);
}

static BOOL make_widgets() {
  BOOL ret = TRUE;
  PTNote* rate_note = model_get_sample_rate();
  UWORD rate_knob_init = (rate_note->pt_octave * 12) + rate_note->semitone;
  UWORD next_widget_idx = 0;

  // Top row of widgets
  UWORD widget_top = kUIGapTop + kUIGapRowTop + (0 * kUIRowStride);
  CHECK(widgets_make_wave(kUIGapLeft + (0 * kUIColStride), widget_top,
                          model_get_osc1_wave(), model_get_osc2_wave(),
                          osc_wave_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (1 * kUIColStride), widget_top, kKnobOscMixRange,
                          model_get_osc_mix(), osc_mix_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (2 * kUIColStride), widget_top, kKnobOscDetuneRange,
                          model_get_osc_detune(), osc_detune_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (3 * kUIColStride), widget_top, kKnobLFORange,
                          50, lfo_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (4 * kUIColStride), widget_top, kKnobLFOModRange,
                          0, lfo_mod_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (5 * kUIColStride), widget_top, kKnobLFOAmtRange,
                          50, lfo_amt_changed, &g.widgets[next_widget_idx ++]));

  // Middle row of widgets
  widget_top += kUIRowStride;
  CHECK(widgets_make_knob(kUIGapLeft + (0 * kUIColStride), widget_top, kKnobCutoffRange,
                          model_get_cutoff(), cutoff_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (1 * kUIColStride), widget_top, kKnobEchoLagRange,
                          25, echo_lag_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (2 * kUIColStride), widget_top, kKnobEchoMixRange,
                          50, echo_mix_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (3 * kUIColStride), widget_top, kKnobGainRange,
                          model_get_gain_db(), gain_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_env(kUIGapLeft + (4 * kUIColStride), widget_top, model_get_amp_env(),
                         amp_env_changed, &g.widgets[next_widget_idx ++]));

  // Bottom row of widgets
  widget_top += kUIRowStride;
  CHECK(widgets_make_knob(kUIGapLeft + (0 * kUIColStride), widget_top, kKnobOctaveRange,
                          model_get_octave_base(), octave_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (1 * kUIColStride), widget_top, kKnobLengthRange,
                          model_get_length_ms(), length_changed, &g.widgets[next_widget_idx ++]));
  CHECK(widgets_make_knob(kUIGapLeft + (2 * kUIColStride), widget_top, kKnobRateRange,
                          rate_knob_init, rate_changed, &g.widgets[next_widget_idx ++]));

  STRPTR widget_titles[kNumWidgets] = {
    "WAVES", "MIX", "DETUNE", "FREQ", "MOD", "AMT",
    "CUTOFF", "LAG", "MIX", "GAIN", "ENVELOPE",
    "OCTAVE", "LENGTH", "RATE",
  };

  for (UWORD i = 0; i < kNumWidgets; ++ i) {
    draw_widget_text(g.widgets[i], widget_titles[i], str_len(widget_titles[i]), 0, WTT_Title);
    g.widgets[i]->render(g.widgets[i]);
  }

cleanup:
  return ret;
}

BOOL ui_init() {
  BOOL ret = TRUE;

  CopyMem(pointer_bpls, &PointerSprs[0][2], sizeof(pointer_bpls));

  struct NewScreen new_screen = {
    .LeftEdge = 0,
    .TopEdge = 0,
    .Width = kScreenWidth,
    .Height = kScreenHeight,
    .Depth = kScreenDepth,
    .DetailPen = 0,
    .BlockPen = 0,
    .ViewModes = 0,
    .Type = CUSTOMSCREEN,
    .Font = NULL,
    .DefaultTitle = NULL,
    .Gadgets = NULL,
    .CustomBitMap = NULL,
  };

  CHECK(g.screen = OpenScreen(&new_screen));

  ShowTitle(g.screen, FALSE);

  UWORD palette[1 << kScreenDepth] = { kColorBG, kColorDark, kColorShadow, kColorColor };
  LoadRGB4(&g.screen->ViewPort, palette, ARRAY_SIZE(palette));
  SetRGB4(&g.screen->ViewPort, 17, 0, 0, 0);
  SetRGB4(&g.screen->ViewPort, 18, 13, 11, 9);

  struct NewWindow new_window = {
    .LeftEdge = 0,
    .TopEdge = 0,
    .Width = kScreenWidth,
    .Height = kScreenHeight,
    .DetailPen = 0,
    .BlockPen = 0,
    .IDCMPFlags = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_DELTAMOVE | IDCMP_RAWKEY,
    .Flags = WFLG_SIMPLE_REFRESH | WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE,
    .FirstGadget = NULL,
    .CheckMark = NULL,
    .Title = NULL,
    .Screen = g.screen,
    .BitMap = NULL,
    .MinWidth = kScreenWidth,
    .MinHeight = kScreenHeight,
    .MaxWidth = kScreenWidth,
    .MaxHeight = kScreenHeight,
    .Type = CUSTOMSCREEN,
  };

  CHECK(g.window = OpenWindow(&new_window));

  SetPointer(g.window, PointerSprs[0], kPtrSprEdge, kPtrSprEdge, kPtrSprOffX, kPtrSprOffY);

  CHECK(make_shadow_font());
  draw_widget_frames();
  CHECK(make_widgets());

  g.mouse_move_ev.ie_NextEvent = NULL;
  g.mouse_move_ev.ie_Class = IECLASS_POINTERPOS;
  g.mouse_move_ev.ie_SubClass = 0;
  g.mouse_move_ev.ie_Code = IECODE_NOBUTTON;
  g.mouse_move_ev.ie_Qualifier = 0;

  CHECK(g.input_mp = CreatePort(NULL, 0));
  CHECK(g.input_io = CreateStdIO(g.input_mp));
  CHECK(OpenDevice("input.device", 0, (struct IORequest*)g.input_io, 0) == 0);

  g.input_io->io_Data = &g.mouse_move_ev;
  g.input_io->io_Length = sizeof(struct InputEvent);
  g.input_io->io_Command = IND_WRITEEVENT;

cleanup:
  return ret;
}

VOID ui_fini() {
  if (g.input_io && g.input_io->io_Device) {
    CloseDevice((struct IORequest*)g.input_io);
  }

  DeleteStdIO(g.input_io);

  if (g.input_mp) {
    DeletePort(g.input_mp);
  }

  for (UWORD i = 0; i < kNumWidgets; ++ i) {
    if (g.widgets[i]) {
      g.widgets[i]->free(g.widgets[i]);
    }
  }

  if (g.window) {
    CloseWindow(g.window);
  }

  if (g.screen) {
    CloseScreen(g.screen);
  }
}

BOOL ui_handle_events() {
  BOOL ret = TRUE;
  ULONG wait_signals = (1 << g.window->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C;
  PTOctave keys_octave_base = PTOct_2;
  Widget* active_widget = NULL;
  UWORD clicked_pos[2] = { 0, 0 };

  for (BOOL running = TRUE; running; ) {
    ULONG signals = Wait(wait_signals);

    if (signals & SIGBREAKF_CTRL_C) {
      running = FALSE;
    }

    for (struct IntuiMessage* msg;
         msg = (struct IntuiMessage*)GetMsg(g.window->UserPort); ) {
      switch (msg->Class) {
      case IDCMP_RAWKEY: {
        UWORD allow_qual_mask =
          IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT |          // allow shift keys, useful when dragging knob
          IEQUALIFIER_LEFTBUTTON | IEQUALIFIER_RELATIVEMOUSE // allow while knob clicked, relative always set
        ;

        if ((msg->Code & IECODE_UP_PREFIX) == 0 &&           // ignore key release
            (msg->Qualifier & (~allow_qual_mask)) == 0) {
          switch (msg->Code) {
          case 0x45: // Escape
            running = FALSE;
            break;

          case 0x50: // F1
            keys_octave_base = PTOct_1;
            break;

          case 0x51: // F2
            keys_octave_base = PTOct_2;
            break;

          case 0x52: // F3
            CHECK(model_export_sample());
            break;

          default:
            {
              UWORD decoded_key = KeyDecodeTable[msg->Code];

              if (decoded_key & (1 << 6)) {
                PTNote note = {
                  .semitone = decoded_key & 0xF,
                  .pt_octave = keys_octave_base + (decoded_key >> 7),
                };

                CHECK(model_play_note(&note));
              }
            }
          }
        }

        break;
      }

      case IDCMP_MOUSEMOVE:
        {
          if (active_widget) {
            // Scale adjustment if shift key is not held down.
            UWORD delta_scale = (msg->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? 1 : kDragDeltaScale;
            active_widget->dragged(active_widget, msg->MouseX * delta_scale, msg->MouseY * delta_scale);
          }

          break;
        }

      case IDCMP_MOUSEBUTTONS:
        if (msg->Code == SELECTDOWN) {
          for (UWORD i = 0; i < kNumWidgets; ++ i) {
            if (g.screen->MouseX >= g.widgets[i]->pos_tl[0] &&
                g.screen->MouseX <= g.widgets[i]->pos_br[0] &&
                g.screen->MouseY >= g.widgets[i]->pos_tl[1] &&
                g.screen->MouseY <= g.widgets[i]->pos_br[1]) {
              active_widget = g.widgets[i];

              // Blank pointer sprite and remember its position.
              clicked_pos[0] = g.screen->MouseX;
              clicked_pos[1] = g.screen->MouseY;
              SetPointer(g.window, PointerSprs[1], kPtrSprEdge, kPtrSprEdge, 0, 0);

              // Start listening to mouse move events.
              g.window->Flags |= WFLG_REPORTMOUSE;

              if (active_widget->clicked) {
                UWORD mouse_rel[2];

                for (UWORD i = 0; i < 2; ++ i) {
                  mouse_rel[i] = clicked_pos[i] - active_widget->pos_tl[i];
                }

                active_widget->clicked(active_widget, mouse_rel);
              }

              break;
            }
          }
        }
        else if (msg->Code == SELECTUP && active_widget) {
          UWORD new_mouse_pos[2];

          for (UWORD i = 0; i < 2; ++ i) {
            new_mouse_pos[i] = clicked_pos[i];
          }

          if (active_widget->released) {
            UWORD mouse_rel[2];
            active_widget->released(active_widget, mouse_rel);

            for (UWORD i = 0; i < 2; ++ i) {
              new_mouse_pos[i] = mouse_rel[i] + active_widget->pos_tl[i];
            }
          }

          // Stop listening to mouse move events.
          g.window->Flags &= ~WFLG_REPORTMOUSE;

          // Move pointer back to click position and restore sprite.
          active_widget = NULL;
          g.mouse_move_ev.ie_X = new_mouse_pos[0] * 2; // not sure why *2 needed?
          g.mouse_move_ev.ie_Y = new_mouse_pos[1] * 2;
          CHECK(DoIO((struct IORequest*)g.input_io) == 0);
          SetPointer(g.window, PointerSprs[0], kPtrSprEdge, kPtrSprEdge, kPtrSprOffX, kPtrSprOffY);
        }

        break;
      }

      ReplyMsg((struct Message*)msg);
    }
  }

cleanup:
  return ret;
}

struct Window* ui_get_window() {
  return g.window;
}
