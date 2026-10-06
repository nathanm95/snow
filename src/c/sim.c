//
//  sim.c
//  Snow Watchface — falling-sand style snow simulation.
//
//  The 2013 original used the framebuffer itself as the collision map, read
//  through a cast that SDK 3+ no longer allows. Here the state lives in our own
//  occupancy bitmap instead, so each frame can be redrawn from scratch: clock
//  text and the edge of a round screen are just "wall" bits in that bitmap.
//

#include "snow.h"

#define GRID_WORDS ((SCREEN_W * SCREEN_H + 31) / 32)

// Gravity below this (watch lying flat) is topped up towards the bottom of the
// screen, so snow drifts down gently instead of hanging in the air.
#define MIN_G 350
// Endless mode melts roughly s_n / MELT_DIV grounded flakes per frame.
#define MELT_DIV 6000
// Radius of the finger brush for touch sweep.
#define SWEEP_R (SCREEN_W / 10)

static const uint8_t DENSITY_PCT[] = { 6, 12, 20, 30 };

static uint32_t *s_grid;                    // 1 = flake, clock text, or off the glass
static int16_t s_row_min[SCREEN_H];         // visible span of each row (round screens)
static int16_t s_row_max[SCREEN_H];
static int16_t s_col_min[SCREEN_W];         // visible span of each column
static int16_t s_col_max[SCREEN_W];
static uint8_t *s_rows[SCREEN_H];           // framebuffer row pointers, per frame

static Pos *s_flakes;
static int s_cap;                           // allocated flakes
static int s_n;                             // live flakes
static int s_target;                        // wanted flakes
static int s_battery = 100;

static bool s_mask_dirty = true;
static bool s_need_spawn = true;

static char s_time_text[8];
static char s_date_text[24];
static GRect s_time_rect, s_date_rect;

static int s_gx = 0, s_gy = 1000;           // gravity, milli-g, screen axes
static int s_ddx = 0, s_ddy = 1;            // gravity quantised to 8 directions

static int s_wind;                          // current sideways push, milli-g
static int s_gust_wait = 100, s_gust_len, s_gust_t, s_gust_peak;
static int s_melt_acc;

static bool s_touch_down, s_touch_pending;
static int s_tx, s_ty, s_px, s_py;

static int s_awake_frames, s_quiet;

// ------------------------------------------------------------------ Helpers ---

static uint32_t s_rng = 2463534242u;

static inline uint32_t rng(void) {
  uint32_t x = s_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return s_rng = x;
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

static inline bool grid_get(int x, int y) {
  uint32_t i = (uint32_t)y * SCREEN_W + x;
  return (s_grid[i >> 5] >> (i & 31)) & 1;
}

static inline void grid_set(int x, int y) {
  uint32_t i = (uint32_t)y * SCREEN_W + x;
  s_grid[i >> 5] |= 1u << (i & 31);
}

static inline void grid_clr(int x, int y) {
  uint32_t i = (uint32_t)y * SCREEN_W + x;
  s_grid[i >> 5] &= ~(1u << (i & 31));
}

static inline bool blocked(int x, int y) {
  return (unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H || grid_get(x, y);
}

static inline bool on_glass(int x, int y) {
  return (unsigned)y < SCREEN_H && x >= s_row_min[y] && x <= s_row_max[y];
}

static inline bool free_cell(int x, int y) {
  return on_glass(x, y) && !grid_get(x, y);
}

static inline void move_flake(int i, int x, int y) {
  Pos p = s_flakes[i];
  grid_clr(POS_X(p), POS_Y(p));
  grid_set(x, y);
  s_flakes[i] = POS(x, y);
}

static int glass_area(void) {
  return PBL_IF_ROUND_ELSE(SCREEN_W * SCREEN_H * 785 / 1000, SCREEN_W * SCREEN_H);
}

static bool find_free_random(Pos *out) {
  for (int t = 0; t < 40; t++) {
    int y = rng() % SCREEN_H;
    int span = s_row_max[y] - s_row_min[y] + 1;
    if (span <= 0) continue;
    int x = s_row_min[y] + (int)(rng() % span);
    if (!grid_get(x, y)) {
      *out = POS(x, y);
      return true;
    }
  }
  return false;
}

// Nearest free cell for a flake that a new digit has landed on: first straight
// "up" (against gravity), so snow is lifted onto the top of the digit, then a
// widening ring.
static bool find_free_near(int x, int y, Pos *out) {
  for (int k = 1; k < 64; k++) {
    int nx = x - s_ddx * k, ny = y - s_ddy * k;
    if (!on_glass(nx, ny)) break;
    if (!grid_get(nx, ny)) {
      *out = POS(nx, ny);
      return true;
    }
  }
  static const int8_t DIRS[8][2] = {
    { 0, -1 }, { -1, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 1, 1 }, { 0, 1 }
  };
  for (int r = 1; r <= 12; r++) {
    for (int d = 0; d < 8; d++) {
      int nx = x + DIRS[d][0] * r, ny = y + DIRS[d][1] * r;
      if (free_cell(nx, ny)) {
        *out = POS(nx, ny);
        return true;
      }
    }
  }
  return false;
}

// A free cell just inside the edge the snow is falling away from, at a random
// column (or row, when the watch is on its side), so new snow drifts in across
// the whole sky rather than appearing mid-air.
static bool spawn_upstream(Pos *out) {
  bool vertical = iabs(s_gy) >= iabs(s_gx);
  for (int t = 0; t < 16; t++) {
    int x, y, in = rng() % 4;
    if (vertical) {
      x = rng() % SCREEN_W;
      if (s_col_min[x] > s_col_max[x]) continue;
      y = s_gy > 0 ? s_col_min[x] + in : s_col_max[x] - in;
    } else {
      y = rng() % SCREEN_H;
      if (s_row_min[y] > s_row_max[y]) continue;
      x = s_gx > 0 ? s_row_min[y] + in : s_row_max[y] - in;
    }
    if (free_cell(x, y)) {
      *out = POS(x, y);
      return true;
    }
  }
  return false;
}

// --------------------------------------------------------------- Lifecycle ---

static void update_target(void) {
  int t = s_cap;
  if (g_settings.battery_snow) {
    int pct = s_battery < 5 ? 5 : s_battery;
    t = s_cap * pct / 100;
  }
  s_target = t;
}

static bool alloc_flakes(void) {
  free(s_flakes);
  s_flakes = NULL;
  int want = glass_area() * DENSITY_PCT[g_settings.density] / 100;
  // aplite has 24K for everything; shrink until the allocation fits.
  while (want >= 200) {
    s_flakes = malloc(want * sizeof(Pos));
    if (s_flakes) break;
    want = want * 3 / 4;
  }
  s_cap = s_flakes ? want : 0;
  s_n = 0;
  s_need_spawn = true;
  s_mask_dirty = true;
  update_target();
  return s_flakes != NULL;
}

bool sim_init(void) {
  s_rng ^= (uint32_t)time(NULL) * 2654435761u;
  if (!s_rng) s_rng = 1;
  s_grid = malloc(GRID_WORDS * sizeof(uint32_t));
  if (!s_grid) return false;
  memset(s_grid, 0, GRID_WORDS * sizeof(uint32_t));
  return alloc_flakes();
}

void sim_deinit(void) {
  free(s_flakes);
  free(s_grid);
  s_flakes = NULL;
  s_grid = NULL;
}

void sim_reset(void) {
  alloc_flakes();
}

void sim_set_text(const char *time_text, const char *date_text) {
  if (strcmp(time_text, s_time_text) || strcmp(date_text, s_date_text)) {
    strncpy(s_time_text, time_text, sizeof(s_time_text) - 1);
    strncpy(s_date_text, date_text, sizeof(s_date_text) - 1);
    s_mask_dirty = true;
  }
}

void sim_invalidate_mask(void) {
  s_mask_dirty = true;
}

void sim_set_gravity(int gx, int gy) {
  int mag = iabs(gx) + iabs(gy);
  if (mag < MIN_G) gy += MIN_G - mag;
  s_gx = gx;
  s_gy = gy;
  int ax = iabs(gx), ay = iabs(gy);
  s_ddx = ax * 2 > ay ? (gx > 0 ? 1 : -1) : 0;
  s_ddy = ay * 2 > ax ? (gy > 0 ? 1 : -1) : 0;
}

void sim_set_battery(int percent) {
  s_battery = percent;
  update_target();
}

void sim_wake(void) {
  s_awake_frames = 0;
  s_quiet = 0;
}

int sim_frames_until_gust(void) {
  if (!g_settings.wind) return -1;
  return s_gust_len ? 0 : s_gust_wait;
}

#ifdef SNOW_DEBUG
static int s_dbg_unstable = -1;
int g_dbg_bad, g_dbg_rand;
int sim_flake_count(void) { return s_n; }
int sim_debug_unstable(void) { return s_dbg_unstable; }
int sim_debug_awake_frames(void) { return s_awake_frames; }
int sim_debug_wind(void) { return s_wind; }
#endif

// ---------------------------------------------------------- Text and walls ---

static GFont time_font(void) {
  if (g_settings.clock_size == CLOCK_LARGE) {
#if SCREEN_W >= 200
    return fonts_get_system_font(FONT_KEY_LECO_60_BOLD_NUMBERS_AM_PM);
#else
    return fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
#endif
  }
  return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
}

static GFont date_font(void) {
  return fonts_get_system_font(SCREEN_W >= 200 ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
}

static void layout_text(GRect area) {
  GRect box = GRect(area.origin.x, 0, area.size.w, SCREEN_H);
  GSize ts = graphics_text_layout_get_content_size(s_time_text, time_font(), box,
                                                   GTextOverflowModeFill, GTextAlignmentCenter);
  int dh = 0;
  if (g_settings.show_date) {
    dh = graphics_text_layout_get_content_size(s_date_text, date_font(), box,
                                               GTextOverflowModeFill, GTextAlignmentCenter).h;
  }
  int top = area.origin.y + (area.size.h - ts.h - dh) / 2;
  s_time_rect = GRect(area.origin.x, top, area.size.w, ts.h + 8);
  s_date_rect = GRect(area.origin.x, top + ts.h, area.size.w, dh + 8);
}

static void draw_text(GContext *ctx, GColor colour) {
  graphics_context_set_text_color(ctx, colour);
  graphics_draw_text(ctx, s_time_text, time_font(), s_time_rect,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  if (g_settings.show_date) {
    graphics_draw_text(ctx, s_date_text, date_font(), s_date_rect,
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

// Flakes the new walls landed on, or that ended up off the glass or doubled
// up, are moved to the nearest free cell; the rest keep their place.
static void reseat_flakes(void) {
  int ok = 0;
  for (int i = 0; i < s_n; i++) {
    Pos p = s_flakes[i];
    if (!blocked(POS_X(p), POS_Y(p))) {
      grid_set(POS_X(p), POS_Y(p));
      s_flakes[i] = s_flakes[ok];
      s_flakes[ok++] = p;
    }
  }
  int n = s_n;
  s_n = ok;
#ifdef SNOW_DEBUG
  g_dbg_bad = n - ok;
  g_dbg_rand = 0;
#endif
  for (int i = ok; i < n; i++) {
    Pos p = s_flakes[i], q;
    bool near = find_free_near(POS_X(p), POS_Y(p), &q);
#ifdef SNOW_DEBUG
    if (!near) g_dbg_rand++;
#endif
    if (near || find_free_random(&q)) {
      grid_set(POS_X(q), POS_Y(q));
      s_flakes[s_n++] = q;
    }
  }
}

// Renders the text white-on-black, reads it back, and turns every lit pixel
// (plus everything outside a round screen's circle) into a wall.
static bool build_mask(GContext *ctx, GRect bounds) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  draw_text(ctx, GColorWhite);

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return false;
  bool one_bit = gbitmap_get_format(fb) == GBitmapFormat1Bit;
  memset(s_grid, 0, GRID_WORDS * sizeof(uint32_t));
  for (int y = 0; y < SCREEN_H; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    int lo = row.min_x < 0 ? 0 : row.min_x;
    int hi = row.max_x >= SCREEN_W ? SCREEN_W - 1 : row.max_x;
    s_row_min[y] = lo;
    s_row_max[y] = hi;
    for (int x = 0; x < SCREEN_W; x++) {
      bool wall;
      if (x < lo || x > hi) {
        wall = true;
      } else if (one_bit) {
        wall = (row.data[x >> 3] >> (x & 7)) & 1;
      } else {
        wall = row.data[x] != GColorBlackARGB8;
      }
      if (wall) grid_set(x, y);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
  for (int x = 0; x < SCREEN_W; x++) {
    s_col_min[x] = SCREEN_H;
    s_col_max[x] = -1;
  }
  for (int y = 0; y < SCREEN_H; y++) {
    for (int x = s_row_min[y]; x <= s_row_max[y]; x++) {
      if (y < s_col_min[x]) s_col_min[x] = y;
      s_col_max[x] = y;
    }
  }
  reseat_flakes();
  return true;
}

static void scatter_spawn(void) {
  while (s_n < s_target) {
    Pos q;
    if (!find_free_random(&q)) break;
    grid_set(POS_X(q), POS_Y(q));
    s_flakes[s_n++] = q;
  }
}

// ----------------------------------------------------------------- Physics ---

static void wind_step(void) {
  if (!g_settings.wind) {
    s_wind = 0;
    s_gust_len = 0;
    return;
  }
  if (s_gust_len) {
    if (++s_gust_t >= s_gust_len) {
      s_gust_len = 0;
      s_wind = 0;
      s_gust_wait = 100 + rng() % 300;   // 5-20 s of calm
    } else {
      s_wind = s_gust_peak * sin_lookup(TRIG_MAX_ANGLE / 2 * s_gust_t / s_gust_len) / TRIG_MAX_RATIO;
    }
  } else if (s_gust_wait > 0) {
    s_gust_wait--;
  } else {
    s_gust_len = 40 + rng() % 60;        // 2-5 s gust
    s_gust_t = 0;
    s_gust_peak = (500 + rng() % 600) * ((rng() & 1) ? 1 : -1);
  }
}

// Endless mode: flakes resting on the edge of the glass melt away and fall
// again from the opposite side, so the pile turns over without growing.
static void endless_step(void) {
  if (!g_settings.endless || s_n == 0) return;
  s_melt_acc += s_n;
  while (s_melt_acc >= MELT_DIV) {
    s_melt_acc -= MELT_DIV;
    for (int t = 0; t < 32; t++) {
      int i = rng() % s_n;
      Pos p = s_flakes[i];
      if (on_glass(POS_X(p) + s_ddx, POS_Y(p) + s_ddy)) continue;
      Pos q;
      if (spawn_upstream(&q)) move_flake(i, POS_X(q), POS_Y(q));
      break;
    }
  }
}

// Battery snow and density changes: move the flake count towards the target a
// little each frame, so new snow falls in and lost snow thins out visibly.
static void adjust_count(void) {
  int budget = s_cap / 300 + 1;
  while (s_n > s_target && budget-- > 0) {
    int i = rng() % s_n;
    Pos p = s_flakes[i];
    grid_clr(POS_X(p), POS_Y(p));
    s_flakes[i] = s_flakes[--s_n];
  }
  while (s_n < s_target && budget-- > 0) {
    Pos q;
    if (!spawn_upstream(&q)) break;
    grid_set(POS_X(q), POS_Y(q));
    s_flakes[s_n++] = q;
  }
}

static int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

// Pushes every flake under the fingertip out to just beyond its edge.
static void sweep_at(int cx, int cy) {
  const int r2max = SWEEP_R * SWEEP_R;
  for (int i = 0; i < s_n; i++) {
    Pos p = s_flakes[i];
    int dx = POS_X(p) - cx, dy = POS_Y(p) - cy;
    if (dx > SWEEP_R || dx < -SWEEP_R || dy > SWEEP_R || dy < -SWEEP_R) continue;
    int d2 = dx * dx + dy * dy;
    if (d2 > r2max) continue;
    if (d2 == 0) {
      dx = (rng() & 1) ? 1 : -1;
      dy = -1;
      d2 = 2;
    }
    int d = isqrt(d2);
    for (int k = SWEEP_R + 1; k <= SWEEP_R + 12; k++) {
      int nx = cx + dx * k / d, ny = cy + dy * k / d;
      if (free_cell(nx, ny)) {
        move_flake(i, nx, ny);
        break;
      }
    }
  }
}

static void sweep_step(void) {
  if (!s_touch_pending) return;
  int dx = s_tx - s_px, dy = s_ty - s_py;
  int len = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
  int steps = len / (SWEEP_R / 2) + 1;
  for (int s = 1; s <= steps; s++) {
    sweep_at(s_px + dx * s / steps, s_py + dy * s / steps);
  }
  s_px = s_tx;
  s_py = s_ty;
  s_touch_pending = s_touch_down;   // a resting finger keeps clearing its spot
}

void sim_touch(int x, int y, bool down) {
  if (!g_settings.touch_sweep) return;
  if (!down) {
    s_touch_down = false;
    return;
  }
  if (!s_touch_down) {
    s_px = x;
    s_py = y;
  }
  s_tx = x;
  s_ty = y;
  s_touch_down = true;
  s_touch_pending = true;
}

void sim_stir(void) {
  for (int i = 0; i < s_n; i++) {
    for (int t = 0; t < 2; t++) {
      int y = rng() % SCREEN_H, x = rng() % SCREEN_W;
      if (free_cell(x, y)) {
        move_flake(i, x, y);
        break;
      }
    }
  }
}

// Falling snow keeps the 2013 physics: each attempt picks a flake, wobbles
// gravity sideways a little at random, and steps one pixel if the cell is free.
// Attempts scale with gravity, so a steeper tilt moves the snow faster.
//
// Landed snow (blocked in the gravity direction) follows a simpler rule: it
// tries one diagonal step downhill. The original let landed flakes shuffle
// sideways too, so piles flattened like sand, drained off the digits over
// minutes, and the globe never came to rest; with this rule piles hold a 45°
// slope and avalanches finish in seconds.
static void physics(void) {
  if (s_n == 0) return;
  // The two diagonals either side of "down", e.g. (-1,1) and (1,1) for (0,1).
  const int sl[2][2] = {
    { s_ddx - s_ddy < 0 ? -1 : s_ddx - s_ddy > 0, s_ddx + s_ddy < 0 ? -1 : s_ddx + s_ddy > 0 },
    { s_ddx + s_ddy < 0 ? -1 : s_ddx + s_ddy > 0, s_ddy - s_ddx < 0 ? -1 : s_ddy - s_ddx > 0 },
  };
  int attempts = (iabs(s_gx) + iabs(s_gy) + iabs(s_wind) + 10) * s_n / 2000;
  for (int a = 0; a < attempts; a++) {
    uint32_t r1 = rng();
    int i = r1 % s_n;
    Pos p = s_flakes[i];
    int x = POS_X(p), y = POS_Y(p);
    int dx, dy;

    if (blocked(x + s_ddx, y + s_ddy)) {
      const int *d = sl[(r1 >> 24) & 1];
      dx = d[0];
      dy = d[1];
      if (dx * s_gx + dy * s_gy <= 0) continue;   // never slide uphill
    } else {
      uint32_t r2 = rng(), r3 = rng();
      int fx = s_gx + s_wind, fy = s_gy;
      int side = (r2 % 3 == 0) ? (int)((r2 >> 8) % 5) - 2 : 0;
      int px = fx + fy * side, py = fy - fx * side;
      int ax = iabs(px), ay = iabs(py);
      dx = 0;
      dy = 0;
      if (ax > ay || (ay && (int)((r3 & 0xFFFF) % ay) < ax)) dx = px > 0 ? 1 : -1;
      if (ay > ax || (ax && (int)((r3 >> 16) % ax) < ay)) dy = py > 0 ? 1 : -1;
      if (!dx && !dy) continue;
    }

    int nx = x + dx, ny = y + dy;
    if (blocked(nx, ny)) continue;
    grid_clr(x, y);
    grid_set(nx, ny);
    s_flakes[i] = POS(nx, ny);
  }
}

static int count_unstable(void) {
  int c = 0;
  for (int i = 0; i < s_n; i++) {
    Pos p = s_flakes[i];
    if (!blocked(POS_X(p) + s_ddx, POS_Y(p) + s_ddy)) c++;
  }
  return c;
}

// The last few airborne flakes drop straight to rest so nothing is left
// hanging mid-air while the simulation sleeps.
static void settle_stragglers(void) {
  for (int i = 0; i < s_n; i++) {
    Pos p = s_flakes[i];
    int x = POS_X(p), y = POS_Y(p);
    if (blocked(x + s_ddx, y + s_ddy)) continue;
    while (!blocked(x + s_ddx, y + s_ddy)) {
      x += s_ddx;
      y += s_ddy;
    }
    move_flake(i, x, y);
  }
}

bool sim_step(void) {
  if (s_mask_dirty || s_need_spawn || !s_flakes) return true;   // walls rebuild on next render

  wind_step();
  endless_step();
  adjust_count();
  sweep_step();
  physics();

  // Settle detection, for the battery saver.
  s_awake_frames++;
  bool busy = g_settings.endless || s_gust_len || s_touch_down || s_touch_pending ||
              s_n != s_target || s_awake_frames < 20;
  if (busy) {
    s_quiet = 0;
    return true;
  }
  if (s_awake_frames % 10) return true;
  int unstable = count_unstable();
#ifdef SNOW_DEBUG
  s_dbg_unstable = unstable;
#endif
  if (unstable > s_n / 1000 + 2) {
    s_quiet = 0;
    return true;
  }
  if (++s_quiet < 2) return true;
  settle_stragglers();
  return false;
}

// ---------------------------------------------------------------- Drawing ---

static void colours(GColor *bg, GColor *snow, GColor *text) {
#ifdef PBL_COLOR
  *bg = g_settings.col_bg;
  *snow = g_settings.col_snow;
  *text = g_settings.col_time;
#else
  *bg = g_settings.invert ? GColorWhite : GColorBlack;
  *snow = *text = g_settings.invert ? GColorBlack : GColorWhite;
#endif
}

void sim_render(GContext *ctx, GRect bounds, GRect text_bounds) {
  if (!s_grid) return;
  if (s_mask_dirty) {
    layout_text(text_bounds);
    if (build_mask(ctx, bounds)) s_mask_dirty = false;
  }
  if (s_need_spawn && !s_mask_dirty && s_flakes) {
    scatter_spawn();
    s_need_spawn = false;
  }

  GColor bg, snow, text;
  colours(&bg, &snow, &text);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  draw_text(ctx, text);

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  for (int y = 0; y < SCREEN_H; y++) {
    s_rows[y] = gbitmap_get_data_row_info(fb, y).data;
  }
  if (gbitmap_get_format(fb) == GBitmapFormat1Bit) {
    bool white = gcolor_equal(snow, GColorWhite);
    for (int i = 0; i < s_n; i++) {
      int x = POS_X(s_flakes[i]), y = POS_Y(s_flakes[i]);
      uint8_t *b = &s_rows[y][x >> 3];
      if (white) *b |= 1u << (x & 7);
      else *b &= ~(1u << (x & 7));
    }
  } else {
    uint8_t c = snow.argb;
    for (int i = 0; i < s_n; i++) {
      int x = POS_X(s_flakes[i]), y = POS_Y(s_flakes[i]);
      if (x >= s_row_min[y] && x <= s_row_max[y]) s_rows[y][x] = c;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}
