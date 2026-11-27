"""Converte um clipe OEL .lkd em quadros OLED para gerar-boot.cjs."""

import io
import json
from pathlib import Path
import struct
import sys
import tarfile


def converter(path):
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"zLKD":
        raise ValueError("Cabeçalho LKD inválido.")
    frame_count, = struct.unpack_from("<I", data, 16)
    with tarfile.open(fileobj=io.BytesIO(data[20:]), mode="r:gz") as archive:
        member, = archive.getmembers()
        with archive.extractfile(member) as source:
            bmp = source.read()

    offset, dib_size, width, height, planes, depth, compression = struct.unpack_from(
        "<IIiiHHI", bmp, 10
    )
    if (bmp[:2] != b"BM" or dib_size < 40 or width <= 0 or height <= 0
            or planes != 1 or depth != 24 or compression != 0
            or frame_count == 0 or height % frame_count):
        raise ValueError("O clipe precisa conter quadros em um BMP BGR de 24 bits.")
    stride = (width * 3 + 3) & ~3
    if offset < 14 + dib_size or len(bmp) < offset + height * stride:
        raise ValueError("BMP truncado.")

    frame_height = height // frame_count
    if width % 2:
        raise ValueError("A largura do quadro precisa ser par.")
    frames = []
    for index in range(frame_count):
        pixels = bytearray(width * frame_height // 2)
        for y in range(frame_height):
            source_y = index * frame_height + y
            row = offset + (height - 1 - source_y) * stride
            for x in range(width):
                source_x = row + x * 3
                blue, green, red = bmp[source_x:source_x + 3]
                intensity = (299 * red + 587 * green + 114 * blue + 8500) // 17000
                pixel = y * width + x
                pixels[pixel // 2] |= intensity << (4 if pixel % 2 == 0 else 0)
        frames.append({"pixels": pixels.hex(), "duration": 63})
    return {"version": 1, "width": width, "height": frame_height, "frames": frames}


if __name__ == "__main__":
    json.dump(converter(Path(sys.argv[1])), sys.stdout)
