#include "snow.h"

Settings g_settings;

// Settings live as one struct under one persist key. The version byte is
// checked on load so a struct written by an older build is discarded rather
// than reinterpreted field by field.

static void settings_defaults(void) {
  g_settings = (Settings){
    .version = SETTINGS_VERSION,
    .col_bg = GColorBlack,
    .col_snow = GColorWhite,
    .col_time = GColorWhite,
    .invert = false,
    .clock_size = CLOCK_CLASSIC,
    .show_date = false,
    .density = DENSITY_NORMAL,
    .battery_snow = false,
    .battery_saver = true,
    .endless = false,
    .hourly_flurry = false,
    .wind = false,
    .tilt = true,
    .shake_stir = true,
    .shake_light = true,
    .touch_sweep = true,
  };
}

void settings_init(void) {
  settings_defaults();
  if (persist_exists(SETTINGS_KEY)) {
    Settings stored;
    int read = persist_read_data(SETTINGS_KEY, &stored, sizeof(stored));
    if (read == (int)sizeof(stored) && stored.version == SETTINGS_VERSION) {
      g_settings = stored;
    }
  }
#ifdef SNOW_PRESET
  // Emulator testing only (SNOW_PRESET=n pebble build): the settings page
  // can't be driven from the CLI, so bake a feature combination in instead.
  switch (SNOW_PRESET) {
    case 1:   // colours, big clock, date
      g_settings.col_bg = GColorOxfordBlue;
      g_settings.col_snow = GColorCeleste;
      g_settings.col_time = GColorChromeYellow;
      g_settings.invert = true;
      g_settings.clock_size = CLOCK_LARGE;
      g_settings.show_date = true;
      break;
    case 2:   // endless snowfall and wind
      g_settings.endless = true;
      g_settings.wind = true;
      break;
    case 3:   // battery-level snow, heavy
      g_settings.battery_snow = true;
      g_settings.density = DENSITY_HEAVY;
      break;
    case 4:   // blizzard
      g_settings.density = DENSITY_BLIZZARD;
      break;
    case 5:   // festive colours, heavy
      g_settings.col_bg = GColorDarkGreen;
      g_settings.col_snow = GColorWhite;
      g_settings.col_time = GColorRed;
      g_settings.density = DENSITY_HEAVY;
      break;
  }
#endif
}

static void settings_save(void) {
  g_settings.version = SETTINGS_VERSION;
  persist_write_data(SETTINGS_KEY, &g_settings, sizeof(g_settings));
}

// Clay sends toggles as integers but select values as strings, so read both.
static bool read_int(DictionaryIterator *it, uint32_t key, int32_t *out) {
  Tuple *t = dict_find(it, key);
  if (!t) return false;
  if (t->type == TUPLE_CSTRING) {
    *out = atoi(t->value->cstring);
  } else if (t->length == 1) {
    *out = t->type == TUPLE_INT ? t->value->int8 : t->value->uint8;
  } else if (t->length == 2) {
    *out = t->type == TUPLE_INT ? t->value->int16 : t->value->uint16;
  } else {
    *out = t->value->int32;
  }
  return true;
}

static void read_bool(DictionaryIterator *it, uint32_t key, bool *dst) {
  int32_t v;
  if (read_int(it, key, &v)) *dst = v != 0;
}

static void read_u8(DictionaryIterator *it, uint32_t key, uint8_t *dst, int lo, int hi) {
  int32_t v;
  if (!read_int(it, key, &v)) return;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  *dst = (uint8_t)v;
}

static void read_colour(DictionaryIterator *it, uint32_t key, GColor *dst) {
  int32_t v;
  if (read_int(it, key, &v)) *dst = GColorFromHEX(v);
}

bool settings_handle_inbox(DictionaryIterator *iter) {
  uint8_t old_density = g_settings.density;

  read_colour(iter, MESSAGE_KEY_ColBg, &g_settings.col_bg);
  read_colour(iter, MESSAGE_KEY_ColSnow, &g_settings.col_snow);
  read_colour(iter, MESSAGE_KEY_ColTime, &g_settings.col_time);
  read_bool(iter, MESSAGE_KEY_Invert, &g_settings.invert);
  read_u8(iter, MESSAGE_KEY_ClockSize, &g_settings.clock_size, 0, 1);
  read_bool(iter, MESSAGE_KEY_ShowDate, &g_settings.show_date);
  read_u8(iter, MESSAGE_KEY_Density, &g_settings.density, 0, 3);
  read_bool(iter, MESSAGE_KEY_BatterySnow, &g_settings.battery_snow);
  read_bool(iter, MESSAGE_KEY_BatterySaver, &g_settings.battery_saver);
  read_bool(iter, MESSAGE_KEY_Endless, &g_settings.endless);
  read_bool(iter, MESSAGE_KEY_HourlyFlurry, &g_settings.hourly_flurry);
  read_bool(iter, MESSAGE_KEY_Wind, &g_settings.wind);
  read_bool(iter, MESSAGE_KEY_Tilt, &g_settings.tilt);
  read_bool(iter, MESSAGE_KEY_ShakeStir, &g_settings.shake_stir);
  read_bool(iter, MESSAGE_KEY_ShakeLight, &g_settings.shake_light);
  read_bool(iter, MESSAGE_KEY_TouchSweep, &g_settings.touch_sweep);

  settings_save();
  return g_settings.density != old_density;
}
