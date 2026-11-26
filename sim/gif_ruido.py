#!/usr/bin/env python3
"""Mede ruído nos quadros compostos e gera recortes de conferência."""
import argparse
import glob
import os

from gif_referencia import H, W, png_fallback, read_raw, to_rgb


def delta(a, b):
    return max(abs(a[i] - b[i]) for i in range(3))


def bright(rgb):
    return min(rgb) >= 200


def masks(frame, previous, following, threshold):
    rgb = [to_rgb(value) for value in frame]
    near = [0 < max(color) <= threshold for color in rgb]
    isolated = [False] * len(frame)
    line_h = [False] * len(frame)
    line_v = [False] * len(frame)
    light = [bright(color) for color in rgb]
    neighbor_light = []
    if previous is not None:
        neighbor_light.append([bright(to_rgb(value)) for value in previous])
    if following is not None:
        neighbor_light.append([bright(to_rgb(value)) for value in following])
    for y in range(1, H - 1):
        for x in range(1, W - 1):
            i = y * W + x
            c, left, right = rgb[i], rgb[i - 1], rgb[i + 1]
            up, down = rgb[i - W], rgb[i + W]
            isolated[i] = (delta(c, left) >= 64 and delta(c, right) >= 64 and delta(left, right) <= 16 and
                           delta(c, up) >= 64 and delta(c, down) >= 64 and delta(up, down) <= 16)
            transient = bool(neighbor_light) and all(not other[i] for other in neighbor_light)
            if transient and light[i]:
                line_h[i] = light[i - 1] and light[i + 1] and not light[i - W] and not light[i + W]
                line_v[i] = light[i - W] and light[i + W] and not light[i - 1] and not light[i + 1]
    lines = keep_runs(line_h, horizontal=True)
    vertical = keep_runs(line_v, horizontal=False)
    lines = [a or b for a, b in zip(lines, vertical)]
    return near, isolated, lines


def keep_runs(mask, horizontal):
    kept = [False] * len(mask)
    outer, inner = (H, W) if horizontal else (W, H)
    for a in range(outer):
        b = 0
        while b < inner:
            index = a * W + b if horizontal else b * W + a
            if not mask[index]:
                b += 1
                continue
            start = b
            while b < inner:
                index = a * W + b if horizontal else b * W + a
                if not mask[index]:
                    break
                b += 1
            if b - start >= 3:
                for p in range(start, b):
                    index = a * W + p if horizontal else p * W + a
                    kept[index] = True
    return kept


def densest(mask, size=48):
    prefix = [[0] * (W + 1) for _ in range(H + 1)]
    for y in range(H):
        running = 0
        for x in range(W):
            running += int(mask[y * W + x])
            prefix[y + 1][x + 1] = prefix[y][x + 1] + running
    best = (-1, 0, 0)
    for y in range(H - size + 1):
        for x in range(W - size + 1):
            count = prefix[y + size][x + size] - prefix[y][x + size] - prefix[y + size][x] + prefix[y][x]
            if count > best[0]:
                best = (count, x, y)
    return best[1], best[2], size


def save_crop(path, frame, mask):
    x0, y0, size = densest(mask)
    rgb = []
    for y in range(y0, y0 + size):
        row = [to_rgb(frame[y * W + x]) for x in range(x0, x0 + size)]
        expanded = []
        for color in row:
            expanded.extend([color] * 4)
        for _ in range(4):
            rgb.extend(expanded)
    raw = bytes(channel for color in rgb for channel in color)
    png_fallback(path, size * 4, size * 4, raw)


def save_panel(path, frames):
    raw = bytearray()
    converted = [[to_rgb(value) for value in frame] for frame in frames]
    for y in range(H):
        for frame in converted:
            for color in frame[y * W:(y + 1) * W]:
                raw.extend(color)
    png_fallback(path, W * len(frames), H, bytes(raw))


def load_frames(root, stem):
    return [read_raw(path) for path in sorted(glob.glob(os.path.join(root, stem, "*.raw")))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", required=True)
    parser.add_argument("--sem-limpeza", required=True)
    parser.add_argument("--so-paleta", required=True)
    parser.add_argument("--com-limpeza", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--limiar", type=int, default=12)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    stems = sorted(os.path.basename(path) for path in glob.glob(os.path.join(args.original, "*")) if os.path.isdir(path))
    summaries = []
    details = []
    for stem in stems:
        original = load_frames(args.original, stem)
        without = load_frames(args.sem_limpeza, stem)
        palette_only = load_frames(args.so_paleta, stem)
        cleaned = load_frames(args.com_limpeza, stem)
        if not (len(original) == len(without) == len(palette_only) == len(cleaned)):
            raise ValueError("quantidade de quadros diferente em " + stem)
        totals = [0] * 7
        ranked = []
        category_best = {"quase-pretos": (-1, None, None), "isolados": (-1, None, None), "linhas-claras": (-1, None, None)}
        for number, frame in enumerate(original):
            previous = original[number - 1] if number else None
            following = original[number + 1] if number + 1 < len(original) else None
            near, isolated, lines = masks(frame, previous, following, args.limiar)
            counts = [sum(near), sum(isolated), sum(lines)]
            pal_previous = palette_only[number - 1] if number else None
            pal_following = palette_only[number + 1] if number + 1 < len(palette_only) else None
            _, isolated_after, lines_after = masks(palette_only[number], pal_previous, pal_following, args.limiar)
            changed_palette = sum(a != b for a, b in zip(frame, palette_only[number]))
            changed_filter = sum(a != b for a, b in zip(palette_only[number], cleaned[number]))
            values = counts + [sum(isolated_after), sum(lines_after), changed_palette, changed_filter]
            totals = [totals[i] + value for i, value in enumerate(values)]
            details.append((stem + ".gif", number, *values))
            ranked.append((changed_palette + changed_filter + counts[1] + counts[2], number))
            for label, count, mask in zip(category_best, counts, (near, isolated, lines)):
                if count > category_best[label][0]:
                    category_best[label] = (count, number, mask)
        worst = max(ranked)[1] if ranked else 0
        save_panel(os.path.join(args.out, stem + "-original-sem-com.png"),
                   (original[worst], without[worst], cleaned[worst]))
        for label, (count, number, mask) in category_best.items():
            if count > 0:
                save_crop(os.path.join(args.out, "%s-%s-q%04d-4x.png" % (stem, label, number)), original[number], mask)
        summaries.append((stem + ".gif", len(original), *totals, worst,
                          sum(a != b for frames in zip(original, without) for a, b in zip(*frames))))
    with open(os.path.join(args.out, "medicoes.tsv"), "w", encoding="utf-8", newline="\n") as out:
        out.write("arquivo\tquadros\tquase_pretos\tisolados\tlinhas_claras\tisolados_apos_paleta\tlinhas_apos_paleta\tpixels_mudados_paleta\tpixels_mudados_filtro\tpior_quadro\tdiferencas_sem_limpeza\n")
        for row in summaries:
            out.write("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n" % row)
    with open(os.path.join(args.out, "medicoes-por-quadro.tsv"), "w", encoding="utf-8", newline="\n") as out:
        out.write("arquivo\tquadro\tquase_pretos\tisolados\tlinhas_claras\tisolados_apos_paleta\tlinhas_apos_paleta\tpixels_mudados_paleta\tpixels_mudados_filtro\n")
        for row in details:
            out.write("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n" % row)
    print("medição de ruído: %d GIFs" % len(summaries))


if __name__ == "__main__":
    main()
