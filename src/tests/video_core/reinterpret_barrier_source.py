# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the actual destination image barrier emitted after a reinterpret copy."""

from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text(encoding='utf-8')
start = text.index('void TextureCacheRuntime::ReinterpretImage(')
end = text.index('\n}', start)
body = text[start:end]
declaration_start = body.index('        const std::array post_barriers{')
declaration_end = body.index('\n        };', declaration_start) + len('\n        };')
copy = body.index('cmdbuf.CopyBufferToImage(', declaration_end)
call = body.index('        cmdbuf.PipelineBarrier(', copy)
call_end = body.index(';', call) + 1
barrier = body[call:call_end]
assert barrier.endswith('post_barriers);')
output.write_text(body[declaration_start:declaration_end] + '\n' + barrier + '\n',
                  encoding='utf-8')
