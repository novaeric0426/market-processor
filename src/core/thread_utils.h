#pragma once

#include <thread>
#include <spdlog/spdlog.h>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace mde::core {

// Pin the current thread to a specific CPU core.
// Only effective on Linux; no-op on other platforms.
inline bool set_thread_affinity(int cpu_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc == 0) {
        spdlog::info("Thread pinned to CPU {}", cpu_id);
        return true;
    } else {
        spdlog::warn("Failed to pin thread to CPU {} (rc={})", cpu_id, rc);
        return false;
    }
#else
    (void)cpu_id;
    spdlog::debug("CPU affinity not supported on this platform (cpu_id={})", cpu_id);
    return false;
#endif
}

// Set thread name for profiling/debugging visibility.
inline void set_thread_name(const char* name) {
#ifdef __linux__
    pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#else
    (void)name;
#endif
}

} // namespace mde::core
