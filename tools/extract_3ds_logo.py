#!/usr/bin/env python3
"""Extract a transparent Nintendo 3DS wordmark from the supplied backdrop."""
import os

from PySide6.QtCore import QRect
from PySide6.QtGui import QColor, QImage

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
source = QImage(os.path.join(root, 'phase0', 'title_code', 'meta', 'nintendo_3ds_template.png'))
if source.isNull():
    raise SystemExit('Nintendo 3DS template is missing')

# Reconstruct the pale header gradient across the old logo area, blending into untouched pixels
# at both sides. This removes the mark without leaving the rectangular patch from the prior pass.
clean = source.copy().convertToFormat(QImage.Format_RGB32)
x0, x1 = round(source.width() * .635), round(source.width() * .98)
y0, y1 = round(source.height() * .075), round(source.height() * .225)
for y in range(y0, y1):
    left, right = clean.pixelColor(x0, y), clean.pixelColor(x1, y)
    span = x1 - x0
    for x in range(x0, x1 + 1):
        t = (x - x0) / span
        clean.setPixelColor(x, y, QColor(
            round(left.red() * (1 - t) + right.red() * t),
            round(left.green() * (1 - t) + right.green() * t),
            round(left.blue() * (1 - t) + right.blue() * t)))
clean_path = os.path.join(root, 'phase0', 'title_code', 'meta', 'nintendo_3ds_template_clean.png')
if not clean.save(clean_path):
    raise SystemExit('Could not save the cleaned template')

# Tight crop around the printed header logo; the near-white backdrop becomes transparent.
rect = QRect(round(source.width() * .643), round(source.height() * .108),
             round(source.width() * .328), round(source.height() * .105))
logo = source.copy(rect).convertToFormat(QImage.Format_ARGB32)
for y in range(logo.height()):
    for x in range(logo.width()):
        color = logo.pixelColor(x, y)
        alpha = max(0, min(255, (235 - min(color.red(), color.green(), color.blue())) * 4))
        color.setAlpha(alpha)
        logo.setPixelColor(x, y, color)

# Trim transparent padding so the left edge of the visible N aligns exactly with the text column.
visible = [(x, y) for y in range(logo.height()) for x in range(logo.width()) if logo.pixelColor(x, y).alpha() > 24]
left = min(x for x, _ in visible)
top = min(y for _, y in visible)
right = max(x for x, _ in visible)
bottom = max(y for _, y in visible)
logo = logo.copy(QRect(left, top, right - left + 1, bottom - top + 1))

output = os.path.join(root, 'phase0', 'title_code', 'meta', 'nintendo_3ds_logo.png')
if not logo.save(output):
    raise SystemExit('Could not save the extracted logo')
print(output)
print(clean_path)
