"""
Generate macOS AppIcon.icns for GridGuard
Uses PIL/Pillow and macOS built-in iconutil
"""

import os
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw


def draw_gridguard_icon(size):
    """Draw a modern SCADA electrical grid shield icon at specified pixel size."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    pad = size * 0.06
    r = size * 0.22

    # 1. Dark Slate Rounded Rect background
    bg_box = [pad, pad, size - pad, size - pad]
    draw.rounded_rectangle(bg_box, radius=r, fill="#0f172a", outline="#0284c7", width=max(1, int(size * 0.03)))

    # 2. Glowing inner border
    inner_pad = pad + size * 0.035
    draw.rounded_rectangle([inner_pad, inner_pad, size - inner_pad, size - inner_pad],
                           radius=r * 0.85, outline="#38bdf8", width=max(1, int(size * 0.015)))

    # 3. Transmission Tower / Power Grid Motif (Cyan & Amber)
    cx = size / 2.0
    cy = size / 2.0
    s = size * 0.55  # motif scale

    # Draw transmission tower lines
    top_y = cy - s * 0.45
    bot_y = cy + s * 0.45
    w_top = s * 0.22
    w_bot = s * 0.50

    lw = max(2, int(size * 0.035))
    lw_thin = max(1, int(size * 0.02))

    # Tower main legs
    draw.line([(cx - w_top, top_y), (cx - w_bot, bot_y)], fill="#38bdf8", width=lw)
    draw.line([(cx + w_top, top_y), (cx + w_bot, bot_y)], fill="#38bdf8", width=lw)

    # Crossarms (horizontal)
    y_arm1 = cy - s * 0.18
    w_arm1 = s * 0.65
    draw.line([(cx - w_arm1, y_arm1), (cx + w_arm1, y_arm1)], fill="#0284c7", width=lw)

    y_arm2 = cy + s * 0.08
    w_arm2 = s * 0.52
    draw.line([(cx - w_arm2, y_arm2), (cx + w_arm2, y_arm2)], fill="#0284c7", width=lw)

    # Bracing cross
    draw.line([(cx - w_arm1 * 0.7, y_arm1), (cx + w_arm2 * 0.7, y_arm2)], fill="#64748b", width=lw_thin)
    draw.line([(cx + w_arm1 * 0.7, y_arm1), (cx - w_arm2 * 0.7, y_arm2)], fill="#64748b", width=lw_thin)

    # 4. Central Electric Lightning Bolt (Amber/Yellow Glow)
    bolt_pts = [
        (cx + s * 0.08, cy - s * 0.35),
        (cx - s * 0.15, cy + s * 0.02),
        (cx + s * 0.04, cy + s * 0.02),
        (cx - s * 0.08, cy + s * 0.42),
        (cx + s * 0.18, cy + s * 0.05),
        (cx - s * 0.02, cy + s * 0.05),
    ]
    draw.polygon(bolt_pts, fill="#fbbf24", outline="#f59e0b")

    # 5. Peak insulator sphere
    sp_r = size * 0.035
    draw.ellipse([cx - sp_r, top_y - sp_r, cx + sp_r, top_y + sp_r], fill="#38bdf8", outline="#ffffff", width=max(1, int(size * 0.01)))

    return img


def build_mac_icns(out_icns_path):
    out_icns = Path(out_icns_path).resolve()
    iconset_dir = out_icns.parent / "AppIcon.iconset"
    iconset_dir.mkdir(parents=True, exist_ok=True)

    sizes = [
        (16, "icon_16x16.png"),
        (32, "icon_16x16@2x.png"),
        (32, "icon_32x32.png"),
        (64, "icon_32x32@2x.png"),
        (128, "icon_128x128.png"),
        (256, "icon_128x128@2x.png"),
        (256, "icon_256x256.png"),
        (512, "icon_256x256@2x.png"),
        (512, "icon_512x512.png"),
        (1024, "icon_512x512@2x.png"),
    ]

    for s, name in sizes:
        img = draw_gridguard_icon(s)
        img.save(iconset_dir / name, "PNG")

    # Run iconutil on macOS
    cmd = ["iconutil", "-c", "icns", str(iconset_dir), "-o", str(out_icns)]
    subprocess.run(cmd, check=True)
    print(f"✓ Berhasil membuat icon macOS: {out_icns}")


if __name__ == "__main__":
    assets_dir = Path("assets")
    assets_dir.mkdir(exist_ok=True)
    build_mac_icns(assets_dir / "AppIcon.icns")
