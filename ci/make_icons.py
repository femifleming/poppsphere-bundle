"""poppsphere icons, in the bundle's own look (after Oval's ovalprocess): not the Septambres letters."""
import sys
from PIL import Image, ImageDraw
S = 1024

def grad(W, top, bot):
    g = Image.new("RGBA", (W, W))
    d = ImageDraw.Draw(g)
    for y in range(W):
        t = y / (W - 1)
        d.line([(0, y), (W, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,))
    return g

def tile(W, field):
    m, r = round(W * 100 / 1024), round(W * 185 / 1024)
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).rounded_rectangle([m, m, W - m - 1, W - m - 1], radius=r, fill=255)
    out = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    out.paste(field, (0, 0), mask)
    return out, m

def triangle(d, W, x, y, h, red, tail):
    # the play triangle: red, with a pale trail falling off behind it, as on the right rail
    u = W / 1024
    d.polygon([(x - 40 * u, y + h * 0.15), (x + h * 0.95, y + h * 0.62), (x - 40 * u, y + h * 0.92)], fill=tail)
    d.polygon([(x - 80 * u, y - h * 0.5), (x + h * 0.85, y), (x - 80 * u, y + h * 0.5)], fill=red)

def openprocess(W, sphere=False):
    # openprocess: the blue field; openfield: green
    img, m = tile(W, grad(W, (62, 176, 96), (36, 124, 64)) if sphere else grad(W, (64, 140, 230), (40, 92, 196)))
    d = ImageDraw.Draw(img)
    u = W / 1024
    # dotted white mix window
    x0, y0, x1, y1 = 230 * u, 250 * u, 690 * u, 770 * u
    d.rectangle([x0, y0, x1, y1], fill=(255, 255, 255, 255))
    d.rectangle([x0, y0 - 26 * u, x1 + 26 * u, y0], fill=(200, 200, 212, 255))       # grey rail on top
    d.rectangle([x1, y0 - 26 * u, x1 + 26 * u, y1], fill=(200, 200, 212, 255))       # and down the side
    step = 34 * u
    yy = y0 + step
    while yy < y1 - 4 * u:
        xx = x0 + step
        while xx < x1 - 4 * u:
            d.ellipse([xx - 2.4 * u, yy - 2.4 * u, xx + 2.4 * u, yy + 2.4 * u], fill=(170, 190, 176, 255) if sphere else (170, 176, 200, 255))
            xx += step
        yy += step
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    if sphere:   # the black ring with its coloured tab
        R, w = 130 * u, 56 * u
        d.rectangle([cx + 30 * u, cy - R - 40 * u, cx + 130 * u, cy - R + 14 * u], fill=(233, 120, 170, 255))
        d.ellipse([cx - R, cy - R, cx + R, cy + R], fill=(28, 28, 32, 255))
        d.ellipse([cx - R + w, cy - R + w, cx + R - w, cy + R - w], fill=(255, 255, 255, 255))
    else:        # a browser square: coloured frame, coloured centre
        a, b = 150 * u, 52 * u
        d.rectangle([cx - a, cy - a, cx + a, cy + a], fill=(232, 128, 40, 255))
        d.rectangle([cx - a + b, cy - a + b, cx + a - b, cy + a - b], fill=(255, 255, 255, 255))
        c = 54 * u
        d.rectangle([cx - c, cy - c, cx + c, cy + c], fill=(40, 70, 170, 255))
    triangle(d, W, 790 * u, 510 * u, 230 * u, (214, 40, 36, 255), (176, 236, 190, 255) if sphere else (170, 214, 250, 255))
    return img

def quell(W):
    img, m = tile(W, grad(W, (206, 40, 34), (186, 30, 28)))
    d = ImageDraw.Draw(img)
    u = W / 1024
    x0, y0, a = 225 * u, 280 * u, 440 * u          # the gold / orange checker square
    n = 6
    c = a / n
    for i in range(n):
        for j in range(n):
            col = (247, 196, 30, 255) if (i + j) % 2 == 0 else (240, 140, 30, 255)
            d.rectangle([x0 + i * c, y0 + j * c, x0 + (i + 1) * c, y0 + (j + 1) * c], fill=col)
    d.rectangle([x0, y0, x0 + a, y0 + a], outline=(255, 255, 255, 255), width=round(16 * u))
    triangle(d, W, 790 * u, 500 * u, 230 * u, (120, 180, 235, 255), (230, 170, 160, 255))
    return img

ICONS = {"openprocess": lambda W: openprocess(W), "openfield": lambda W: openprocess(W, True), "Quell": quell}

def icon(name, size=S):
    return ICONS[name](size * 4).resize((size, size), Image.LANCZOS)

if __name__ == "__main__":
    if sys.argv[1] == "sheet":
        sheet = Image.new("RGBA", (660, 230), "#ffffff")
        for i, n in enumerate(ICONS):
            im = icon(n, 200); sheet.paste(im, (10 + i * 220, 10), im)
        sheet.save(sys.argv[2])
    else:
        icon(sys.argv[1], int(sys.argv[3]) if len(sys.argv) > 3 else S).save(sys.argv[2])
