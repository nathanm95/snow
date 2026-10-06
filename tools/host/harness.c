// Host-side test harness for the snow simulation.
//
// Builds sim.c on the Mac against the stub pebble.h next to this file, then
// drives every feature for thousands of frames under a SIGALRM watchdog and
// checks the grid invariants after each phase. Run via tools/host/run.sh.

#include <signal.h>
#include <unistd.h>

#include "../../src/c/sim.c"

Settings g_settings;

// ------------------------------------------------------------- Pebble stubs ---

static uint8_t s_fb[SCREEN_H][SCREEN_W];
static uint8_t (*s_draw)[SCREEN_W] = s_fb;
static GColor s_fill, s_textc;
static int16_t s_fake_min[SCREEN_H], s_fake_max[SCREEN_H];

static int font_px(GFont f) {
  if (!strcmp(f, "leco60")) return 60;
  if (!strcmp(f, "roboto49")) return 49;
  if (!strcmp(f, "bitham42")) return 42;
  if (!strcmp(f, "gothic28")) return 28;
  return 24;
}

GFont fonts_get_system_font(const char *key) { return key; }

GSize graphics_text_layout_get_content_size(const char *t, GFont f, GRect box,
                                            GTextOverflowMode m, GTextAlignment a) {
  int px = font_px(f);
  return (GSize){ (int16_t)(strlen(t) * px * 6 / 10), (int16_t)(px + 8) };
}

void graphics_context_set_text_color(GContext *ctx, GColor c) { s_textc = c; }
void graphics_context_set_fill_color(GContext *ctx, GColor c) { s_fill = c; }

static void put(int x, int y, uint8_t c) {
  if (y >= 0 && y < SCREEN_H && x >= s_fake_min[y] && x <= s_fake_max[y]) s_draw[y][x] = c;
}

// Each glyph is a hollow box (digits) or two dots (colon): ledges, holes and
// overhangs, which is what matters to the snow.
void graphics_draw_text(GContext *ctx, const char *t, GFont f, GRect r, GTextOverflowMode m,
                        GTextAlignment a, void *attr) {
  int px = font_px(f), cw = px * 6 / 10, gh = px * 7 / 10, th = px / 7 + 1;
  int len = strlen(t);
  int x0 = r.origin.x + (r.size.w - len * cw) / 2;
  int y0 = r.origin.y + px / 5;
  for (int i = 0; i < len; i++) {
    int gx = x0 + i * cw + 2, gw = cw - 4;
    for (int y = y0; y < y0 + gh; y++) {
      for (int x = gx; x < gx + gw; x++) {
        bool on;
        if (t[i] == ':') on = (x - gx > gw / 3 && x - gx < gw * 2 / 3) &&
                              ((y - y0 > gh / 5 && y - y0 < gh * 2 / 5) ||
                               (y - y0 > gh * 3 / 5 && y - y0 < gh * 4 / 5));
        else if (t[i] == ' ') on = false;
        else {
          // Hollow box, plus a crossbar or a spine depending on the digit, so
          // a minute change really moves walls.
          int d = t[i] - '0';
          on = x < gx + th || x >= gx + gw - th || y < y0 + th || y >= y0 + gh - th;
          if (d % 2 && y >= y0 + gh / 2 - th / 2 && y < y0 + gh / 2 + th / 2 + 1) on = true;
          if (d % 3 == 0 && x >= gx + gw / 2 - th / 2 && x < gx + gw / 2 + th / 2 + 1) on = true;
        }
        if (on) put(x, y, s_textc.argb);
      }
    }
  }
}

void graphics_fill_rect(GContext *ctx, GRect r, int radius, int corners) {
  for (int y = r.origin.y; y < r.origin.y + r.size.h; y++)
    for (int x = r.origin.x; x < r.origin.x + r.size.w; x++) put(x, y, s_fill.argb);
}

static int s_captured;
GBitmap *graphics_capture_frame_buffer(GContext *ctx) {
  if (s_captured) { fprintf(stderr, "FAIL: double capture\n"); exit(1); }
  s_captured = 1;
  return (GBitmap *)s_fb;
}
bool graphics_release_frame_buffer(GContext *ctx, GBitmap *b) { s_captured = 0; return true; }
GBitmapFormat gbitmap_get_format(const GBitmap *b) {
  return PBL_IF_ROUND_ELSE(GBitmapFormat8BitCircular, GBitmapFormat8Bit);
}
GBitmapDataRowInfo gbitmap_get_data_row_info(const GBitmap *b, uint16_t y) {
  return (GBitmapDataRowInfo){ s_fb[y], s_fake_min[y], s_fake_max[y] };
}
bool gcolor_equal(GColor a, GColor b) { return a.argb == b.argb; }
int32_t sin_lookup(int32_t angle) {
  double r = angle * 6.283185307179586 / TRIG_MAX_ANGLE;
  return (int32_t)(__builtin_sin(r) * TRIG_MAX_RATIO);
}

// ------------------------------------------------------------------ Checks ---

static const char *s_phase = "init";
static long s_frames_total;

static void on_alarm(int sig) {
  fprintf(stderr, "FAIL: HANG in phase '%s' (frame %ld)\n", s_phase, s_frames_total);
  _exit(2);
}

static void check(void) {
  // Text mask, re-rendered independently into a scratch buffer.
  static uint8_t scratch[SCREEN_H][SCREEN_W];
  memset(scratch, 0, sizeof(scratch));
  s_draw = scratch;
  draw_text(NULL, GColorWhite);
  s_draw = s_fb;

  static uint8_t seen[SCREEN_H][SCREEN_W];
  memset(seen, 0, sizeof(seen));
  for (int i = 0; i < s_n; i++) {
    int x = POS_X(s_flakes[i]), y = POS_Y(s_flakes[i]);
    const char *why = NULL;
    if (!on_glass(x, y)) why = "off the glass";
    else if (!grid_get(x, y)) why = "grid bit clear";
    else if (seen[y][x]) why = "duplicate";
    else if (!s_mask_dirty && scratch[y][x]) why = "inside the clock text";
    if (why) {
      fprintf(stderr, "FAIL [%s]: flake %d at (%d,%d) %s\n", s_phase, i, x, y, why);
      exit(1);
    }
    seen[y][x] = 1;
  }
  if (s_n > s_cap) { fprintf(stderr, "FAIL [%s]: n %d > cap %d\n", s_phase, s_n, s_cap); exit(1); }
}

static int frame(void) {
  alarm(2);
  bool moving = sim_step();
  sim_render(NULL, GRect(0, 0, SCREEN_W, SCREEN_H), GRect(0, 0, SCREEN_W, SCREEN_H));
  alarm(0);
  s_frames_total++;
  return moving;
}

// Runs until the battery saver would put the sim to sleep. Returns the frame
// count, or -1 if it never settled within max frames.
static void dump_unstable(int limit) {
  for (int i = 0; i < s_n && limit > 0; i++) {
    int x = POS_X(s_flakes[i]), y = POS_Y(s_flakes[i]);
    if (blocked(x + s_ddx, y + s_ddy)) continue;
    limit--;
    printf("    unstable (%d,%d) g=(%d,%d) d=(%d,%d):", x, y, s_gx, s_gy, s_ddx, s_ddy);
    for (int dy = -1; dy <= 1; dy++) {
      printf(" ");
      for (int dx = -1; dx <= 1; dx++) printf("%c", !on_glass(x + dx, y + dy) ? '#' : grid_get(x + dx, y + dy) ? 'o' : '.');
    }
    printf("\n");
  }
}

static int run_until_settled(int max) {
  bool trace = getenv("TRACE") != NULL;
  for (int f = 1; f <= max; f++) {
    if (!frame()) { sim_wake(); return f; }
    if (f % 100 == 0) {
      check();
      if (trace) printf("    f%-5d unstable=%d\n", f, count_unstable());
      if (trace && f == 800) dump_unstable(25);
    }
  }
  dump_unstable(6);
  return -1;
}

static void run_frames(int count) {
  for (int f = 1; f <= count; f++) {
    frame();
    if (f % 100 == 0) check();
  }
}

static int airborne(void) {
  int c = 0;
  for (int i = 0; i < s_n; i++)
    if (!blocked(POS_X(s_flakes[i]) + s_ddx, POS_Y(s_flakes[i]) + s_ddy)) c++;
  return c;
}

static void report(const char *what, int frames) {
  if (frames < 0) printf("  %-34s did NOT settle (unstable=%d)\n", what, count_unstable());
  else printf("  %-34s settled in %4d frames (%.1fs)\n", what, frames, frames / 20.0);
}

static void defaults(void) {
  g_settings = (Settings){
    .version = SETTINGS_VERSION, .col_bg = GColorBlack, .col_snow = GColorWhite,
    .col_time = { .argb = 0xF0 }, .clock_size = CLOCK_CLASSIC, .density = DENSITY_NORMAL,
    .battery_saver = true, .tilt = true, .shake_stir = true, .shake_light = true,
    .touch_sweep = true,
  };
}

int main(void) {
  signal(SIGALRM, on_alarm);
  for (int y = 0; y < SCREEN_H; y++) {
#ifdef PBL_ROUND
    int r = SCREEN_W / 2, dy = y - r + 0.5;
    int hw = 0;
    while ((hw + 1) * (hw + 1) + dy * dy <= r * r) hw++;
    s_fake_min[y] = r - hw;
    s_fake_max[y] = r + hw - 1;
#else
    s_fake_min[y] = 0;
    s_fake_max[y] = SCREEN_W - 1;
#endif
  }
  defaults();
  printf("%dx%d %s\n", SCREEN_W, SCREEN_H, PBL_IF_ROUND_ELSE("round", "rect"));

  s_phase = "init";
  if (!sim_init()) { fprintf(stderr, "FAIL: sim_init\n"); return 1; }
  sim_set_text("16:12", "Tue 6 Oct");
  sim_set_gravity(0, 1000);
  frame();
  check();
  printf("  flakes: %d (cap %d)\n", s_n, s_cap);
  report("initial scatter", run_until_settled(4000));
  check();

  s_phase = "minute change";
  Pos before[20000];
  int n_before = s_n;
  memcpy(before, s_flakes, s_n * sizeof(Pos));
  sim_set_text("16:13", "Tue 6 Oct");
  sim_wake();
  frame();
  check();
  // Flakes are reordered by the reseat, so compare as position sets.
  static uint8_t was[SCREEN_H][SCREEN_W];
  memset(was, 0, sizeof(was));
  for (int i = 0; i < n_before; i++) was[POS_Y(before[i])][POS_X(before[i])] = 1;
  int moved = 0;
  for (int i = 0; i < s_n; i++) if (!was[POS_Y(s_flakes[i])][POS_X(s_flakes[i])]) moved++;
  printf("  minute change moved %d of %d flakes (should be small)\n", moved, s_n);
  report("after minute change", run_until_settled(4000));

  s_phase = "stir";
  sim_stir();
  sim_wake();
  report("after stir", run_until_settled(4000));

  s_phase = "tilt";
  int gs[][2] = { { 1000, 0 }, { -1000, 0 }, { 0, -1000 }, { 700, 700 }, { 0, 0 }, { 0, 1000 } };
  for (unsigned k = 0; k < sizeof(gs) / sizeof(gs[0]); k++) {
    sim_set_gravity(gs[k][0], gs[k][1]);
    sim_wake();
    char label[48];
    snprintf(label, sizeof(label), "gravity (%d,%d)", gs[k][0], gs[k][1]);
    report(label, run_until_settled(6000));
  }

  s_phase = "noisy wrist";
  for (int f = 0; f < 3000; f++) {
    sim_set_gravity((int)(rng() % 1200) - 600, 700 + (int)(rng() % 600) - 300);
    frame();
    if (f % 100 == 0) check();
  }
  sim_set_gravity(0, 1000);
  sim_wake();
  report("after noisy wrist", run_until_settled(6000));

  s_phase = "touch sweep";
  for (int k = 0; k < 40; k++) {
    int x = rng() % SCREEN_W, y = rng() % SCREEN_H;
    sim_touch(x, y, true);
    for (int f = 0; f < 10; f++) {
      x += (int)(rng() % 21) - 10;
      y += (int)(rng() % 21) - 10;
      sim_touch(x, y, true);
      frame();
    }
    sim_touch(x, y, false);
    frame();
  }
  check();
  sim_wake();
  report("after sweeps", run_until_settled(6000));

  s_phase = "big clock + date";
  g_settings.clock_size = CLOCK_LARGE;
  g_settings.show_date = true;
  sim_invalidate_mask();
  sim_wake();
  frame();
  check();
  report("big clock + date", run_until_settled(6000));

  s_phase = "battery snow";
  g_settings.battery_snow = true;
  int levels[] = { 100, 40, 5, 90 };
  for (unsigned k = 0; k < 4; k++) {
    sim_set_battery(levels[k]);
    sim_wake();
    char label[48];
    snprintf(label, sizeof(label), "battery %d%% -> %d flakes", levels[k], s_target);
    report(label, run_until_settled(6000));
    if (s_n != s_target) { fprintf(stderr, "FAIL: n %d != target %d\n", s_n, s_target); return 1; }
  }
  g_settings.battery_snow = false;
  sim_set_battery(100);

  s_phase = "wind";
  g_settings.wind = true;
  run_frames(4000);
  printf("  wind: gust due in %d frames\n", sim_frames_until_gust());
  g_settings.wind = false;
  sim_wake();
  report("after wind", run_until_settled(6000));

  s_phase = "endless";
  g_settings.endless = true;
  int n0 = s_n;
  run_frames(2000);
  printf("  endless: flakes %d -> %d, airborne %d\n", n0, s_n, airborne());
  if (frame() == 0) { fprintf(stderr, "FAIL: endless went to sleep\n"); return 1; }
  g_settings.endless = false;
  sim_wake();
  report("after endless", run_until_settled(6000));

  s_phase = "hourly flurry / repeated stirs";
  for (int k = 0; k < 20; k++) { sim_stir(); run_frames(50); }
  check();

  for (int d = DENSITY_LIGHT; d <= DENSITY_BLIZZARD; d++) {
    s_phase = "density";
    g_settings.density = d;
    sim_reset();
    sim_wake();
    frame();
    char label[48];
    snprintf(label, sizeof(label), "density %d: %d flakes", d, s_n);
    report(label, run_until_settled(8000));
    check();
  }

  printf("PASS (%ld frames)\n", s_frames_total);
  sim_deinit();
  return 0;
}
