// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/common_types.h"

namespace Nx2Sampler::Detail {

struct RawThread {
    u32 id;
    std::string name;
};

struct RawModule {
    u64 begin;
    u64 end;
    std::string path;
};

// Called after collection, with every sampled thread resumed. Stream one sample at a time
// so retaining all PCs does not require a second, large in-memory JSON tree.
template <class Sample>
bool WriteRawSnapshot(std::ostream& out, std::string_view source_commit,
                      std::span<const RawThread> threads, std::span<const RawModule> modules,
                      std::span<const Sample> samples, size_t max_depth,
                      double requested_seconds, double sampled_seconds) {
    if (!std::isfinite(requested_seconds) || !std::isfinite(sampled_seconds) ||
        requested_seconds < 0 || sampled_seconds < 0) {
        return false;
    }
    for (const auto& sample : samples) {
        if (sample.thread >= threads.size() || sample.depth > sample.frames.size() ||
            sample.depth > max_depth) {
            return false;
        }
    }
    using Json = nlohmann::json;
    Json metadata{{"format", "nx2-samples-raw"}, {"version", 1},
                  {"source_commit", source_commit}, {"max_stack_depth", max_depth},
                  {"requested_seconds", requested_seconds}, {"sampled_seconds", sampled_seconds},
                  {"sample_count", samples.size()}, {"threads", Json::array()},
                  {"modules", Json::array()}};
    for (const auto& thread : threads) {
        metadata["threads"].push_back({{"id", thread.id}, {"name", thread.name}});
    }
    for (const auto& module : modules) {
        metadata["modules"].push_back({{"base", fmt::format("{:#x}", module.begin)},
                                      {"end", fmt::format("{:#x}", module.end)},
                                      {"path", module.path}});
    }
    const auto header = metadata.dump();
    out.write(header.data(), static_cast<std::streamsize>(header.size() - 1));
    out << ",\"samples\":[";
    bool first = true;
    for (const auto& sample : samples) {
        Json record{{"thread", sample.thread}, {"faulted", sample.faulted},
                    {"frames", Json::array()}};
        for (size_t frame = 0; frame < sample.depth; ++frame) {
            // Hex strings preserve all address bits in consumers with 53-bit JSON numbers.
            record["frames"].push_back(fmt::format("{:#x}", sample.frames[frame]));
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << record.dump();
    }
    out << "]}\n";
    return out.good();
}

} // namespace Nx2Sampler::Detail
