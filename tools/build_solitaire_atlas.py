#!/usr/bin/env python3
"""Author the two committed Solitaire atlases (not a build dependency).

Requires ImageMagick 6's `convert` and `montage`. Generated face plates remain
decorative: this script owns every rank, suit and pip so card identity is exact.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile


CARD_W = 128
CARD_H = 192
RANKS = ("A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K")
SUITS = (("C", "♣", "#15202b"), ("D", "♦", "#c12635"),
         ("H", "♥", "#c12635"), ("S", "♠", "#15202b"))

PIPS = {
    1: ((64, 101),),
    2: ((64, 55), (64, 147)),
    3: ((64, 51), (64, 101), (64, 151)),
    4: ((38, 55), (90, 55), (38, 147), (90, 147)),
    5: ((38, 55), (90, 55), (64, 101), (38, 147), (90, 147)),
    6: ((38, 48), (90, 48), (38, 101), (90, 101), (38, 154), (90, 154)),
    7: ((38, 45), (90, 45), (64, 76), (38, 104), (90, 104),
        (38, 157), (90, 157)),
    8: ((38, 42), (90, 42), (64, 72), (38, 101), (90, 101),
        (64, 130), (38, 160), (90, 160)),
    9: ((38, 42), (90, 42), (38, 79), (90, 79), (64, 101),
        (38, 123), (90, 123), (38, 160), (90, 160)),
    10: ((38, 39), (90, 39), (64, 68), (38, 91), (90, 91),
         (38, 113), (90, 113), (64, 136), (38, 163), (90, 163)),
}


def pip_draw(suit: str, x: int, y: int, scale: int = 9) -> str:
    half = max(3, scale // 2)
    if suit == "D":
        return (f"polygon {x},{y-scale} {x+scale},{y} "
                f"{x},{y+scale} {x-scale},{y}")
    if suit == "H":
        return (f"circle {x-half},{y-half} {x-scale},{y-half} "
                f"circle {x+half},{y-half} {x+scale},{y-half} "
                f"polygon {x-scale},{y-half} {x+scale},{y-half} "
                f"{x},{y+scale}")
    if suit == "C":
        return (f"circle {x},{y-half} {x},{y-scale} "
                f"circle {x-half},{y} {x-scale},{y} "
                f"circle {x+half},{y} {x+scale},{y} "
                f"rectangle {x-2},{y} {x+2},{y+scale} "
                f"polygon {x-scale+2},{y+scale} {x+scale-2},{y+scale} "
                f"{x},{y+half}")
    return (f"circle {x-half},{y+half} {x-scale},{y+half} "
            f"circle {x+half},{y+half} {x+scale},{y+half} "
            f"polygon {x-scale},{y+half} {x+scale},{y+half} "
            f"{x},{y-scale} rectangle {x-2},{y} {x+2},{y+scale} "
            f"polygon {x-scale+2},{y+scale} {x+scale-2},{y+scale} "
            f"{x},{y+half}")


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def face(source: pathlib.Path, output: pathlib.Path, rank_index: int,
         rank: str, suit_letter: str, color: str) -> None:
    args = [
        "convert", str(source), "-resize", f"{CARD_W}x{CARD_H}!",
        "-font", "Ubuntu-Bold", "-fill", color,
        "-gravity", "NorthWest", "-pointsize", "17",
        "-annotate", "+9+7", rank + suit_letter,
        "-gravity", "SouthEast", "-annotate", "+9+7", rank + suit_letter,
        "-gravity", "NorthWest",
    ]
    if rank_index <= 10:
        args += ["-pointsize", "24"]
        for x, y in PIPS[rank_index]:
            args += ["-draw", pip_draw(suit_letter, x, y)]
    else:
        args += ["-gravity", "Center", "-pointsize", "62",
                 "-annotate", "+0-16", rank,
                 "-gravity", "NorthWest", "-draw",
                 pip_draw(suit_letter, 64, 134, 14)]
    args.append(str(output))
    run(*args)


def atlas(face_source: pathlib.Path, back_source: pathlib.Path,
          output: pathlib.Path) -> None:
    with tempfile.TemporaryDirectory(prefix="glyphcade-solitaire-") as temp:
        root = pathlib.Path(temp)
        tiles: list[pathlib.Path] = []
        for suit_letter, _suit_glyph, color in SUITS:
            for rank_index, rank in enumerate(RANKS, start=1):
                target = root / f"{suit_letter}-{rank_index:02d}.png"
                face(face_source, target, rank_index, rank, suit_letter, color)
                tiles.append(target)

        back = root / "back.png"
        run("convert", str(back_source), "-resize", f"{CARD_W}x{CARD_H}!",
            str(back))
        tiles.append(back)
        for index in range(12):
            blank = root / f"blank-{index:02d}.png"
            run("convert", "-size", f"{CARD_W}x{CARD_H}", "xc:none", str(blank))
            tiles.append(blank)

        output.parent.mkdir(parents=True, exist_ok=True)
        run("montage", *(str(tile) for tile in tiles), "-tile", "13x5",
            "-geometry", f"{CARD_W}x{CARD_H}+0+0", "-background", "none",
            str(output))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=pathlib.Path,
                        default=pathlib.Path("assets"))
    args = parser.parse_args()
    assets = args.assets.resolve()
    source = assets / "solitaire" / "source"
    atlas(source / "neon-face.png", assets / "proof" / "card-back.png",
          assets / "solitaire" / "neon-atlas.png")
    atlas(source / "classic-face.png", source / "classic-back.png",
          assets / "solitaire" / "classic-atlas.png")


if __name__ == "__main__":
    main()
