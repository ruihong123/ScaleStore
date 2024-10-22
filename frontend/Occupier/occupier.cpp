#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

std::atomic<bool> should_run(true);

void pin_thread_to_core(int core_id) {
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), (1ULL << core_id));
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

void eat_cpu(int core_id) {
    pin_thread_to_core(core_id);
    while (should_run.load()) {
        // Perform intensive calculations to fully utilize the CPU
        for (volatile int i = 0; i < 10000000; ++i) {
            [[maybe_unused]] double result = std::sin(i) * std::cos(i);
        }
    }
}

int main() {
    int core_id = 23;
    std::cout << "Enter the CPU core ID to eat (0-based): ";
//    std::cin >> core_id;

    std::thread eater_thread(eat_cpu, core_id);

//    std::cout << "Eating CPU core " << core_id << ". Press Enter to stop..." << std::endl;
//    std::cin.ignore();
//    std::cin.get();

//    should_run.store(false);
    eater_thread.join();

    std::cout << "CPU core " << core_id << " has been released." << std::endl;
    return 0;
}