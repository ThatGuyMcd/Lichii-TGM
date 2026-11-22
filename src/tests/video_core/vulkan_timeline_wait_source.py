# SPDX-License-Identifier: GPL-3.0-or-later
"""Expose the production wait and refresh bodies to a scripted Vulkan backend."""

from pathlib import Path
import sys


source = Path(sys.argv[1]).read_text(encoding="utf-8")
start = source.index("void MasterSemaphore::Refresh() {")
end = source.index("VkResult MasterSemaphore::SubmitQueue(", start)
output = Path(sys.argv[2])
output.write_text(source[start:end], encoding="utf-8")
