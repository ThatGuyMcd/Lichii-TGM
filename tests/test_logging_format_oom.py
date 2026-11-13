"""Compile the actual noexcept formatter with deterministic allocation failure."""
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
    code = r'''
#include <chrono>
#include <cstdint>
#include <new>
#include <string>
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
''' + actual + r'''
int main() {
    Entry entry{"source.cpp"};
    if (FormatLogMessage(entry) != "formatted") return 1;
    fail_allocation = true;
    if (!FormatLogMessage(entry).empty()) return 2;
    entry.filename = nullptr;
    if (!FormatLogMessage(entry).empty()) return 3;
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
    print("PASS: actual formatter normal output, injected bad_alloc and missing filename")


if __name__ == "__main__":
    main()
