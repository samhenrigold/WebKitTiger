#!/usr/bin/env python3
"""Check a full-screen screencapture of scrolltest.html for mirrored bands.

The bands are rgb(6*index, 90, 180): strongly blue (blue - green is large) with a
red channel that rises down the page. The screenshot is colour managed, so nothing
absolute is trusted -- band pixels are found by blue - green, and within them the
red channel must not fall going down the window. Any falling run is a vertically
mirrored blit.

The magenta bar locates the web view: its x range gives the content columns, and
when the bar is position:fixed it must sit at the top of them.

  check-scrollshot.py shot.png [...]
"""
import sys, subprocess, struct, zlib

def read_png(path):
    raw = open(path, 'rb').read()
    assert raw[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, w = 8, b'', None
    while pos < len(raw):
        ln, typ = struct.unpack('>I4s', raw[pos:pos+8])
        data = raw[pos+8:pos+8+ln]
        if typ == b'IHDR':
            w, h, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', data)
            assert depth == 8 and interlace == 0 and color in (2, 6), (depth, color, interlace)
            channels = 3 if color == 2 else 4
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    buf = zlib.decompress(idat)
    stride = w * channels
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = buf[p]; line = bytearray(buf[p+1:p+1+stride]); p += 1 + stride
        for i in range(stride):
            a = line[i-channels] if i >= channels else 0
            b = prev[i]
            c = prev[i-channels] if i >= channels else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b-c), abs(a-c), abs(a+b-2*c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line); prev = line
    return w, h, channels, rows

def check(path):
    w, h, ch, rows = read_png(path)

    def px(x, y):
        o = x * ch
        return rows[y][o], rows[y][o+1], rows[y][o+2]

    # The magenta bar: the only saturated red+blue, no green thing on screen.
    magenta = [(x, y) for y in range(0, h, 2) for x in range(0, w, 4)
               if px(x, y)[0] > 160 and px(x, y)[1] < 90 and px(x, y)[2] > 160]
    if not magenta:
        return '%s: BAR NOT FOUND (page not loaded or not visible)' % path
    x0, x1 = min(p[0] for p in magenta), max(p[0] for p in magenta)
    barTop, barBottom = min(p[1] for p in magenta), max(p[1] for p in magenta)

    violations, columns = [], 0
    # Only the right half of the view: the row labels are white text on the left and
    # their antialiased edges are part band, part white, which reads as a band pixel
    # with a lifted red channel and would look like a drop on the row below.
    for x in range(x0 + (x1 - x0) // 2, x1 - 8, 8):
        # Walk down from under the bar and stop at the window edge: the first gap
        # of more than 8 rows with no band pixel. Only the right half is scanned, so there
        run, lastY = [], None
        for y in range(barBottom + 2, h):
            r, g, b = px(x, y)
            if b - g > 80 and g < 100:
                run.append((y, r))
                lastY = y
            elif lastY is not None and y - lastY > 8:
                break
        if len(run) < 200:
            continue
        columns += 1
        drops = [(y, run[i-1][1], r) for i, (y, r) in enumerate(run) if i and r < run[i-1][1] - 12]
        if drops:
            violations.append((x, len(drops), drops[:2]))
    if not columns:
        return '%s: bar at y=%d..%d x=%d..%d but no band columns found' % (path, barTop, barBottom, x0, x1)
    if violations:
        return '%s: MIRRORED -- %d of %d band columns fall back up the page, e.g. x=%d %d drops %r' % (
            (path, len(violations), columns) + violations[0])
    return '%s: OK -- bar y=%d..%d, %d band columns, red channel never falls' % (
        path, barTop, barBottom, columns)

if __name__ == '__main__':
    for p in sys.argv[1:]:
        print(check(p))
