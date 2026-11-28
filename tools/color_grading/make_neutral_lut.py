"""Writes the neutral colour grading LUT strip that tonemap.slang applyLut reads.

Layout: 32 slices of 32x32 side by side (1024x32). Red runs across a slice, green down, blue picks the slice.
Artists paste this strip into a screenshot, grade the screenshot, then crop the strip back out.

Usage: python make_neutral_lut.py <output.png>
"""

import sys

import numpy as np
from PIL import Image

SIZE = 32


def neutral_lut(size: int = SIZE) -> np.ndarray:
    x = np.arange(size * size)
    y = np.arange(size)
    xx, yy = np.meshgrid(x, y)
    rgb = np.stack([xx % size, yy, xx // size], axis=-1) / (size - 1)
    return np.round(rgb * 255).astype(np.uint8)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    lut = neutral_lut()
    assert lut.shape == (SIZE, SIZE * SIZE, 3)
    assert tuple(lut[0, 0]) == (0, 0, 0) and tuple(lut[-1, -1]) == (255, 255, 255)
    assert tuple(lut[5, 3 * SIZE + 7]) == tuple(round(v * 255 / (SIZE - 1)) for v in (7, 5, 3))
    Image.fromarray(lut, "RGB").save(sys.argv[1])
