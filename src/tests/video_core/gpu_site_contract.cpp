// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/dkb_frame_trace.h"

namespace {
int checks{};
int failures{};
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}

constexpr int VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO = 1;
constexpr int VK_QUERY_TYPE_TIMESTAMP = 2;
struct QueryInfo {
    int sType;
    const void* pNext;
    int flags;
    int queryType;
    u32 queryCount;
    int pipelineStatistics;
};
struct QueryPool {
    u32 count{};
};
namespace vk {
struct Exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};
}
struct Physical {
    u32 bits{64};
    float period{1};
    struct QueueFamily { u32 timestampValidBits; };
    struct Properties {
        struct Limits { float timestampPeriod; } limits;
    };
    auto GetQueueFamilyProperties() const { return std::array{QueueFamily{bits}}; }
    auto GetProperties() const { return Properties{{period}}; }
};
struct Logical {
    int throw_on{};
    std::vector<u32> allocations;
    QueryPool CreateQueryPool(QueryInfo info) {
        Check(info.queryType == VK_QUERY_TYPE_TIMESTAMP && !info.flags && !info.pNext,
              "Timestamp query type and flags retained");
        allocations.push_back(info.queryCount);
        if (throw_on == static_cast<int>(allocations.size())) {
            throw vk::Exception{"Independent synthetic allocation failure"};
        }
        return {info.queryCount};
    }
};
struct Device {
    Physical physical;
    Logical logical;
    auto GetPhysical() const { return physical; }
    int GetGraphicsFamily() const { return 0; }
    auto& GetLogical() { return logical; }
};
namespace VideoCommon::GpuSplit {
bool check_owner{};
int checks{};
void CheckOwner(const char*) { ++checks; }
}
struct RecordSite {
    enum class Role { Work, Upload };
    const char* file;
    const char* function;
    u32 line;
    Role role;
    u8 flags;
};
struct Pending {
    bool uploads{};
    std::vector<std::string>& events;
    bool UploadsPending() const { return uploads; }
    void Record() { events.push_back("pending"); }
};
struct Chunk {
    std::vector<std::string>& events;
    bool full{};
    std::vector<int> commands;
    std::vector<RecordSite> sites;
    bool Record(const int& command) {
        events.push_back("record");
        if (full) {
            return false;
        }
        commands.push_back(command);
        return true;
    }
    void NoteSite(RecordSite site) {
        events.push_back("note");
        sites.push_back(site);
    }
};

struct Fixture {
#include "gpu_site_constants.inc"
    Device device;
    u32 trace_timestamp_bits{};
    float trace_timestamp_period{};
    QueryPool trace_query_pool, trace_work_pool, trace_site_pool;
    std::vector<int> trace_submissions, trace_work_items, trace_site_items;
    int warnings{}, information{}, markers{};
    std::vector<std::string> events;
    Pending pending_submission{false, events};
    Chunk first{events, false, {}, {}}, second{events, false, {}, {}};
    Chunk* chunk{&first};
    bool record_sites{};
    void Init() {
#define LOG_WARNING(...) ++warnings
#define LOG_INFO(...) ++information
#include "gpu_site_constructor.inc"
#undef LOG_WARNING
#undef LOG_INFO
    }
    void FlushUploads() {
        events.push_back("uploads");
        pending_submission.uploads = false;
    }
    void RecordSiteMarker(std::source_location) { ++markers; events.push_back("marker"); }
    void DispatchWork() { events.push_back("dispatch"); chunk = &second; }
#include "gpu_site_record.inc"
};

void Exercise(bool frame, bool sites) {
    Fixture fixture;
    fixture.Init();
    Check(fixture.trace_query_pool.count == Fixture::TraceMaxSubmissions * 2,
          "Submission timestamps remain available to the overlay without tracing");
    Check(fixture.trace_submissions.size() == Fixture::TraceMaxSubmissions, "Submission storage retained");
    Check(fixture.trace_work_pool.count == (frame ? Fixture::TraceMaxWork * 2 : 0), "Work pool follows frame tracing");
    Check(fixture.trace_work_items.size() == (frame ? Fixture::TraceMaxWork : 0), "Work items follow frame tracing");
    Check(fixture.trace_site_pool.count == (sites ? Fixture::TraceMaxSites : 0), "Site pool is separately opt-in");
    Check(fixture.trace_site_items.size() == (sites ? Fixture::TraceMaxSites : 0), "Site storage is separately opt-in");
    Check(fixture.device.logical.allocations.size() == 1u + frame + sites, "No unused query pool allocated");

    for (bool full : {false, true}) {
        for (bool uploads : {false, true}) {
            for (bool cost : {false, true}) {
                Fixture recording;
                recording.first.full = full;
                recording.pending_submission.uploads = uploads;
                recording.record_sites = cost;
                VideoCommon::GpuSplit::check_owner = true;
                const int before = VideoCommon::GpuSplit::checks;
                const auto location = std::source_location::current();
                recording.RecordAs(37, location, RecordSite::Role::Upload, 5);
                std::vector<std::string> expected;
                if (uploads) expected.push_back("uploads");
                if (sites) expected.push_back("marker");
                expected.insert(expected.end(), {"pending", "record"});
                if (full) expected.insert(expected.end(), {"dispatch", "record"});
                if (cost) expected.push_back("note");
                Check(recording.events == expected, "Uploads, site markers, recording and chunk retry retain order");
                Check(recording.chunk->commands == std::vector<int>{37}, "The command is retained exactly once");
                Check(recording.markers == sites, "RecordAs gates actual marker calls");
                Check(VideoCommon::GpuSplit::checks == before + 1, "Ownership checks retained");
                Check(recording.chunk->sites.size() == static_cast<size_t>(cost), "Cost-site labels remain independent");
                if (cost) {
                    const auto& site = recording.chunk->sites.front();
                    Check(site.file == location.file_name() && site.function == location.function_name() &&
                          site.line == location.line() && site.role == RecordSite::Role::Upload && site.flags == 5,
                          "Cost-site source, role and flags retained");
                }
            }
        }
    }
    for (const auto physical : {Physical{0, 1}, Physical{65, 1}, Physical{64, 0}}) {
        Fixture unsupported;
        unsupported.device.physical = physical;
        unsupported.Init();
        Check(unsupported.device.logical.allocations.empty(), "Unsupported timestamp devices allocate no pools");
        Check(unsupported.information == frame, "Unsupported-device diagnostic retains its original frame gate");
    }
    for (int allocation = 1; allocation <= 1 + frame + sites; ++allocation) {
        Fixture failed;
        failed.device.logical.throw_on = allocation;
        failed.Init();
        Check(failed.warnings == 1, "Query allocation failure is caught and reported");
        Check(failed.device.logical.allocations.size() == static_cast<size_t>(allocation), "Failed setup makes no later allocations");
        Check(failed.trace_query_pool.count == (allocation > 1 ? Fixture::TraceMaxSubmissions * 2 : 0), "Earlier submission pool retained");
        Check(failed.trace_work_pool.count == (allocation > 2 ? Fixture::TraceMaxWork * 2 : 0), "Earlier work pool retained");
        Check(!failed.trace_site_pool.count, "Failed setup does not invent a site pool");
    }
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Exercise(std::string{argv[1]} == "1", std::string{argv[2]} == "1");
    std::cout << (failures ? "FAIL" : "PASS") << " gpu-site orchestration: " << checks
              << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
