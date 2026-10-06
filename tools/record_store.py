#!/usr/bin/env python3
"""Record everything tools/store_assets.py needs, one emulator at a time.

Builds three store variants (all pinned to 10:08 and held upright or tilted
by build flags, so nothing depends on pypkjs once installed):

  classic   default colours, flick at STIR_S          -> GIF, time, snowfall
  midnight  preset 1: colours, big clock, date         -> alt colours
  festive   preset 5: green/white/red, heavy, 25° tilt -> second scheme + tilt

then, per platform, installs each one and records it with tools/record.py into
<rec_dir>/<platform>_<variant>/. Runs `pebble kill` between platforms because
the Mac is short of memory, so make sure no other session is using emulators.

Usage: record_store.py <rec_dir> [platform ...]
"""
import os, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import full_scale, snap_table  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SDK = '4.33.1'
PLATFORMS = ['aplite', 'basalt', 'chalk', 'diorite', 'emery', 'flint', 'gabbro']
STIR_S = 45

VARIANTS = {
    'classic': ({'SNOW_DEMO_TILT': '0', 'SNOW_DEMO_STIR': str(STIR_S)}, 75, 0.12),
    'midnight': ({'SNOW_PRESET': '1', 'SNOW_DEMO_TILT': '0'}, 50, 1.0),
    'festive': ({'SNOW_PRESET': '5', 'SNOW_DEMO_TILT': '25'}, 60, 1.0),
}


def pebble(*args, timeout=240):
    env = dict(os.environ, PATH=os.path.expanduser('~/.local/bin') + ':' + os.environ['PATH'])
    try:
        r = subprocess.run(['pebble', *args], capture_output=True, text=True, timeout=timeout,
                           env=env, cwd=ROOT)
        return r.returncode == 0, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return False, 'timeout'


def build_variants(out):
    paths = {}
    for name, (flags, _, _) in VARIANTS.items():
        env = dict(os.environ, SNOW_SHOW_TIME='1008', **flags,
                   PATH=os.path.expanduser('~/.local/bin') + ':' + os.environ['PATH'])
        for k in ('SNOW_DEBUG', 'SNOW_PRESET', 'SNOW_DEMO_STIR'):
            if k not in flags:
                env.pop(k, None)
        r = subprocess.run(['pebble', 'build'], capture_output=True, text=True, env=env, cwd=ROOT)
        if r.returncode:
            sys.exit('build failed for %s:\n%s' % (name, r.stdout[-2000:]))
        paths[name] = os.path.join(out, 'snow_%s.pbw' % name)
        shutil.copy(os.path.join(ROOT, 'build', 'snow.pbw'), paths[name])
        print('built', name)
    return paths


def looks_like_snow(platform, variant):
    """Another face can stay active through an install; check the screen's
    dominant colour matches the variant's sky before recording."""
    from PIL import Image
    shot = os.path.join(tempfile.gettempdir(), 'snow_check.png')
    subprocess.run(['python3', os.path.join(TOOLS, 'shot.py'), '%s@%s' % (platform, SDK), shot],
                   capture_output=True)
    if not os.path.exists(shot):
        return False
    im = Image.open(shot).convert('RGB')
    levels = sorted({v for band in im.split() for _, v in band.getcolors(256)} - {0}) or [255]
    im = im.point(list(snap_table(full_scale(levels, platform))) * 3)
    w, h = im.size
    centre = im.crop((w // 4, h // 8, w * 3 // 4, h // 3))   # sky above the clock
    colours = sorted(centre.getcolors(w * h), reverse=True)
    top = colours[0][1]
    sky = {'classic': [(0, 0, 0)], 'midnight': [(0, 0, 85), (255, 255, 255)],
           'festive': [(0, 85, 0), (0, 0, 0)]}[variant]
    return top in sky


def install(platform, pbw, variant):
    for attempt in range(3):
        ok, out = pebble('install', '--emulator', platform, '--sdk', SDK, pbw)
        if ok:
            time.sleep(2)
            if looks_like_snow(platform, variant):
                return True
            print('  wrong face on screen, dismissing and retrying')
            pebble('emu-button', '--emulator', platform, '--sdk', SDK, 'click', 'back')
            time.sleep(2)
        else:
            print('  install failed (%s), retrying' % out.strip().splitlines()[-1][:80])
            time.sleep(5)
    return False


def main():
    rec = os.path.abspath(sys.argv[1])
    platforms = sys.argv[2:] or PLATFORMS
    os.makedirs(rec, exist_ok=True)
    pbws = build_variants(rec)
    for platform in platforms:
        print(platform, flush=True)
        for variant, (_, seconds, interval) in VARIANTS.items():
            if not install(platform, pbws[variant], variant):
                print('  FAILED to get %s on screen' % variant, flush=True)
                continue
            d = os.path.join(rec, '%s_%s' % (platform, variant))
            subprocess.run(['python3', os.path.join(TOOLS, 'record.py'), '%s@%s' % (platform, SDK),
                            d, str(seconds), '--interval', str(interval)], check=True)
        pebble('kill')
        time.sleep(3)


if __name__ == '__main__':
    main()
