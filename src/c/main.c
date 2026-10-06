//
//  main.c
//  Snow Watchface
//
//  Original concept and physics: Copyright (c) 2013 Leo Vandriel.
//  2026 port to SDK 4 (Time 2 / Round 2) and settings: Nathan Marchant.
//

#include "snow.h"

#define FRAME_MS 50       // simulation frame interval
#define WAKE_DELTA 150    // tilt change (milli-g) that wakes a sleeping globe

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_frame_timer;
static AppTimer *s_gust_timer;
static bool s_focused = true;
static bool s_accel_on;
static int s_ax, s_ay;             // last accelerometer reading
static int s_sleep_ax, s_sleep_ay; // reading when the snow last settled

#ifdef SNOW_DEBUG
static int s_step_ms, s_render_ms;
static bool s_debug_awake;

static int now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (int)(s % 1000) * 1000 + ms;
}
#endif

static void frame(void *context);

#ifdef SNOW_DEBUG
static char s_wake_src = '-';
#define WAKE(src) do { s_wake_src = (src); wake(); } while (0)
#else
#define WAKE(src) wake()
#endif

static void wake(void) {
  if (s_gust_timer) {
    app_timer_cancel(s_gust_timer);
    s_gust_timer = NULL;
  }
  // Always redraw, so the clock stays current behind a notification; only
  // the animation waits for focus to come back.
  layer_mark_dirty(s_layer);
  if (!s_focused) return;
  sim_wake();
  if (!s_frame_timer) s_frame_timer = app_timer_register(FRAME_MS, frame, NULL);
}

static void gust_due(void *context) {
  s_gust_timer = NULL;
  WAKE('g');
}

static void frame(void *context) {
  s_frame_timer = NULL;
#ifdef SNOW_DEBUG
  int t0 = now_ms();
#endif
  bool moving = sim_step();
#ifdef SNOW_DEBUG
  s_step_ms = now_ms() - t0;
  s_debug_awake = moving;
#endif
  layer_mark_dirty(s_layer);
  if (!s_focused) return;

  if (moving || !g_settings.battery_saver) {
    s_frame_timer = app_timer_register(FRAME_MS, frame, NULL);
    return;
  }
  // Settled: sleep until a tilt, shake, touch, tick, or the next wind gust.
  s_sleep_ax = s_ax;
  s_sleep_ay = s_ay;
  int gust = sim_frames_until_gust();
  if (gust >= 0) s_gust_timer = app_timer_register(gust * FRAME_MS + FRAME_MS, gust_due, NULL);
}

static void update_proc(Layer *layer, GContext *ctx) {
#ifdef SNOW_DEBUG
  int t0 = now_ms();
#endif
  GRect bounds = layer_get_bounds(layer);
#if PBL_API_EXISTS(layer_get_unobstructed_bounds)
  GRect text_bounds = layer_get_unobstructed_bounds(layer);
#else
  GRect text_bounds = bounds;
#endif
  sim_render(ctx, bounds, text_bounds);
#ifdef SNOW_DEBUG
  s_render_ms = now_ms() - t0;
  static char buf[40];
  snprintf(buf, sizeof(buf), "u%d %c b%d w%d %s", sim_debug_unstable(), s_wake_src,
           g_dbg_bad, sim_debug_wind(), s_debug_awake ? "ON" : "zz");
  graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
  graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     GRect(0, bounds.size.h * 3 / 4, bounds.size.w, 30),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
#endif
}

// ---------------------------------------------------------------- Clock ---

static void update_time(struct tm *t) {
  char time_text[8], date_text[24];
#ifdef SNOW_SHOW_TIME
  // Store screenshots: the emulator clock can't be set, so pin 10:08 on
  // Thursday 24 December.
  struct tm fixed = *t;
  fixed.tm_hour = SNOW_SHOW_TIME / 100;
  fixed.tm_min = SNOW_SHOW_TIME % 100;
  fixed.tm_mday = 24;
  fixed.tm_mon = 11;
  fixed.tm_wday = 4;
  t = &fixed;
#endif
  strftime(time_text, sizeof(time_text), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  if (!clock_is_24h_style() && time_text[0] == '0') {
    memmove(time_text, time_text + 1, strlen(time_text));
  }
  strftime(date_text, sizeof(date_text), "%a %d %b", t);
  char *zero = strstr(date_text, " 0");
  if (zero) memmove(zero + 1, zero + 2, strlen(zero + 2) + 1);
  sim_set_text(time_text, date_text);
}

static void tick_handler(struct tm *t, TimeUnits changed) {
  update_time(t);
  if (g_settings.hourly_flurry && t->tm_min == 0) sim_stir();
  WAKE('t');
}

// ---------------------------------------------------------------- Motion ---

static void accel_handler(AccelData *data, uint32_t num) {
  int sx = 0, sy = 0, n = 0;
  for (uint32_t i = 0; i < num; i++) {
    if (data[i].did_vibrate) continue;
    sx += data[i].x;
    sy += data[i].y;
    n++;
  }
  if (!n) return;
  s_ax = sx / n;
  s_ay = sy / n;
  // Accelerometer +y points to 12 o'clock; the screen's +y points down.
  sim_set_gravity(s_ax, -s_ay);
  if (!s_frame_timer) {
    int delta = abs(s_ax - s_sleep_ax) + abs(s_ay - s_sleep_ay);
    if (delta > WAKE_DELTA) WAKE('a');
  }
}

static void apply_tilt(void) {
  if (g_settings.tilt && !s_accel_on) {
    accel_data_service_subscribe(2, accel_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    s_accel_on = true;
  } else if (!g_settings.tilt && s_accel_on) {
    accel_data_service_unsubscribe();
    s_accel_on = false;
  }
  if (!g_settings.tilt) sim_set_gravity(0, 1000);
#ifdef SNOW_DEMO_TILT
  // Store recordings: hold the watch at a fixed angle instead of reading the
  // emulator's accelerometer, which reports a flat watch.
  if (s_accel_on) {
    accel_data_service_unsubscribe();
    s_accel_on = false;
  }
  int32_t a = DEG_TO_TRIGANGLE(SNOW_DEMO_TILT);
  sim_set_gravity(sin_lookup(a) * 1000 / TRIG_MAX_RATIO, cos_lookup(a) * 1000 / TRIG_MAX_RATIO);
#endif
}

#ifdef SNOW_DEMO_STIR
static void demo_stir(void *context) {
  sim_stir();
  WAKE('s');
}
#endif

static void tap_handler(AccelAxisType axis, int32_t direction) {
  if (g_settings.shake_light) light_enable_interaction();
  if (g_settings.shake_stir) {
    sim_stir();
    WAKE('s');
  }
}

#if defined(PBL_TOUCH)
static void touch_handler(const TouchEvent *event, void *context) {
  if (!g_settings.touch_sweep) return;
  sim_touch(event->x, event->y, event->type != TouchEvent_Liftoff);
  WAKE('h');
}
#endif

// ------------------------------------------------------- Watch services ---

// The system re-sends the battery state every few seconds even when nothing
// changed, so only a new percentage is allowed to wake the simulation.
static void battery_handler(BatteryChargeState state) {
  static int s_last_pct = -1;
  if (state.charge_percent == s_last_pct) return;
  s_last_pct = state.charge_percent;
  sim_set_battery(state.charge_percent);
  if (g_settings.battery_snow) WAKE('b');
}

static void focus_handler(bool in_focus) {
  s_focused = in_focus;
  if (in_focus) WAKE('f');
}

#if PBL_API_EXISTS(unobstructed_area_service_subscribe)
static void unobstructed_change(void *context) {
  sim_invalidate_mask();
  WAKE('u');
}
#endif

static void inbox_handler(DictionaryIterator *iter, void *context) {
  if (settings_handle_inbox(iter)) sim_reset();
  apply_tilt();
  sim_set_battery(battery_state_service_peek().charge_percent);
  sim_invalidate_mask();
  WAKE('m');
}

// ---------------------------------------------------------------- Window ---

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

static void init(void) {
  settings_init();
  sim_init();
  sim_set_battery(battery_state_service_peek().charge_percent);
  time_t now = time(NULL);
  update_time(localtime(&now));

  s_window = window_create();
  window_set_background_color(s_window, GColorClear);   // our layer paints everything
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  accel_tap_service_subscribe(tap_handler);
  apply_tilt();
  app_focus_service_subscribe_handlers((AppFocusHandlers){ .did_focus = focus_handler });
#if PBL_API_EXISTS(unobstructed_area_service_subscribe)
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){
    .did_change = unobstructed_change,
  }, NULL);
#endif
#if defined(PBL_TOUCH)
  touch_service_subscribe(touch_handler, NULL);
#endif

  app_message_register_inbox_received(inbox_handler);
  app_message_open(512, 64);
#ifdef SNOW_DEMO_STIR
  app_timer_register(SNOW_DEMO_STIR * 1000, demo_stir, NULL);
#endif

  WAKE('i');
}

static void deinit(void) {
  if (s_frame_timer) app_timer_cancel(s_frame_timer);
  if (s_gust_timer) app_timer_cancel(s_gust_timer);
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();
  if (s_accel_on) accel_data_service_unsubscribe();
#if defined(PBL_TOUCH)
  touch_service_unsubscribe();
#endif
  window_destroy(s_window);
  sim_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
