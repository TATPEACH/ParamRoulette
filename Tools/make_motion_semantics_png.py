# 画 docs/motion-semantics.png（README 里那张六个运动语义的曲线卡片）。在仓库根目录跑：python Tools/make_motion_semantics_png.py
# 曲线坐标照抄 Source/Theme.cpp 的 drawSemanticGlyph（单位坐标，y 向下），颜色用 Grid 主题。
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = 2          # 输出像素比（2x，高分屏清晰）
SS  = 4          # 先按 4x 画再缩到 2x，抗锯齿
K   = OUT * SS   # 逻辑 px → 画布 px

CANVAS, DOT   = (0x15, 0x15, 0x15), (0x24, 0x24, 0x24)
MODULE, STROKE = (0x2a, 0x2a, 0x2a), (0x36, 0x36, 0x36)
WELL, GUIDE   = (0x1c, 0x1c, 0x1c), (0x29, 0x29, 0x29)
CYAN          = (0x1b, 0xe3, 0xe5)
TEXT, DIM     = (0xe8, 0xe8, 0xe8), (0x9c, 0x9c, 0x9c)

FONT_BOLD = "C:/Windows/Fonts/Noto Sans SC Bold (TrueType).otf"
FONT_REG  = "C:/Windows/Fonts/Noto Sans SC (TrueType).otf"

# (名字, 说明, [起点, (c1, c2, 终点), ...])
CARDS = [
    ("保持原语义", "只做音色抖动",       [(0.1, 0.5), ((0.37, 0.5), (0.63, 0.5), (0.9, 0.5))]),
    ("上扬",       "单段 ramp · inv 0",  [(0.1, 0.84), ((0.38, 0.84), (0.52, 0.16), (0.9, 0.16))]),
    ("下降",       "单段 ramp · inv 1" , [(0.1, 0.16), ((0.38, 0.16), (0.52, 0.84), (0.9, 0.84))]),
    ("先扬后抑",   "双段 peak · inv 0",  [(0.1, 0.84), ((0.26, 0.84), (0.32, 0.16), (0.46, 0.16)),
                                                       ((0.6, 0.16), (0.68, 0.84), (0.9, 0.84))]),
    ("先抑后扬",   "双段 peak · inv 1",  [(0.1, 0.16), ((0.26, 0.16), (0.32, 0.84), (0.46, 0.84)),
                                                       ((0.6, 0.84), (0.68, 0.16), (0.9, 0.16))]),
    ("漫游",       "带惯性的随机游走",   [(0.08, 0.55), ((0.2, 0.12), (0.3, 0.12), (0.4, 0.48)),
                                                        ((0.5, 0.86), (0.62, 0.86), (0.7, 0.42)),
                                                        ((0.76, 0.2), (0.86, 0.24), (0.92, 0.4))]),
]

# 逻辑尺寸
MARGIN, GAP = 16, 12
CARD_W, CARD_H = 136, 160
WELL_INSET, WELL_H = 10, 92
W = MARGIN * 2 + CARD_W * len(CARDS) + GAP * (len(CARDS) - 1)
H = MARGIN * 2 + CARD_H


def sample(path, steps=160):
    """把 [起点, (c1, c2, 终点), ...] 采样成折线（单位坐标）。"""
    pts = [path[0]]
    p0 = path[0]
    for c1, c2, p3 in path[1:]:
        for i in range(1, steps + 1):
            t = i / steps
            u = 1 - t
            x = u**3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t**3 * p3[0]
            y = u**3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t**3 * p3[1]
            pts.append((x, y))
        p0 = p3
    return pts


def stroke(draw, pts, colour, width):
    draw.line(pts, fill=colour, width=width, joint="curve")
    r = width / 2
    for (x, y) in (pts[0], pts[-1]):
        draw.ellipse((x - r, y - r, x + r, y + r), fill=colour)


img = Image.new("RGB", (W * K, H * K), CANVAS)
d = ImageDraw.Draw(img)

# 画布点阵（Grid 的画布就是这样）
for gx in range(8, W, 16):
    for gy in range(8, H, 16):
        r = 0.9 * K
        d.ellipse((gx * K - r, gy * K - r, gx * K + r, gy * K + r), fill=DOT)

f_name = ImageFont.truetype(FONT_BOLD, int(14.5 * K))
f_sub  = ImageFont.truetype(FONT_REG, int(11 * K))

glow = Image.new("RGBA", img.size, (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
lines = Image.new("RGBA", img.size, (0, 0, 0, 0))
ld = ImageDraw.Draw(lines)

for i, (name, sub, path) in enumerate(CARDS):
    x0 = MARGIN + i * (CARD_W + GAP)
    y0 = MARGIN
    d.rounded_rectangle((x0 * K, y0 * K, (x0 + CARD_W) * K, (y0 + CARD_H) * K),
                        radius=10 * K, fill=MODULE, outline=STROKE, width=max(1, int(1 * K)))

    wx0, wy0 = x0 + WELL_INSET, y0 + WELL_INSET
    wx1, wy1 = x0 + CARD_W - WELL_INSET, wy0 + WELL_H
    d.rounded_rectangle((wx0 * K, wy0 * K, wx1 * K, wy1 * K), radius=7 * K, fill=WELL)

    # 曲线的两个锚点电平（0.16 / 0.84）画两道淡线，看得出"往上 / 往下"拉到哪
    ix0, iy0, iw, ih = wx0 + 6, wy0 + 4, (wx1 - wx0) - 12, (wy1 - wy0) - 8
    for ly in (0.16, 0.84):
        y = (iy0 + ly * ih) * K
        d.line(((ix0 + 0.06 * iw) * K, y, (ix0 + 0.94 * iw) * K, y), fill=GUIDE, width=max(1, int(1 * K)))

    pts = [((ix0 + u * iw) * K, (iy0 + v * ih) * K) for (u, v) in sample(path)]
    stroke(gd, pts, CYAN + (150,), int(7 * K))
    stroke(ld, pts, CYAN + (255,), int(2.6 * K))

    cx = (x0 + CARD_W / 2) * K
    d.text((cx, (wy1 + 22) * K), name, font=f_name, fill=TEXT, anchor="mm")
    d.text((cx, (wy1 + 44) * K), sub, font=f_sub, fill=DIM, anchor="mm")

glow = glow.filter(ImageFilter.GaussianBlur(5 * K))
img = Image.alpha_composite(img.convert("RGBA"), glow)
img = Image.alpha_composite(img, lines).convert("RGB")
img = img.resize((W * OUT, H * OUT), Image.LANCZOS)
img.save("docs/motion-semantics.png", optimize=True)
print("docs/motion-semantics.png", img.size)
