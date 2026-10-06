// Minimal host-side stand-in for <pebble.h>, just enough to compile sim.c on
// the Mac for tools/host/harness.c. Text is faked as solid blocks so the wall
// mask has realistic shapes for the snow to collide with.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
typedef union { uint8_t argb; } GColor;
typedef struct GContext GContext;
typedef struct GBitmap GBitmap;
typedef const char *GFont;
typedef struct DictionaryIterator DictionaryIterator;
typedef enum { GBitmapFormat1Bit, GBitmapFormat8Bit, GBitmapFormat8BitCircular } GBitmapFormat;
typedef struct { uint8_t *data; int16_t min_x, max_x; } GBitmapDataRowInfo;
typedef int GTextOverflowMode;
typedef int GTextAlignment;
#define GTextOverflowModeFill 0
#define GTextAlignmentCenter 0
#define GCornerNone 0
#define GRect(x, y, w, h) ((GRect){ { (x), (y) }, { (w), (h) } })
#define GColorBlackARGB8 0xC0
#define GColorWhiteARGB8 0xFF
#define GColorBlack ((GColor){ .argb = GColorBlackARGB8 })
#define GColorWhite ((GColor){ .argb = GColorWhiteARGB8 })
#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xffff
#define FONT_KEY_BITHAM_42_BOLD "bitham42"
#define FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM "leco60"
#define FONT_KEY_ROBOTO_BOLD_SUBSET_49 "roboto49"
#define FONT_KEY_GOTHIC_28_BOLD "gothic28"
#define FONT_KEY_GOTHIC_24_BOLD "gothic24"
#ifdef PBL_ROUND
#define PBL_IF_ROUND_ELSE(a, b) (a)
#else
#define PBL_IF_ROUND_ELSE(a, b) (b)
#endif

GFont fonts_get_system_font(const char *key);
GSize graphics_text_layout_get_content_size(const char *t, GFont f, GRect box,
                                            GTextOverflowMode m, GTextAlignment a);
void graphics_context_set_text_color(GContext *ctx, GColor c);
void graphics_context_set_fill_color(GContext *ctx, GColor c);
void graphics_draw_text(GContext *ctx, const char *t, GFont f, GRect r, GTextOverflowMode m,
                        GTextAlignment a, void *attr);
void graphics_fill_rect(GContext *ctx, GRect r, int radius, int corners);
GBitmap *graphics_capture_frame_buffer(GContext *ctx);
bool graphics_release_frame_buffer(GContext *ctx, GBitmap *b);
GBitmapFormat gbitmap_get_format(const GBitmap *b);
GBitmapDataRowInfo gbitmap_get_data_row_info(const GBitmap *b, uint16_t y);
bool gcolor_equal(GColor a, GColor b);
int32_t sin_lookup(int32_t angle);
