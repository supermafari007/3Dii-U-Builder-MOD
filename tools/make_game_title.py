#!/usr/bin/env python3
"""Assembles a game's Wii U title folder (code, content, meta), for Cemu and the WUP packer. Game files are hard links, so they take no extra disk.
tools/win/build3ds.py and builder/pipeline.py call make_title() for every game. By hand:
    make_game_title.py <title folder> <game folder> <rpx> <shaders folder> <title id> <name> <product code> [system archives folder]"""
import os
import re
import shutil
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, 'phase0', 'title_code')
ARCHIVES = os.path.join(ROOT, 'games', 'system_archives')
THREEDS_TEMPLATE = os.path.join(ROOT, 'phase0', 'title_code', 'meta', 'nintendo_3ds_template_clean.png')
THREEDS_LOGO = os.path.join(ROOT, 'phase0', 'title_code', 'meta', 'nintendo_3ds_logo.png')
BACKGROUND = (0xFF, 0xF4, 0xD0)
_QT_APP = None


def link(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst):
        if os.path.samefile(src, dst):
            return
        os.remove(dst)
    os.link(src, dst)


def morton(x, y):
    r = 0
    for i in range(3):
        r |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
    return r


def icon_pixels(smdh_path):
    """the game's own 48x48 icon from its SMDH: RGB565 in 8x8 tiles, each tile in Morton order"""
    smdh = open(smdh_path, 'rb').read()
    icon = [[(0, 0, 0)] * 48 for _ in range(48)]
    for ty in range(6):
        for tx in range(6):
            for y in range(8):
                for x in range(8):
                    v = struct.unpack_from('<H', smdh, 0x24C0 + (ty * 6 + tx) * 128 + morton(x, y) * 2)[0]
                    icon[ty * 8 + y][tx * 8 + x] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    return icon


def write_tga(path, icon, w, h, bpp, scale, background):
    """the icon scaled up and centred on a plain background, in the TGA forms and sizes the Wii U Menu expects"""
    x0, y0 = (w - 48 * scale) // 2, (h - 48 * scale) // 2
    pixels = bytearray()
    for y in range(h - 1, -1, -1):  # bottom-up
        for x in range(w):
            r, g, b = background
            if scale and x0 <= x < x0 + 48 * scale and y0 <= y < y0 + 48 * scale:
                r, g, b = icon[(y - y0) // scale][(x - x0) // scale]
            pixels += bytes((b, g, r))
            if bpp == 32:
                pixels.append(255)
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, bpp, 8 if bpp == 32 else 0)
    with open(path, 'wb') as f:
        f.write(header + bytes(pixels) + TGA_FOOTER + (bytes(4) if bpp == 24 else b''))


TGA_FOOTER = bytes(8) + b'TRUEVISION-XFILE.\0'  # the console's loader wants it; Cemu doesn't care


SHARED_FONT = '0004009b00014002'  # the system archive with the font games write their text in


def unpacked_shared_font(archive):
    """The shared font out of its system archive, decompressed: 3 MB the console would otherwise unpack from 1.4 MB every time a game starts.
    None if the archive isn't there or holds no font."""
    if not os.path.isfile(archive):
        return None
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import dvlb_to_log  # its romfs listing and its LZ11
    with open(archive, 'rb') as f:
        data = f.read()
    for offset, _ in dvlb_to_log.romfs_files(data):
        font = dvlb_to_log.lz(data, offset)
        if font and font[:4] == b'CFNT':
            return font
    return None


def art_pixels(path, w, h, background):
    """A picture (cover art, say) as rows of (r, g, b), scaled to fill w x h and centre-cropped. Read with Qt, which the builder carries;
    None if it can't be read."""
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QColor, QImage, QPainter
    src = QImage(path)
    if src.isNull():
        return None
    fit = src.scaled(w, h, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
    canvas = QImage(w, h, QImage.Format_RGB888)
    canvas.fill(QColor(*background))
    painter = QPainter(canvas)
    painter.drawImage(0, 0, fit, (fit.width() - w) // 2, (fit.height() - h) // 2, w, h)
    painter.end()
    stride, data = canvas.bytesPerLine(), bytes(canvas.constBits())
    return [[tuple(data[y * stride + 3 * x:y * stride + 3 * x + 3]) for x in range(w)] for y in range(h)]


def smdh_names(smdh_path):
    """the game's long name and its publisher from its SMDH: the English ones, or the Japanese where a game has no English"""
    smdh = open(smdh_path, 'rb').read()
    for lang in (1, 0):
        at = 8 + 0x200 * lang
        text = lambda o, n: smdh[at + o:at + o + n].decode('utf-16-le', 'replace').split('\0')[0]
        name = ' '.join(text(0x80, 0x100).split())
        if name:
            return name, text(0x180, 0x80).strip()
    return '', ''


_qt = None  # the application Qt draws text with, made once


def _ensure_qt():
    global _qt, _QT_APP
    from PySide6.QtGui import QFontDatabase, QGuiApplication
    app = QGuiApplication.instance()
    if app is None:
        os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
        app = QGuiApplication([sys.argv[0]] if sys.argv else [])
    _qt = _QT_APP = app
    if not QFontDatabase.families():
        fonts_dir = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
        for fn in ('arial.ttf', 'arialbd.ttf', 'ariali.ttf', 'segoeuil.ttf', 'segoeui.ttf',
                   'segoeuib.ttf', 'segoeuii.ttf', 'seguisb.ttf', 'msgothic.ttc', 'meiryo.ttc', 'malgun.ttf'):
            fp = os.path.join(fonts_dir, fn)
            if os.path.isfile(fp):
                QFontDatabase.addApplicationFont(fp)
    return app


def console_screens(art, smdh_path, released='', case_scan=False, name_override=None, publisher_override=None):
    """The start-up screens as the Wii U's Virtual Console draws a DS game's (the user's picture of one, 2026-11-14): "Virtual Console" at the
    top left, a large faint drawing of the console behind it all on the right, and across the middle a white band holding the game's art, the
    console's name, the game's name from its SMDH, and released ("Released in Europe: 2011", say), or its publisher without one. A GameTDB cover
    (case_scan: 768x680, the case's front) gives the case without the strip with the console's name down its side, shown whole.
    All of it is drawn here; none of it is Nintendo's artwork. {'bootTvTex.tga': rows, 'bootDrcTex.tga': rows}, as art_pixels gives them, or
    None if art can't be read."""
    from PySide6.QtCore import QPointF, QRectF, Qt
    from PySide6.QtGui import QColor, QFont, QImage, QLinearGradient, QPainter, QPainterPath, QPen
    _ensure_qt()
    cover = QImage(art)
    if cover.isNull():
        return None
    if case_scan:  # a GameTDB case scan: its white strip down the right goes, and the whole height stays (a logo can sit low on it)
        cover = cover.copy(0, 0, 684, 680)
    name, publisher = smdh_names(smdh_path)
    name = name_override or name
    publisher = publisher_override if publisher_override is not None else publisher
    img = QImage(1280, 720, QImage.Format_RGB888)
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing)
    p.setRenderHint(QPainter.SmoothPixmapTransform)

    def text(x, y, w, h, px, rgb, words, weight=QFont.Normal, italic=False, spacing=0.0):
        f = QFont('Segoe UI')
        f.setPixelSize(px)
        f.setWeight(weight)
        f.setItalic(italic)
        f.setLetterSpacing(QFont.AbsoluteSpacing, spacing)
        p.setFont(f)
        p.setPen(QColor(*rgb))
        p.drawText(QRectF(x, y, w, h), Qt.AlignLeft | Qt.AlignVCenter | Qt.TextWordWrap, words)
        return p.fontMetrics().horizontalAdvance(words)

    bg = QLinearGradient(0, 0, 0, 720)
    bg.setColorAt(0, QColor(247, 247, 247))
    bg.setColorAt(1, QColor(212, 212, 212))
    p.fillRect(0, 0, 1280, 720, bg)
    # an open 3DS in faint lines, behind everything on the right and running off the edge: lid, screens, hinge, base, controls, stylus
    p.save()
    p.translate(812, 8)
    p.scale(1.3, 1.3)
    p.setPen(QPen(QColor(192, 192, 192), 1.6))
    p.setBrush(Qt.NoBrush)
    for x, y, w, h, r in ((0, 0, 400, 240, 22), (50, 26, 300, 180, 4), (12, 244, 376, 16, 8), (0, 262, 400, 240, 22), (100, 296, 200, 150, 4)):
        p.drawRoundedRect(QRectF(x, y, w, h), r, r)
    p.drawEllipse(QPointF(45, 318), 25, 25)
    cross = QPainterPath()
    for x, y, w, h in ((34, 364, 22, 62), (14, 384, 62, 22)):
        cross.addRect(QRectF(x, y, w, h))
    p.drawPath(cross.simplified())
    for dx, dy in ((0, -24), (24, 0), (0, 24), (-24, 0)):
        p.drawEllipse(QPointF(355 + dx, 335 + dy), 11, 11)
    for k in range(3):
        p.drawRoundedRect(QRectF(130 + k * 52, 466, 36, 9), 4, 4)
    p.drawLine(QPointF(330, 482), QPointF(395, 250))
    p.restore()
    # the band across the middle, on a soft shadow
    for k in range(14):
        p.fillRect(QRectF(0, 564 + k, 1280, 1), QColor(0, 0, 0, 34 - 34 * k // 14))
    p.fillRect(QRectF(0, 180, 1280, 384), QColor(255, 255, 255, 236))
    p.fillRect(QRectF(0, 180, 1280, 2), QColor(255, 255, 255))
    # the game's art, filling its frame, vertically centred in the band
    if case_scan:  # shown whole: its own shape, inside the band with a margin
        scale = min(344 / cover.width(), 344 / cover.height())
        frame = QRectF(72, 372 - cover.height() * scale / 2, cover.width() * scale, cover.height() * scale)
    else:
        height = max(200.0, min(300.0, 344 * cover.height() / cover.width()))  # the art's own shape, within reason
        frame = QRectF(72, 372 - height / 2, 344, height)
    fit = cover.scaled(int(frame.width()), int(frame.height()), Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
    clip = QPainterPath()
    clip.addRoundedRect(frame, 8, 8)
    p.save()
    p.setClipPath(clip)
    p.drawImage(frame, fit, QRectF((fit.width() - frame.width()) / 2, 0, frame.width(), frame.height()))
    p.restore()
    p.setPen(QPen(QColor(200, 200, 200), 1.5))
    p.drawPath(clip)
    # the console's name, the game's, and when it came out or who made it
    shift = text(552, 262, 300, 70, 30, (88, 88, 88), 'NINTENDO', QFont.Light, spacing=3.0)
    text(552 + shift + 16, 250, 300, 80, 56, (58, 58, 58), '3DS', QFont.Bold)
    text(552, 344, 690, 104, 40, (52, 52, 52), name)
    text(552, 456, 690, 48, 26, (84, 84, 84), released or publisher)
    text(52, 48, 600, 70, 46, (156, 156, 156), 'Virtual Console', italic=True)
    p.end()

    def rows(image):
        stride, data = image.bytesPerLine(), bytes(image.constBits())
        return [[tuple(data[y * stride + 3 * x:y * stride + 3 * x + 3]) for x in range(image.width())] for y in range(image.height())]
    return {'bootTvTex.tga': rows(img), 'bootDrcTex.tga': rows(img.scaled(854, 480, Qt.IgnoreAspectRatio, Qt.SmoothTransformation))}


def minimal_3ds_screens(art, name, publisher='', fallback_icon=None, released=''):
    """Compose the supplied Nintendo 3DS backdrop as a Virtual Console information card."""
    from PySide6.QtCore import Qt, QPointF, QRectF
    from PySide6.QtGui import QColor, QFont, QFontMetricsF, QImage, QPainter, QPainterPath, QPen
    _ensure_qt()
    template = QImage(THREEDS_TEMPLATE)
    if template.isNull():
        return None
    cover = QImage(art) if art else QImage()
    fallback_cover = False
    if cover.isNull() and fallback_icon and os.path.isfile(fallback_icon):
        pixels = icon_pixels(fallback_icon)
        raw = bytes(channel for row in pixels for pixel in row for channel in pixel)
        cover = QImage(raw, 48, 48, 48 * 3, QImage.Format_RGB888).copy()
        fallback_cover = True
    output = {}
    for filename, width, height in (('bootTvTex.tga', 1280, 720), ('bootDrcTex.tga', 854, 480)):
        image = QImage(width, height, QImage.Format_RGB888)
        image.fill(QColor('#f4f4f4'))
        painter = QPainter(image)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setRenderHint(QPainter.SmoothPixmapTransform)
        painter.drawImage(image.rect(), template)

        # Match StartupPreview.qml: the template already contains the clean white card and its shadow.

        # The artwork fills the template frame edge to edge; game information sits in the right column.
        inset = QRectF(width * 0.072, height * 0.289, width * 0.348, height * 0.505)
        painter.setPen(Qt.NoPen)
        painter.setBrush(QColor(242, 244, 247))
        painter.drawRoundedRect(inset, width * 0.006, width * 0.006)
        if not cover.isNull():
            clip = QPainterPath()
            painter.save()
            if fallback_cover:
                # Keep the game's small square icon proportional when there is no user photo.
                fit = cover.scaled(int(inset.width()), int(inset.height()), Qt.KeepAspectRatio, Qt.SmoothTransformation)
                placed = QRectF(inset.center().x() - fit.width() / 2, inset.center().y() - fit.height() / 2,
                                fit.width(), fit.height())
                clip.addRoundedRect(placed, width * 0.006, width * 0.006)
                painter.setClipPath(clip)
                painter.drawImage(placed, fit)
            else:
                # A supplied photo uses the entire template frame; scale it without cropping any edges.
                fit = cover.scaled(int(inset.width()), int(inset.height()), Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
                clip.addRoundedRect(inset, width * 0.006, width * 0.006)
                painter.setClipPath(clip)
                painter.drawImage(inset, fit)
            painter.restore()

        info_x = width * 0.455
        info_w = width * 0.465
        logo = QImage(THREEDS_LOGO)
        if not logo.isNull():
            logo_box = QRectF(info_x, height * 0.295, width * 0.255, height * 0.068)
            logo_fit = logo.scaled(int(logo_box.width()), int(logo_box.height()), Qt.KeepAspectRatio, Qt.SmoothTransformation)
            painter.drawImage(QRectF(logo_box.x(), logo_box.y(), logo_fit.width(), logo_fit.height()), logo_fit)

        title_box = QRectF(info_x, height * 0.385, info_w, height * 0.115)
        title_font = QFont('Arial')
        title_font.setPixelSize(max(18, int(height * 0.052)))
        title_font.setWeight(QFont.Medium)
        while title_font.pixelSize() > max(16, int(height * 0.038)):
            bounds = QFontMetricsF(title_font).boundingRect(title_box, Qt.AlignLeft | Qt.AlignVCenter | Qt.TextWordWrap, name)
            if bounds.height() <= title_box.height():
                break
            title_font.setPixelSize(title_font.pixelSize() - 1)
        painter.setFont(title_font)
        painter.setPen(QColor('#202124'))
        painter.drawText(title_box, Qt.AlignLeft | Qt.AlignVCenter | Qt.TextWordWrap, name)

        detail_font = QFont('Arial')
        detail_font.setPixelSize(max(13, int(height * 0.032)))
        detail_font.setWeight(QFont.Normal)
        painter.setFont(detail_font)
        if publisher:
            painter.setPen(QColor('#3b3d40'))
            painter.drawText(QRectF(info_x, height * 0.525, info_w, height * 0.06),
                             Qt.AlignLeft | Qt.AlignVCenter | Qt.TextWordWrap, publisher)
        if released:
            painter.setPen(QColor('#3b3d40'))
            painter.drawText(QRectF(info_x, height * 0.600, info_w, height * 0.06),
                             Qt.AlignLeft | Qt.AlignVCenter | Qt.TextWordWrap, released)
        painter.end()
        output[filename] = RGBRows(image)
    return output


class RGBRows:
    """A small, indexable RGB image view. Avoids allocating a Python tuple for every pixel."""
    def __init__(self, image):
        self.image = image
        self.width, self.height = image.width(), image.height()
        self.stride = image.bytesPerLine()
        self.data = bytes(image.constBits())
        self._rows = [RGBRow(self, y) for y in range(self.height)]

    def __len__(self):
        return self.height

    def __getitem__(self, y):
        if isinstance(y, slice):
            return [self[row] for row in range(*y.indices(self.height))]
        if y < 0:
            y += self.height
        if not 0 <= y < self.height:
            raise IndexError(y)
        return self._rows[y]


class RGBRow:
    def __init__(self, rows, y):
        self.rows, self.y = rows, y
        self._pixels = None

    def __len__(self):
        return self.rows.width

    def __getitem__(self, x):
        if isinstance(x, slice):
            return [self[col] for col in range(*x.indices(self.rows.width))]
        if x < 0:
            x += self.rows.width
        if not 0 <= x < self.rows.width:
            raise IndexError(x)
        if self._pixels is None:
            at = self.y * self.rows.stride
            self._pixels = [tuple(self.rows.data[at + col * 3:at + col * 3 + 3]) for col in range(self.rows.width)]
        return self._pixels[x]


def write_art_tga(path, rows, bpp):
    """rows of (r, g, b), top first, as the TGA the Wii U Menu expects (write_tga's layout)"""
    if isinstance(rows, RGBRows) and bpp == 24:
        from PySide6.QtGui import QImage
        image = rows.image.convertToFormat(QImage.Format_BGR888)
        stride, data = image.bytesPerLine(), bytes(image.constBits())
        row_bytes = rows.width * 3
        pixels = b''.join(data[y * stride:y * stride + row_bytes] for y in range(rows.height - 1, -1, -1))
        header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, rows.width, rows.height, bpp, 0)
        with open(path, 'wb') as f:
            f.write(header + pixels + TGA_FOOTER + bytes(4))
        return
    h, w = len(rows), len(rows[0])
    pixels = bytearray()
    for row in reversed(rows):  # bottom-up
        for r, g, b in row:
            pixels += bytes((b, g, r, 255) if bpp == 32 else (b, g, r))
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, bpp, 8 if bpp == 32 else 0)
    with open(path, 'wb') as f:
        f.write(header + bytes(pixels) + TGA_FOOTER + (bytes(4) if bpp == 24 else b''))


PAD_W, PAD_H = 854, 480  # the GamePad's screen


def bezel_pixels(path):
    """A picture for the GamePad where it shows no screen (gx2_backend.cpp, pad_bezel), filling 854x480 (scaled to cover it, centred, the
    overflow cut off) over black, as RGBA bytes row by row; None if it can't be read. Read with Qt, which the builder carries."""
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QColor, QImage, QPainter
    src = QImage(path)
    if src.isNull():
        return None
    cover = src.scaled(PAD_W, PAD_H, Qt.KeepAspectRatioByExpanding, Qt.SmoothTransformation)
    canvas = QImage(PAD_W, PAD_H, QImage.Format_RGBA8888)
    canvas.fill(QColor(0, 0, 0))  # what a picture leaves see-through is black, as without one
    painter = QPainter(canvas)
    painter.drawImage(0, 0, cover, (cover.width() - PAD_W) // 2, (cover.height() - PAD_H) // 2, PAD_W, PAD_H)
    painter.end()
    stride, data = canvas.bytesPerLine(), bytes(canvas.constBits())
    return b''.join(data[y * stride:y * stride + PAD_W * 4] for y in range(PAD_H))


def make_title(title, game, rpx, shaders, title_id, name, product, archives=ARCHIVES, art=None, icon_art=None, style='picture', released='', case_scan=False,
               pad_bezel=None, pad_art=None, boot_sound=None, publisher=None, disable_runtime_shaders=False):
    """title: the folder to make. game: the unpacked game (exheader.bin, code.bin, romfs.bin, exefs/icon.bin). rpx and shaders: what the build made.
    art: a picture for the start-up screens and the Wii U Menu icon, in place of the game's own 48x48 icon scaled up. icon_art: another
    picture for the icon alone (a square one, where art is a wide banner). style: 'picture' shows art as it is on the start-up screens,
    'console' uses the bundled minimal 3DS template at TV and GamePad resolution with the game's name and publisher; case_scan is retained for compatibility.
    pad_art: a picture of its own for the GamePad's start-up screen, shown as it is. boot_sound: the game's own start-up sound (a .btsnd,
    tools/make_boot_sound.py), in place of the one this PC has for every game."""
    rpx_name = os.path.basename(rpx)
    shutil.rmtree(os.path.join(title, 'content'), ignore_errors=True)
    for d in ('code', 'content', 'meta'):
        os.makedirs(os.path.join(title, d), exist_ok=True)

    for xml in ('app.xml', 'cos.xml'):
        s = open(os.path.join(TEMPLATE, 'code', xml), encoding='utf-8', newline='').read()
        s = s.replace('0005000010334455', title_id).replace('bench.rpx', rpx_name).replace('>0A000000<', '>0E000000<')
        # as retail titles have it: Minecraft on the user's console, and a working inject
        s = s.replace('<overlay_arena type="hexBinary" length="4">00000001<', '<overlay_arena type="hexBinary" length="4">00000000<')
        open(os.path.join(title, 'code', xml), 'w', encoding='utf-8', newline='').write(s)
    shutil.copyfile(rpx, os.path.join(title, 'code', rpx_name))

    # art for the Wii U Menu, from the game's own icon
    icon = icon_pixels(os.path.join(game, 'exefs', 'icon.bin'))
    meta_dir = os.path.join(title, 'meta')
    write_tga(os.path.join(meta_dir, 'iconTex.tga'), icon, 128, 128, 32, 2, BACKGROUND)
    write_tga(os.path.join(meta_dir, 'bootLogoTex.tga'), icon, 170, 42, 32, 0, (0, 0, 0))
    write_tga(os.path.join(meta_dir, 'bootDrcTex.tga'), icon, 854, 480, 24, 6, BACKGROUND)
    write_tga(os.path.join(meta_dir, 'bootTvTex.tga'), icon, 1280, 720, 24, 10, BACKGROUND)
    screens = minimal_3ds_screens(art, name, publisher, os.path.join(game, 'exefs', 'icon.bin'), released) if style == 'console' else None
    for tga, w, h, bpp, picture in (('iconTex.tga', 128, 128, 32, icon_art or art), ('bootDrcTex.tga', 854, 480, 24, art), ('bootTvTex.tga', 1280, 720, 24, art)):
        rows = screens[tga] if screens and tga in screens else art_pixels(picture, w, h, (0, 0, 0)) if picture else None
        if rows:
            write_art_tga(os.path.join(meta_dir, tga), rows, bpp)
        elif picture:
            print(f'the picture {picture} could not be read: the game\'s own icon is used for {tga}')
    pad = art_pixels(pad_art, PAD_W, PAD_H, (0, 0, 0)) if pad_art else None
    if pad:  # the GamePad's start-up screen of its own, over the one made above
        write_art_tga(os.path.join(meta_dir, 'bootDrcTex.tga'), pad, 24)
    elif pad_art:
        print(f"the picture {pad_art} could not be read: the GamePad shows the TV's start-up screen")
    # the sound while the game starts: its own, or the one this PC has for every game (tools/make_boot_sound.py); a title without either starts silently
    sound, every_game = None, os.path.join(os.path.dirname(os.path.abspath(archives)), 'boot_sound.btsnd')
    if boot_sound:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import make_boot_sound  # checks a .btsnd, and makes one of a WAV
        try:
            sound = make_boot_sound.boot_sound(boot_sound)
        except (OSError, ValueError, EOFError) as e:
            print(f"the sound {boot_sound} can't be used ({e}): the one for every game is used")
    if sound is None:
        # A user-selected common sound wins; otherwise use the shipped Virtual Console sound.
        for common in (every_game, os.path.join(ROOT, 'phase0', 'title_code', 'meta', 'bootSound.btsnd')):
            if os.path.isfile(common):
                sound = open(common, 'rb').read()
                break
    if sound is not None:
        with open(os.path.join(meta_dir, 'bootSound.btsnd'), 'wb') as f:
            f.write(sound)
    elif os.path.exists(os.path.join(meta_dir, 'bootSound.btsnd')):
        os.remove(os.path.join(meta_dir, 'bootSound.btsnd'))
    shutil.copyfile(os.path.join(TEMPLATE, 'meta', 'bootMovie.h264'), os.path.join(meta_dir, 'bootMovie.h264'))
    meta = open(os.path.join(TEMPLATE, 'meta', 'meta.xml'), encoding='utf-8', newline='').read()
    xml_name = name.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')
    meta = meta.replace('0005000010334455', title_id).replace('3DS Recomp Code Test', xml_name).replace('3DS-WiiU Recomp', 'Nintendo (recompiled)')
    meta = re.sub(r'WUP-N-[A-Z0-9]+', lambda m: product, meta)
    # the room for saves: the game's in the player's own storage, /vol/save/<persistent id>, which the system lists as save data and keeps when the software is deleted, and
    # the logs' and test runs' in /vol/save/common. With none, the console has nowhere to put them, which Cemu doesn't care about.
    meta = meta.replace('<common_save_size type="hexBinary" length="8">0000000000000000<', '<common_save_size type="hexBinary" length="8">0000000004000000<')
    meta = meta.replace('<account_save_size type="hexBinary" length="8">0000000000000000<', '<account_save_size type="hexBinary" length="8">0000000004000000<')
    assert '<common_save_size type="hexBinary" length="8">0000000004000000<' in meta and '<account_save_size type="hexBinary" length="8">0000000004000000<' in meta
    open(os.path.join(meta_dir, 'meta.xml'), 'w', encoding='utf-8', newline='').write(meta)

    content = os.path.join(title, 'content')
    for f in ('exheader.bin', 'code.bin', 'romfs.bin'):
        link(os.path.join(game, f), os.path.join(content, f))
    link(os.path.join(game, 'exefs', 'icon.bin'), os.path.join(content, 'exefs_icon.bin'))
    for f in os.listdir(archives):
        link(os.path.join(archives, f), os.path.join(content, f))
    font = unpacked_shared_font(os.path.join(archives, SHARED_FONT + '.romfs'))
    if font:  # the console reads it as it is, with nothing to search or decompress while the game starts (services/misc.cpp)
        with open(os.path.join(content, 'shared_font.bcfnt'), 'wb') as f:
            f.write(font)
    for f in ('shaders.gsh', 'shaders.idx'):
        shutil.copyfile(os.path.join(shaders, f), os.path.join(content, f))
    # This title's Wii U DSI report points into CafeGLSL's NIR optimizer. Its
    # generated build avoids that runtime compiler path; missing shaders use fallback.
    if title_id.upper() in ('000500003D000540', '000500003D00053F', '000500003D00053E'):
        open(os.path.join(content, 'noglslcompile'), 'wb').close()
    if disable_runtime_shaders:
        # CafeGLSL's runtime compiler can be disabled for a title diagnosed with
        # an on-console compiler fault; missing shaders then use the CPU fallback.
        open(os.path.join(content, 'noglslcompile'), 'wb').close()
    bezel = bezel_pixels(pad_bezel) if pad_bezel else None
    if bezel:  # the GamePad's picture where no screen is: the console reads its bytes as they are
        with open(os.path.join(content, 'pad_bezel.rgba'), 'wb') as f:
            f.write(bezel)
    elif pad_bezel:
        print(f'the picture {pad_bezel} could not be read: the GamePad stays black beside the screens')
    print('title folder:', title)


if __name__ == '__main__':
    if len(sys.argv) not in (8, 9):
        sys.exit(__doc__)
    make_title(*sys.argv[1:])
