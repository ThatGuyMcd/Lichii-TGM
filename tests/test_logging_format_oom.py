"""Compile the real log formatter and ring backend with deterministic allocation failure."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", required=True)
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[1] / "src/common/logging.cpp").read_text(encoding="utf-8")
    begin = source.index("std::string FormatLogMessage(const Entry& entry) noexcept {")
    end = source.index("\n}\n", begin) + 2
    actual = source[begin:end]
    assert actual.count("fmt::format(") == 1
    actual = actual.replace("fmt::format(", "FaultInjectedFormat(", 1)
    begin = source.index("struct RingBackend final : public Backend {")
    end = source.index("\n};\n", begin) + len("\n};")
    ring = source[begin:end]
    code = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <new>
#include <string>
#include <utility>
struct Entry {
    const char* filename;
    std::chrono::microseconds timestamp{};
    int log_class = 0, log_level = 0, line_num = 3;
    const char* function = "unit", *message = "message";
};
const char* GetLogClassName(int) { return "Core"; }
const char* GetLevelName(int) { return "Warning"; }
bool fail_allocation = false;
template<class... Args> std::string FaultInjectedFormat(const char*, Args&&...) {
    if (fail_allocation) throw std::bad_alloc{};
    return "formatted";
}
struct Backend {
    virtual ~Backend() = default;
    virtual void Write(const Entry&) noexcept = 0;
    virtual void Flush() noexcept = 0;
};
struct RingLine {
    std::uint64_t sequence;
    std::string text;
};
''' + actual + "\n" + ring + r'''
int main() {
    Entry entry{"source.cpp"};
    if (FormatLogMessage(entry) != "formatted") return 1;
    RingBackend backend;
    backend.capacity.store(1);
    backend.Write(entry);
    if (backend.lines.size() != 1 || backend.lines.front().text != "formatted") return 4;
    fail_allocation = true;
    if (!FormatLogMessage(entry).empty()) return 2;
    backend.Write(entry);
    if (backend.lines.size() != 1 || backend.lines.front().sequence != 1 ||
        backend.lines.front().text != "formatted") return 5;
    fail_allocation = false;
    entry.filename = nullptr;
    if (!FormatLogMessage(entry).empty()) return 3;
    backend.Write(entry);
    if (backend.lines.size() != 1 || backend.lines.front().sequence != 1) return 6;
}
'''
    with tempfile.TemporaryDirectory(prefix="tgm-log-oom-") as directory:
        root = Path(directory)
        cpp = root / "formatter.cpp"
        cpp.write_text(code, encoding="utf-8")
        if os.name == "nt":
            exe = root / "formatter.exe"
            command = [args.cxx, "/nologo", "/std:c++20", "/EHsc", "/MD", str(cpp), "/Fe:" + str(exe)]
        else:
            exe = root / "formatter"
            command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                       "-fno-omit-frame-pointer", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(exe)], cwd=root, check=True, timeout=15)
    print("PASS: formatter and diagnostic ring with normal, failed and missing input")


if __name__ == "__main__":
    main()
