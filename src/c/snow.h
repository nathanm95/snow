//
//  snow.h
//  Snow Watchface
//
//  Original concept and physics: Copyright (c) 2013 Leo Vandriel.
//  2026 port to SDK 4 (Time 2 / Round 2) and settings: Nathan Marchant.
//

#pragma once
#include <pebble.h>

#define SCREEN_W PBL_DISPLAY_WIDTH
#define SCREEN_H PBL_DISPLAY_HEIGHT

// A flake's position, packed as (y << SHIFT) | x. Every platform but gabbro fits
// both coordinates in a byte, which halves the flake array on the 24K aplite.
#if SCREEN_W > 255 || SCREEN_H > 255
typedef uint32_t Pos;
#define POS_SHIFT 16
#else
typedef uint16_t Pos;
#define POS_SHIFT 8
#endif
#define POS(x, y) ((Pos)(((Pos)(y) << POS_SHIFT) | (Pos)(x)))
#define POS_X(p) ((int)((p) & ((1u << POS_SHIFT) - 1)))
#define POS_Y(p) ((int)((p) >> POS_SHIFT))

// ---------------------------------------------------------------- Settings ---

#define SETTINGS_KEY 1
#define SETTINGS_VERSION 1

enum { CLOCK_CLASSIC = 0, CLOCK_LARGE = 1 };
enum { DENSITY_LIGHT = 0, DENSITY_NORMAL, DENSITY_HEAVY, DENSITY_BLIZZARD };

typedef struct {
  uint8_t version;
  GColor col_bg;
  GColor col_snow;
  GColor col_time;
  bool invert;          // black-and-white watches only
  uint8_t clock_size;
  bool show_date;
  uint8_t density;
  bool battery_snow;    // amount of snow tracks the battery level
  bool battery_saver;   // stop animating once the snow has settled
  bool endless;         // melt from the ground, keep snowing from the sky
  bool hourly_flurry;   // stir the globe on the hour
  bool wind;            // occasional sideways gusts
  bool tilt;            // gravity follows the wrist
  bool shake_stir;      // a wrist flick throws the snow back up
  bool shake_light;     // a wrist flick turns the backlight on
  bool touch_sweep;     // Time 2 / Round 2: a finger brushes snow aside
} Settings;

extern Settings g_settings;

void settings_init(void);
// Applies an inbox dictionary from the Clay page. Returns true if the flake
// count needs rebuilding (density changed).
bool settings_handle_inbox(DictionaryIterator *iter);

// -------------------------------------------------------------- Simulation ---

// Allocates the occupancy grid and the flake array for the current density.
// Returns false if even a reduced allocation failed.
bool sim_init(void);
void sim_deinit(void);
// Reallocates for a new density and starts a fresh snowfall.
void sim_reset(void);

// Text the snow collides with. The sim rebuilds its wall mask from these on the
// next frame after sim_invalidate_mask().
void sim_set_text(const char *time_text, const char *date_text);
void sim_invalidate_mask(void);

// Gravity in milli-g, screen axes (+x right, +y down).
void sim_set_gravity(int gx, int gy);
// Battery percentage (0-100) used when battery_snow is on.
void sim_set_battery(int percent);

// One physics frame. Returns true while the snow is still moving (i.e. the
// caller should keep the frame timer running).
bool sim_step(void);
// Restarts settle detection; call whenever the frame timer is (re)started.
void sim_wake(void);
void sim_render(GContext *ctx, GRect bounds, GRect text_bounds);

void sim_stir(void);
void sim_touch(int x, int y, bool down);
// Number of 50ms frames until the next wind gust is due, or -1 if none.
int sim_frames_until_gust(void);

#ifdef SNOW_DEBUG
int sim_flake_count(void);
int sim_debug_unstable(void);
int sim_debug_awake_frames(void);
int sim_debug_wind(void);
extern int g_dbg_bad, g_dbg_rand;   // flakes the last mask rebuild had to move
#endif
