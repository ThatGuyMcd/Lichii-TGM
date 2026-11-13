// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <windows.h>
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/windows/crash_handler.h"

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    const std::filesystem::path path{argv[1]};
    Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, path);
    Common::Log::Initialize();
    Common::Log::Start();
    Common::Windows::InstallCrashHandler(path);
    Common::Windows::InstallCrashHandler(path);
    if (std::string_view{argv[2]} == "abort") {
        std::abort();
    }
    RaiseException(0xe0424242, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 1;
}
