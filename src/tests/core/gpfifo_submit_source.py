# SPDX-License-Identifier: GPL-3.0-or-later
"""Expose complete production ioctl serialization and submit staging to CPU tests."""
import hashlib
import json
from pathlib import Path
import sys

root, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
hashes = {}
def read(name):
    raw = (root / name).read_bytes()
    hashes[name] = hashlib.sha256(raw).hexdigest()
    return raw.decode().replace("\r\n", "\n")
def block(text, marker):
    assert text.count(marker) == 1, marker
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]
def save(name, value):
    (out / name).write_text(value + "\n")

serial = read("src/core/hle/service/nvdrv/devices/ioctl_serialization.h")
save("gpfifo_serialization.inc", serial[serial.index("namespace Service::Nvidia::Devices {"):])
dma = read("src/video_core/dma_pusher.h")
save("gpfifo_command_types.inc", block(dma, "struct CommandListHeader {") + ";\n" +
     block(dma, "union CommandHeader {") + ";\n" + block(dma, "struct CommandList final {") + ";")
head = read("src/core/hle/service/nvdrv/devices/nvhost_gpu.h")
save("gpfifo_fixed.inc", block(head, "    struct IoctlSubmitGpfifo {") + ";")
source = read("src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp")
constant = "constexpr size_t GpfifoInlineEntries = 512;"
save("gpfifo_constants.inc", constant if constant in source else "")
for number in (1, 2):
    body = block(source, f"NvResult nvhost_gpu::SubmitGPFIFOBase{number}(")
    save(f"gpfifo_base{number}.inc", body[body.index("{") + 1:-1])
    function = block(source, f"NvResult nvhost_gpu::Ioctl{number}(")
    route = block(function, "        case 0x1b: {")
    save(f"gpfifo_route{number}.inc", route[route.index("{") + 1:-1])
save("source-hashes.json", json.dumps(hashes, indent=2))
