# Genera las propuestas de logo de Keyla. Sin dependencias: rasterizador por
# barrido de lineas con supermuestreo, y el PNG escrito a mano con zlib.

import math
import os
import struct
import zlib

SS = 1024          # lienzo supermuestreado
MASTER = 256       # tamano final del maestro

BG      = (0x1d, 0x1f, 0x24)
IVORY   = (0xf2, 0xf0, 0xea)
GREEN   = (0x7f, 0xb0, 0x69)
SLATE   = (0x14, 0x16, 0x1a)
GREY    = (0x9a, 0xa2, 0xad)


class Canvas:
    def __init__(self, size, colour):
        self.n = size
        self.buf = bytearray(bytes(colour) * (size * size))

    def span(self, y, x0, x1, colour):
        if y < 0 or y >= self.n:
            return
        x0 = max(0, int(math.ceil(x0)))
        x1 = min(self.n, int(math.ceil(x1)))
        if x1 <= x0:
            return
        base = (y * self.n + x0) * 3
        self.buf[base:base + (x1 - x0) * 3] = bytes(colour) * (x1 - x0)

    def polygon(self, points, colour):
        pts = [(x * self.n, y * self.n) for x, y in points]
        ys = [p[1] for p in pts]
        top = max(0, int(math.floor(min(ys))))
        bottom = min(self.n - 1, int(math.ceil(max(ys))))
        count = len(pts)
        for y in range(top, bottom + 1):
            cy = y + 0.5
            xs = []
            for i in range(count):
                x1, y1 = pts[i]
                x2, y2 = pts[(i + 1) % count]
                if (y1 <= cy < y2) or (y2 <= cy < y1):
                    xs.append(x1 + (cy - y1) * (x2 - x1) / (y2 - y1))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                self.span(y, xs[i], xs[i + 1], colour)

    def rounded_rect(self, x0, y0, x1, y1, radius, colour):
        n = self.n
        X0, Y0, X1, Y1 = x0 * n, y0 * n, x1 * n, y1 * n
        R = radius * n
        for y in range(max(0, int(Y0)), min(n, int(math.ceil(Y1)))):
            cy = y + 0.5
            if cy < Y0 + R:
                d = R - (cy - Y0)
            elif cy > Y1 - R:
                d = R - (Y1 - cy)
            else:
                d = 0.0
            inset = R - math.sqrt(max(0.0, R * R - d * d)) if d > 0 else 0.0
            self.span(y, X0 + inset, X1 - inset, colour)

    def rect(self, x0, y0, x1, y1, colour):
        self.rounded_rect(x0, y0, x1, y1, 0.0, colour)

    def disc(self, cx, cy, r, colour):
        n = self.n
        CX, CY, R = cx * n, cy * n, r * n
        for y in range(max(0, int(CY - R)), min(n, int(math.ceil(CY + R)))):
            dy = (y + 0.5) - CY
            if abs(dy) > R:
                continue
            dx = math.sqrt(R * R - dy * dy)
            self.span(y, CX - dx, CX + dx, colour)

    def ring(self, cx, cy, inner, outer, a0, a1, colour):
        """Arco entre dos radios, angulos en grados, 0 = este, sentido horario."""
        n = self.n
        CX, CY = cx * n, cy * n
        RI, RO = inner * n, outer * n
        for y in range(max(0, int(CY - RO)), min(n, int(math.ceil(CY + RO)))):
            dy = (y + 0.5) - CY
            if abs(dy) > RO:
                continue
            xo = math.sqrt(RO * RO - dy * dy)
            xi = math.sqrt(RI * RI - dy * dy) if abs(dy) < RI else 0.0
            for x0, x1 in ((CX - xo, CX - xi), (CX + xi, CX + xo)):
                x = int(math.ceil(x0))
                start = None
                while x < x1:
                    ang = math.degrees(math.atan2(dy, (x + 0.5) - CX))
                    inside = a0 <= ang <= a1
                    if inside and start is None:
                        start = x
                    elif not inside and start is not None:
                        self.span(y, start, x, colour)
                        start = None
                    x += 1
                if start is not None:
                    self.span(y, start, x1, colour)

    def thick_line(self, p0, p1, width, colour):
        (x0, y0), (x1, y1) = p0, p1
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy)
        nx, ny = -dy / length * width / 2, dx / length * width / 2
        self.polygon([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny),
                      (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)], colour)

    def downsample(self, out_size):
        factor = self.n // out_size
        out = bytearray(out_size * out_size * 3)
        n = self.n
        buf = self.buf
        area = factor * factor
        for oy in range(out_size):
            for ox in range(out_size):
                r = g = b = 0
                for sy in range(factor):
                    base = ((oy * factor + sy) * n + ox * factor) * 3
                    for sx in range(factor):
                        i = base + sx * 3
                        r += buf[i]
                        g += buf[i + 1]
                        b += buf[i + 2]
                o = (oy * out_size + ox) * 3
                out[o] = r // area
                out[o + 1] = g // area
                out[o + 2] = b // area
        return out, out_size


def write_png(path, pixels, size):
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        raw += pixels[y * size * 3:(y + 1) * size * 3]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def box_down(pixels, size, out_size):
    factor = size // out_size
    out = bytearray(out_size * out_size * 3)
    area = factor * factor
    for oy in range(out_size):
        for ox in range(out_size):
            r = g = b = 0
            for sy in range(factor):
                base = ((oy * factor + sy) * size + ox * factor) * 3
                for sx in range(factor):
                    i = base + sx * 3
                    r += pixels[i]
                    g += pixels[i + 1]
                    b += pixels[i + 2]
            o = (oy * out_size + ox) * 3
            out[o] = r // area
            out[o + 1] = g // area
            out[o + 2] = b // area
    return out


# ── A · K de teclas ────────────────────────────────────────────────────────
# La identidad es el nombre. Una K rotunda cuyo palo es una tecla blanca —canto
# recto arriba, redondeado abajo, como una tecla de verdad— y cuyo brazo alto
# lleva el verde con el que Keyla enciende las notas que tienes que tocar.
def logo_a():
    c = Canvas(SS, BG)
    c.rounded_rect(0.0, 0.0, 1.0, 1.0, 0.22, BG)

    # Los diagonales primero: el palo los tapa despues y la union queda limpia.
    c.thick_line((0.30, 0.505), (0.80, 0.845), 0.150, IVORY)
    c.thick_line((0.30, 0.495), (0.79, 0.155), 0.150, GREEN)

    # El palo: recto arriba y redondo abajo. Es lo que lo hace tecla y no barra.
    c.rect(0.195, 0.155, 0.355, 0.72, IVORY)
    c.rounded_rect(0.195, 0.60, 0.355, 0.845, 0.048, IVORY)

    return c


# ── B · Teclado encendido ──────────────────────────────────────────────────
# La identidad es lo que hace: encender las teclas que tienes que tocar. Cinco
# teclas blancas, tres negras, y una triada iluminada en verde.
def logo_b():
    c = Canvas(SS, BG)
    c.rounded_rect(0.0, 0.0, 1.0, 1.0, 0.22, BG)

    left, right = 0.13, 0.87
    top, bottom = 0.20, 0.80
    keys = 5
    width = (right - left) / keys
    lit = (0, 2, 4)                      # una triada

    for i in range(keys):
        x0 = left + i * width
        colour = GREEN if i in lit else IVORY
        c.rounded_rect(x0 + 0.008, top, x0 + width - 0.008, bottom, 0.025, colour)

    # Negras entre la 1-2, 2-3 y 4-5, mas cortas.
    for i in (0, 1, 3):
        x = left + (i + 1) * width
        c.rounded_rect(x - width * 0.30, top, x + width * 0.30, top + (bottom - top) * 0.60,
                       0.02, SLATE)

    return c


# ── C · Key + La ───────────────────────────────────────────────────────────
# El juego del nombre, literal: una tecla y la nota que suena en ella. La
# version anterior eran arcos de sonido y parecia el icono de volumen de
# Windows, que para un icono es un defecto grave: dice otra cosa.
def logo_c():
    c = Canvas(SS, BG)
    c.rounded_rect(0.0, 0.0, 1.0, 1.0, 0.22, BG)

    # Dos teclas blancas y la negra entre ellas.
    c.rect(0.16, 0.15, 0.375, 0.72, IVORY)
    c.rounded_rect(0.16, 0.60, 0.375, 0.85, 0.045, IVORY)
    c.rect(0.395, 0.15, 0.61, 0.72, IVORY)
    c.rounded_rect(0.395, 0.60, 0.61, 0.85, 0.045, IVORY)
    c.rounded_rect(0.31, 0.15, 0.455, 0.52, 0.028, SLATE)

    # La nota: cabeza y plica. Es "la" que suena.
    c.thick_line((0.795, 0.235), (0.795, 0.635), 0.062, GREEN)
    c.disc(0.685, 0.655, 0.135, GREEN)

    return c


def render(name, builder):
    c = builder()
    master, size = c.downsample(MASTER)
    write_png(os.path.join(OUTDIR, name + '.png'), master, size)
    return master


OUTDIR = os.environ.get('KEYLA_LOGO_OUT', '.')


def sheet(masters, path):
    """Hoja de contacto: cada propuesta grande y luego a 64, 32 y 16 px reales."""
    pad = 16
    cell = MASTER
    small = [64, 32, 16]
    width = pad + len(masters) * (cell + pad)
    height = pad + cell + pad + 64 + pad
    out = bytearray(bytes(( 0x0f, 0x10, 0x13 )) * (width * height))

    def blit(pixels, size, ox, oy):
        for y in range(size):
            src = y * size * 3
            dst = ((oy + y) * width + ox) * 3
            out[dst:dst + size * 3] = pixels[src:src + size * 3]

    for i, master in enumerate(masters):
        x = pad + i * (cell + pad)
        blit(master, cell, x, pad)
        sx = x
        for s in small:
            blit(box_down(master, MASTER, s), s, sx, pad + cell + pad + (64 - s))
            sx += s + 12

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += out[y * width * 3:(y + 1) * width * 3]

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data
                + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    os.makedirs(OUTDIR, exist_ok=True)
    a = render('logo_a_k', logo_a)
    b = render('logo_b_teclado', logo_b)
    d = render('logo_c_escucha', logo_c)
    sheet([a, b, d], os.path.join(OUTDIR, 'logo_propuestas.png'))
    print('listo:', OUTDIR)
