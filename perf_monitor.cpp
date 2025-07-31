#include <fstream>
#include <thread>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <sys/ioctl.h>

using namespace std;

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

struct PerfCounter {
    string name;
    int type;
    int config;
    int fd = -1;
    long long value = 0;

    PerfCounter(const string& n, int t, int c) : name(n), type(t), config(c) {}
};

vector<PerfCounter> setupCounters() {
    int READ = PERF_COUNT_HW_CACHE_OP_READ;
    int MISS = PERF_COUNT_HW_CACHE_RESULT_MISS;
    int ACCESS = PERF_COUNT_HW_CACHE_RESULT_ACCESS;

    vector<PerfCounter> counters = {
        {"L1D_CACHE_REFILL", PERF_TYPE_HW_CACHE,
         PERF_COUNT_HW_CACHE_L1D | (READ << 8) | (MISS << 16)},
        {"L1D_CACHE_ACCESS", PERF_TYPE_HW_CACHE,
         PERF_COUNT_HW_CACHE_L1D | (READ << 8) | (ACCESS << 16)},
        {"INSTRUCTIONS", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
        // Removed EXCEPTIONS since not defined on your platform
        {"BRANCH_MISPREDICTS", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
        {"BUS_ACCESS", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES}, // fallback for bus access
    };

    for (auto &c : counters) {
        struct perf_event_attr pe {};
        memset(&pe, 0, sizeof(struct perf_event_attr));
        pe.type = c.type;
        pe.size = sizeof(struct perf_event_attr);
        pe.config = c.config;
        pe.disabled = 1;
        pe.exclude_kernel = 0;
        pe.exclude_hv = 1;

        c.fd = perf_event_open(&pe, 0, -1, -1, 0);
        if (c.fd == -1) {
            cerr << "[!] Failed to open: " << c.name << " (not supported)\n";
        }
    }

    return counters;
}

void startCounters(const vector<PerfCounter>& counters) {
    for (const auto& c : counters) {
        if (c.fd != -1) ioctl(c.fd, PERF_EVENT_IOC_RESET, 0);
        if (c.fd != -1) ioctl(c.fd, PERF_EVENT_IOC_ENABLE, 0);
    }
}

void stopCounters(vector<PerfCounter>& counters) {
    for (auto& c : counters) {
        if (c.fd != -1) ioctl(c.fd, PERF_EVENT_IOC_DISABLE, 0);
        if (c.fd != -1) read(c.fd, &c.value, sizeof(long long));
    }
}

int main() {
    cout << "📊 Monitoring Performance Counters every 5s for 30 minutes...\n";

    // Setup performance counters once
    auto counters = setupCounters();

    // Open output file for writing (truncate if exists)
    ofstream outfile("/data/local/tmp/hpc_output.csv");
    if (!outfile.is_open()) {
        cerr << "❌ Failed to open output file\n";
        return 1;
    }

    // Write CSV header
    outfile << "Time(s)";
    for (const auto &c : counters)
        if (c.fd != -1)
            outfile << "," << c.name;
    outfile << "\n";

    const int interval = 5;          // seconds
    const int total_duration = 30 * 60; // 30 minutes
    const int iterations = total_duration / interval;

    for (int i = 0; i < iterations; ++i) {
        cout << "⏱️ Iteration " << i + 1 << "/" << iterations << "\n";

        startCounters(counters);
        this_thread::sleep_for(chrono::seconds(interval));
        stopCounters(counters);

        // Write one line to file
        outfile << (i + 1) * interval;
        for (const auto &c : counters)
            if (c.fd != -1)
                outfile << "," << c.value;
        outfile << "\n";
    }

    outfile.close();
    cout << "✅ Data collection complete: /data/local/tmp/hpc_output.csv\n";
    return 0;
}
