#!/usr/bin/env python3
"""Screenshot a running Pebble emulator via QEMU's own monitor socket.

`pebble screenshot` goes through pypkjs, which wedges permanently the first
time a button is pressed. The QEMU monitor is a separate path and keeps
working. Usage: shot.py <platform> <out.png>
"""
import re, socket, subprocess, sys, time, os

def monitor_port(platform):
    ps = subprocess.run(['ps', 'ax', '-o', 'command'], capture_output=True, text=True).stdout
    for line in ps.splitlines():
        if 'qemu-pebble' in line and platform in line:
            m = re.search(r'-monitor tcp::(\d+)', line)
            if m:
                return int(m.group(1))
    raise SystemExit(f'no running qemu-pebble for {platform}')

def main():
    platform, out = sys.argv[1], sys.argv[2]
    ppm = '/tmp/pebble_shot.ppm'
    if os.path.exists(ppm):
        os.remove(ppm)
    s = socket.create_connection(('127.0.0.1', monitor_port(platform)), timeout=5)
    time.sleep(0.3)
    s.recv(65536)
    s.sendall(f'screendump {ppm}\n'.encode())
    time.sleep(0.8)
    s.close()
    if not os.path.exists(ppm):
        raise SystemExit('screendump produced no file')
    subprocess.run(['sips', '-s', 'format', 'png', ppm, '--out', out],
                   check=True, capture_output=True)
    print(out)

main()
