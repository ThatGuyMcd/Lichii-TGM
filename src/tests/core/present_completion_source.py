"""Extract actual GPU submission and service failure-completion boundaries for CPU tests."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
source = (args.source_root / 'src/video_core/gpu.cpp').read_text()
start = source.index('    void RequestComposite(')
begin = source.index('{', start)
end = source.index('\n    void WaitForComposite()', begin)
body = source[begin + 1:end].rstrip()
assert body.endswith('}')
body = body[:-1]
service = (args.source_root / 'src/core/hle/service/nx2_gsv.h').read_text()
start = service.index('    void Composite(u32 image_id,')
begin = service.index('{', start)
end = service.index('        NotifyHdrChange();', begin)
prefix = service[begin + 1:end]
submit = service.index('        system.GPU().RequestComposite(', end)
submit_end = service.index('        CaptureFrame();', submit)
declaration = service.find('        std::function<void()> on_composited;', end, submit)
dispatch = service[declaration if declaration != -1 else submit:submit_end]
queue_begin = service.index('        const auto value = m_release_queue->Queue(')
queue_end = service.index(';', queue_begin) + 1
args.output_dir.mkdir(parents=True, exist_ok=True)
(args.output_dir / 'present_gpu_body.inc').write_text(body)
(args.output_dir / 'present_service_prefix.inc').write_text(prefix)
(args.output_dir / 'present_service_dispatch.inc').write_text(dispatch)
(args.output_dir / 'present_service_enqueue.inc').write_text(service[queue_begin:queue_end])
