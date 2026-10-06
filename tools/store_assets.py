#!/usr/bin/env python3
"""Build the appstore image set from tools/record_store.py recordings.

Reads <rec_dir>/<platform>_{classic,midnight,festive}/ and writes into store/:

  <platform>_1_preview.gif    10s from the flick, real speed; the loop reads
                              as the globe being shaken again
  <platform>_2_time.png       settled snow at 10:08, default colours
  <platform>_3_snowfall.png   mid-fall after the flick
  <platform>_4_midnight.png   navy / ice / gold, big clock and date
                              (black-and-white watches: _4_inverted.png)
  <platform>_5_festive.png    green / white / red, heavy snow, watch tilted
                              (black-and-white watches: _5_tilt.png)
  icon_80x80.png, icon_144x144.png   pebble publish --icon-small / --icon-large
  banner_720x320.png                 store header image

The flick is found automatically, as the frame where the sky suddenly fills
with snow, so no frame numbers need picking by hand.

Screenshot filenames start with the platform because `pebble publish` infers
the platform from the text before the first underscore.

Usage: store_assets.py <rec_dir>
"""
import glob, json, math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import full_scale, snap_table  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'store')
BW = {'aplite', 'diorite', 'flint'}
GIF_BEFORE, GIF_AFTER = 0.0, 10.0     # seconds either side of the flick
SNOWFALL_AT = 3.5                     # seconds after the flick for shot 3
MAX_GIF = 4_300_000                   # the store's limit is 4.4MB

NAVY = (0, 0, 85)
ICE = (170, 255, 255)
GOLD = (255, 170, 0)
WHITE = (255, 255, 255)


def frames_of(rec, name):
    d = os.path.join(rec, name)
    times = json.load(open(os.path.join(d, 'times.json')))
    return d, times


NATIVE = {'aplite': (144, 168), 'basalt': (144, 168), 'chalk': (180, 180),
          'diorite': (144, 168), 'emery': (200, 228), 'flint': (144, 168),
          'gabbro': (260, 260)}


def frame(d, i):
    platform = os.path.basename(d).split('_')[0]
    im = normalise(Image.open(os.path.join(d, 'f%03d.png' % i)).convert('RGB'), platform)
    # The basalt emulator's dump has a 2px black frame round the 144x168
    # screen; the store rejects anything but the native size.
    w, h = NATIVE[platform]
    if im.size != (w, h):
        x, y = (im.width - w) // 2, (im.height - h) // 2
        im = im.crop((x, y, x + w, y + h))
    return im


def normalise(im, platform):
    """Frames recorded before shot.py snapped colours exactly (or by an older
    copy of it) may still be on the emulator's reduced scale; snap them to the
    watch's 0/85/170/255 levels the same way."""
    levels = sorted({v for band in im.split() for _, v in band.getcolors(256)} - {0})
    if not levels or set(levels) <= {85, 170, 255}:
        return im
    table = list(snap_table(full_scale(levels, platform)))
    return im.point(table * 3)


def sky_snow(im):
    """Fraction of lit pixels in the band above the clock."""
    w, h = im.size
    band = im.crop((w // 4, h // 10, w * 3 // 4, h // 4))
    sky = sorted(band.getcolors(w * h), reverse=True)[0][1]
    lit = sum(n for n, c in band.getcolors(w * h) if c != sky)
    return lit / (band.width * band.height)


def find_flick(d, times):
    """The flick throws the settled pile back into the sky: the first frame
    after 20s whose sky band jumps well above the settled level."""
    n = len(times)
    start = next(i for i, t in enumerate(times) if t > 20)
    base = sky_snow(frame(d, start))
    for i in range(start, n):
        if sky_snow(frame(d, i)) > base + 0.04:
            return i
    raise SystemExit('no flick found in ' + d)


def index_at(times, t):
    return min(range(len(times)), key=lambda i: abs(times[i] - t))


def save_gif(d, times, start, end, path):
    frames = [frame(d, i) for i in range(start, end)]
    colours = sorted({c for f in frames for _, c in f.getcolors(1 << 16)})
    pal = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    step = 1
    while True:
        idx = list(range(0, len(frames), step))
        pframes = [frames[k].quantize(palette=pal, dither=Image.Dither.NONE) for k in idx]
        stamps = [times[start + k] for k in idx] + [times[end - 1] + 0.6]
        durations = [max(20, int(round((stamps[k + 1] - stamps[k]) * 1000)))
                     for k in range(len(idx))]
        durations[-1] = 1200   # linger on the settled globe before looping
        pframes[0].save(path, save_all=True, append_images=pframes[1:], duration=durations,
                        loop=0, optimize=False, disposal=1)
        if os.path.getsize(path) <= MAX_GIF or step > 4:
            return
        step += 1


# ----------------------------------------------------------------- artwork --

def snow_globe(size, ss=4):
    """The icon: a snow globe with a drift inside and flakes in the air."""
    S = size * ss
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cx, r = S / 2, S * 0.40
    cy = S * 0.44
    # Base.
    d.polygon([(cx - r * 0.78, cy + r * 0.80), (cx + r * 0.78, cy + r * 0.80),
               (cx + r * 0.98, S * 0.97), (cx - r * 0.98, S * 0.97)], fill=(170, 0, 0))
    d.rectangle((cx - r * 0.98, S * 0.90, cx + r * 0.98, S * 0.97), fill=(85, 0, 0))
    # Glass and sky.
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=NAVY, outline=ICE, width=max(2, S // 40))
    # Drift: a bumpy pile clipped to the globe.
    drift = Image.new('L', (S, S), 0)
    dd = ImageDraw.Draw(drift)
    pts = [(cx - r, S)]
    for k in range(9):
        x = cx - r + 2 * r * k / 8
        bump = (0.08 if k % 2 else 0.0) + (0.05 if k in (3, 5) else 0)
        pts.append((x, cy + r * (0.42 - bump)))
    pts.append((cx + r, S))
    dd.polygon(pts, fill=255)
    globe = Image.new('L', (S, S), 0)
    ImageDraw.Draw(globe).ellipse((cx - r * 0.93, cy - r * 0.93, cx + r * 0.93, cy + r * 0.93),
                                  fill=255)
    snow = Image.new('RGBA', (S, S), WHITE + (255,))
    im.paste(snow, (0, 0), Image.composite(drift, Image.new('L', (S, S), 0), globe))
    # Flakes.
    rnd = random.Random(7)
    for _ in range(14):
        a, rr = rnd.uniform(0, 2 * math.pi), r * math.sqrt(rnd.uniform(0.02, 0.55))
        x, y = cx + rr * math.cos(a), cy - r * 0.18 + rr * math.sin(a) * 0.8
        f = S * 0.022
        d.ellipse((x - f, y - f, x + f, y + f), fill=WHITE)
    # Glint.
    d.arc((cx - r * 0.78, cy - r * 0.78, cx + r * 0.78, cy + r * 0.78), 200, 245,
          fill=(255, 255, 255, 200), width=max(2, S // 50))
    return im.resize((size, size), Image.LANCZOS)


def make_icon(size):
    bg = Image.new('RGBA', (size, size), NAVY + (255,))
    bg.alpha_composite(snow_globe(size))
    return bg.convert('RGB')


def round_mask(im):
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).ellipse((0, 0, im.width - 1, im.height - 1), fill=255)
    return m


def make_banner(rec):
    W, H = 720, 320
    out = Image.new('RGBA', (W, H), NAVY + (255,))
    d = ImageDraw.Draw(out)
    rnd = random.Random(3)
    for _ in range(260):   # falling snow
        x, y = rnd.randrange(W), rnd.randrange(H)
        d.point((x, y), fill=ICE if rnd.random() < 0.4 else WHITE)
    pts = [(0, H)] + [(x, H - 26 - 10 * math.sin(x / 37) - 6 * math.sin(x / 13))
                      for x in range(0, W + 1, 6)] + [(W, H)]
    d.polygon(pts, fill=WHITE)

    def last(name):
        dd, times = frames_of(rec, name)
        return frame(dd, len(times) - 1)

    gabbro = last('gabbro_midnight')
    emery = last('emery_festive')
    scale = 0.86
    gabbro = gabbro.resize((round(gabbro.width * scale), round(gabbro.height * scale)), Image.LANCZOS)
    emery = emery.resize((round(emery.width * scale), round(emery.height * scale)), Image.LANCZOS)
    gx, gy = W - gabbro.width - 24, (H - gabbro.height) // 2 - 6
    ex, ey = gx - emery.width - 26, (H - emery.height) // 2 - 6
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle((ex - 6, ey - 6, ex + emery.width + 6, ey + emery.height + 6), 16,
                         fill=(0, 0, 0, 150))
    sd.ellipse((gx - 6, gy - 6, gx + gabbro.width + 6, gy + gabbro.height + 6), fill=(0, 0, 0, 150))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(8)))
    bez = ImageDraw.Draw(out)
    bez.rounded_rectangle((ex - 5, ey - 5, ex + emery.width + 4, ey + emery.height + 4), 14,
                          fill=(30, 30, 34))
    bez.ellipse((gx - 5, gy - 5, gx + gabbro.width + 4, gy + gabbro.height + 4), fill=(30, 30, 34))
    out.paste(emery, (ex, ey))
    out.paste(gabbro, (gx, gy), round_mask(gabbro))

    font = '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf'
    d = ImageDraw.Draw(out)
    d.text((30, 92), 'Snow', font=ImageFont.truetype(font, 64), fill=WHITE)
    tag = ImageFont.truetype(font, 18)
    d.text((33, 176), 'A snow globe', font=tag, fill=ICE)
    d.text((33, 199), 'on your wrist', font=tag, fill=ICE)
    return out.convert('RGB')


def main():
    rec = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    for old in glob.glob(os.path.join(OUT, '*_[1-5]_*')):
        os.remove(old)
    platforms = sorted({os.path.basename(p).split('_')[0]
                        for p in glob.glob(os.path.join(rec, '*_classic'))})
    written = []
    for p in platforms:
        d, times = frames_of(rec, p + '_classic')
        flick = find_flick(d, times)
        t0 = times[flick]
        start = index_at(times, t0 - GIF_BEFORE)
        end = index_at(times, t0 + GIF_AFTER)
        path = os.path.join(OUT, '%s_1_preview.gif' % p)
        save_gif(d, times, start, end, path)
        written.append(path)
        shots = [('2_time', d, max(0, flick - 2)),
                 ('3_snowfall', d, index_at(times, t0 + SNOWFALL_AT))]
        for slot, variant in (('4_' + ('inverted' if p in BW else 'midnight'), 'midnight'),
                              ('5_' + ('tilt' if p in BW else 'festive'), 'festive')):
            vd, vt = frames_of(rec, '%s_%s' % (p, variant))
            shots.append((slot, vd, len(vt) - 1))
        for slot, sd, i in shots:
            path = os.path.join(OUT, '%s_%s.png' % (p, slot))
            frame(sd, i).save(path)
            written.append(path)
    for size in (80, 144):
        path = os.path.join(OUT, 'icon_%dx%d.png' % (size, size))
        make_icon(size).save(path)
        written.append(path)
    if os.path.isdir(os.path.join(rec, 'gabbro_midnight')) and os.path.isdir(
            os.path.join(rec, 'emery_festive')):
        path = os.path.join(OUT, 'banner_720x320.png')
        make_banner(rec).save(path)
        written.append(path)
    for w in written:
        im = Image.open(w)
        print('%-34s %dx%d  %dKB' % (os.path.relpath(w, ROOT), im.size[0], im.size[1],
                                     os.path.getsize(w) // 1024))


if __name__ == '__main__':
    main()
