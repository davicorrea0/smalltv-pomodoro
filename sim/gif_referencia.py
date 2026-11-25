#!/usr/bin/env python3
"""Referência GIF89a independente e comparação do render do firmware."""
import argparse
import glob
import os
import struct
import zlib

try:
    from PIL import Image
except ImportError:
    Image = None

W = H = 240


def u16(data, pos):
    return data[pos] | (data[pos + 1] << 8)


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def palette(data, pos, colors):
    out = []
    for i in range(colors):
        out.append(rgb565(data[pos + i * 3:pos + i * 3 + 3]))
    return out, pos + colors * 3


def subblocks(data, pos):
    out = bytearray()
    while True:
        n = data[pos]
        pos += 1
        if not n:
            return bytes(out), pos
        out.extend(data[pos:pos + n])
        pos += n


def lzw(payload, minimum, expected):
    clear, end = 1 << minimum, (1 << minimum) + 1
    table = [bytes((i,)) for i in range(clear)] + [None, None]
    size, bit, previous = minimum + 1, 0, None
    output = bytearray()

    def code_at(offset, width):
        value = 0
        for n in range(width):
            p = offset + n
            if p // 8 < len(payload) and payload[p // 8] & (1 << (p & 7)):
                value |= 1 << n
        return value

    while bit + size <= len(payload) * 8:
        code = code_at(bit, size)
        bit += size
        if code == clear:
            table = [bytes((i,)) for i in range(clear)] + [None, None]
            size, previous = minimum + 1, None
            continue
        if code == end:
            break
        if code < len(table) and table[code] is not None:
            entry = table[code]
        elif code == len(table) and previous is not None:
            entry = previous + previous[:1]
        else:
            raise ValueError("código LZW inválido")
        output.extend(entry)
        if previous is not None and len(table) < 4096:
            table.append(previous + entry[:1])
            if len(table) == (1 << size) and size < 12:
                size += 1
        previous = entry
        if len(output) >= expected:
            break
    if len(output) != expected:
        raise ValueError("LZW produziu %d pixels; esperado %d" % (len(output), expected))
    return bytes(output)


def parse_gif(path):
    data = open(path, "rb").read()
    if data[:6] not in (b"GIF87a", b"GIF89a"):
        raise ValueError("cabeçalho GIF inválido")
    width, height, packed, bg = u16(data, 6), u16(data, 8), data[10], data[11]
    pos = 13
    global_pal = []
    if packed & 0x80:
        global_pal, pos = palette(data, pos, 2 << (packed & 7))
    gce = (0, None)
    frames = []
    while pos < len(data):
        marker = data[pos]
        pos += 1
        if marker == 0x3B:
            break
        if marker == 0x21:
            label = data[pos]
            pos += 1
            if label == 0xF9:
                if data[pos] != 4:
                    raise ValueError("GCE inválido")
                flags, transparent = data[pos + 1], data[pos + 4]
                gce = ((flags >> 2) & 7, transparent if flags & 1 else None)
                pos += 6
            else:
                _, pos = subblocks(data, pos)
            continue
        if marker != 0x2C:
            raise ValueError("bloco GIF desconhecido 0x%02x" % marker)
        x, y, fw, fh, image_flags = u16(data, pos), u16(data, pos + 2), u16(data, pos + 4), u16(data, pos + 6), data[pos + 8]
        pos += 9
        pal = global_pal
        if image_flags & 0x80:
            pal, pos = palette(data, pos, 2 << (image_flags & 7))
        minimum = data[pos]
        payload, pos = subblocks(data, pos + 1)
        pixels = lzw(payload, minimum, fw * fh)
        if image_flags & 0x40:
            rows = list(range(0, fh, 8)) + list(range(4, fh, 8)) + list(range(2, fh, 4)) + list(range(1, fh, 2))
            ordered = bytearray(fw * fh)
            for source, target in enumerate(rows):
                ordered[target * fw:(target + 1) * fw] = pixels[source * fw:(source + 1) * fw]
            pixels = bytes(ordered)
        frames.append({"x": x, "y": y, "w": fw, "h": fh, "palette": pal,
                       "pixels": pixels, "disposal": gce[0], "transparent": gce[1]})
        gce = (0, None)
    return width, height, global_pal, bg, frames


def write_raw(path, pixels):
    with open(path, "wb") as out:
        out.write(struct.pack("<%dH" % len(pixels), *pixels))


def compose(path, out_root):
    width, height, global_pal, bg_index, frames = parse_gif(path)
    # A tela é limpa para preto antes de abrir o GIF.
    canvas = [0] * (width * height)
    background = 0  # como os navegadores: disposal 2 volta a "nada" (preto na tela do relógio)
    previous = None
    restore = None
    name = os.path.splitext(os.path.basename(path))[0]
    out_dir = os.path.join(out_root, name)
    os.makedirs(out_dir, exist_ok=True)
    result = []
    for number, frame in enumerate(frames):
        if previous:
            if previous["disposal"] == 2:
                for yy in range(previous["y"], previous["y"] + previous["h"]):
                    base = yy * width + previous["x"]
                    canvas[base:base + previous["w"]] = [background] * previous["w"]
            elif previous["disposal"] == 3 and restore is not None:
                canvas = restore
        restore = list(canvas) if frame["disposal"] == 3 else None
        pal, tr = frame["palette"], frame["transparent"]
        for yy in range(frame["h"]):
            for xx in range(frame["w"]):
                index = frame["pixels"][yy * frame["w"] + xx]
                if tr is None or index != tr:
                    canvas[(frame["y"] + yy) * width + frame["x"] + xx] = pal[index]
        screen = [0] * (W * H)
        off_x, off_y = max(0, (W - width) // 2), max(0, (H - height) // 2)
        copy_w, copy_h = min(width, W - off_x), min(height, H - off_y)
        for yy in range(copy_h):
            screen[(off_y + yy) * W + off_x:(off_y + yy) * W + off_x + copy_w] = canvas[yy * width:yy * width + copy_w]
        write_raw(os.path.join(out_dir, "quadro-%04d.raw" % number), screen)
        result.append(screen)
        previous = frame
    return result


def to_rgb(value):
    r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def png_fallback(path, width, height, rgb):
    raw = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value) & 0xffffffff)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def comparison_png(path, firmware, reference):
    diff = []
    for a, b in zip(firmware, reference):
        if a == b:
            diff.append((0, 0, 0))
        else:
            ar, ag, ab = to_rgb(a); br, bg, bb = to_rgb(b)
            diff.append((min(255, abs(ar - br) * 3 + 64), min(255, abs(ag - bg) * 3), min(255, abs(ab - bb) * 3 + 64)))
    rgb = bytearray()
    left = [to_rgb(v) for v in firmware]
    right = [to_rgb(v) for v in reference]
    for y in range(H):
        for group in (left, right, diff):
            for color in group[y * W:(y + 1) * W]:
                rgb.extend(color)
    if Image:
        Image.frombytes("RGB", (W * 3, H), bytes(rgb)).save(path)
    else:
        png_fallback(path, W * 3, H, bytes(rgb))


def read_raw(path):
    data = open(path, "rb").read()
    if len(data) != W * H * 2:
        raise ValueError("tamanho RAW inválido: " + path)
    return list(struct.unpack("<%dH" % (W * H), data))


def compare(files, reference_root, firmware_root, out_root):
    rows = []
    os.makedirs(out_root, exist_ok=True)
    for gif in files:
        stem, filename = os.path.splitext(os.path.basename(gif))[0], os.path.basename(gif)
        refs = sorted(glob.glob(os.path.join(reference_root, stem, "*.raw")))
        firm = sorted(glob.glob(os.path.join(firmware_root, stem, "*.raw")))
        if len(refs) != len(firm):
            raise ValueError("%s: referência=%d firmware=%d quadros" % (filename, len(refs), len(firm)))
        ranked = []
        for number, (ref_path, fw_path) in enumerate(zip(refs, firm)):
            reference, firmware = read_raw(ref_path), read_raw(fw_path)
            coords = [(i % W, i // W) for i, pair in enumerate(zip(firmware, reference)) if pair[0] != pair[1]]
            bbox = "-" if not coords else "%d,%d-%d,%d" % (min(x for x, _ in coords), min(y for _, y in coords), max(x for x, _ in coords), max(y for _, y in coords))
            rows.append((filename, number, len(coords), bbox))
            ranked.append((len(coords), number, firmware, reference))
        for _, number, firmware, reference in sorted(ranked, reverse=True)[:3]:
            comparison_png(os.path.join(out_root, "%s-q%04d.png" % (stem, number)), firmware, reference)
    with open(os.path.join(out_root, "diferencas.tsv"), "w", encoding="utf-8", newline="\n") as out:
        out.write("arquivo\tquadro\tpixels_diferentes\tcaixa\n")
        for row in rows:
            out.write("%s\t%d\t%d\t%s\n" % row)
    return sum(row[2] for row in rows), rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gifs", default="sim/gif-album/*.gif")
    parser.add_argument("--out", default="pesquisas/gif-qualidade")
    parser.add_argument("--firmware", default="pesquisas/gif-qualidade/firmware")
    parser.add_argument("--reference-only", action="store_true")
    args = parser.parse_args()
    files = sorted(glob.glob(args.gifs))
    if not files:
        raise SystemExit("nenhum GIF encontrado")
    reference_root = os.path.join(args.out, "referencia")
    for path in files:
        compose(path, reference_root)
    if args.reference_only:
        return
    differences, rows = compare(files, reference_root, args.firmware, os.path.join(args.out, "comparacoes"))
    print("render dourado: %d quadros, %d pixels diferentes%s" %
          (len(rows), differences, " (PNG por fallback; Pillow ausente)" if Image is None else ""))
    if differences:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
