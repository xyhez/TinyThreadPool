#include "../ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BenchmarkCase {
    std::string name;
    std::size_t tasks = 0;
    double submit_ms = 0.0;
    double total_ms = 0.0;
    double throughput = 0.0;
    std::uint64_t checksum = 0;
    bool passed = false;
    std::string note;
};

struct PressureConfig {
    std::size_t future_tasks = 20000;
    int future_iterations = 300;
    std::size_t fire_and_forget_tasks = 50000;
    int fire_and_forget_iterations = 80;
    std::size_t backpressure_tasks = 64;
    int backpressure_sleep_ms = 10;
};

double elapsed_ms(std::chrono::steady_clock::time_point begin,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::uint64_t cpu_work(int iterations, std::uint64_t seed) {
    std::uint64_t x = seed + 0x9e3779b97f4a7c15ULL;
    for (int i = 0; i < iterations; ++i) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
    }
    return x;
}

std::size_t hardware_threads() {
    const unsigned int detected = std::thread::hardware_concurrency();
    return detected == 0 ? 4 : detected;
}

ThreadPoolConfig make_config(std::size_t queue_size) {
    const std::size_t max_threads = std::min<std::size_t>(hardware_threads(), 16);
    const std::size_t core_threads = std::max<std::size_t>(1, max_threads / 2);
    return ThreadPoolConfig(core_threads, max_threads, std::chrono::seconds(2),
                            queue_size);
}

BenchmarkCase run_future_cpu_case(const PressureConfig& pressure) {
    BenchmarkCase result;
    result.name = "submit_with_result cpu";
    result.tasks = pressure.future_tasks;

    ThreadPool pool(make_config(pressure.future_tasks + 64));
    std::vector<std::future<std::uint64_t>> futures;
    futures.reserve(pressure.future_tasks);

    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < pressure.future_tasks; ++i) {
        futures.emplace_back(
            pool.submit_with_result(cpu_work, pressure.future_iterations, i + 1));
    }
    const auto submitted = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0;
    for (auto& future : futures) {
        checksum ^= future.get();
    }
    const auto finished = std::chrono::steady_clock::now();

    pool.shutdown();

    result.submit_ms = elapsed_ms(begin, submitted);
    result.total_ms = elapsed_ms(begin, finished);
    result.throughput = result.tasks * 1000.0 / result.total_ms;
    result.checksum = checksum;
    result.passed = checksum != 0;
    result.note = "future.get verifies every task result";
    return result;
}

BenchmarkCase run_fire_and_forget_case(const PressureConfig& pressure) {
    BenchmarkCase result;
    result.name = "submitTask cpu";
    result.tasks = pressure.fire_and_forget_tasks;

    ThreadPool pool(make_config(pressure.fire_and_forget_tasks + 64));
    std::atomic<std::size_t> completed{0};
    std::atomic<std::uint64_t> checksum{0};
    std::condition_variable done_cv;
    std::mutex done_mutex;

    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < pressure.fire_and_forget_tasks; ++i) {
        pool.submitTask([&, i]() {
            checksum.fetch_xor(cpu_work(pressure.fire_and_forget_iterations, i + 1),
                               std::memory_order_relaxed);
            if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
                pressure.fire_and_forget_tasks) {
                done_cv.notify_one();
            }
        });
    }
    const auto submitted = std::chrono::steady_clock::now();

    {
        std::unique_lock<std::mutex> lock(done_mutex);
        done_cv.wait(lock, [&]() {
            return completed.load(std::memory_order_acquire) ==
                   pressure.fire_and_forget_tasks;
        });
    }
    const auto finished = std::chrono::steady_clock::now();

    pool.shutdown();

    result.submit_ms = elapsed_ms(begin, submitted);
    result.total_ms = elapsed_ms(begin, finished);
    result.throughput = result.tasks * 1000.0 / result.total_ms;
    result.checksum = checksum.load(std::memory_order_relaxed);
    result.passed = completed.load() == pressure.fire_and_forget_tasks;
    result.note = "atomic counter verifies completion";
    return result;
}

BenchmarkCase run_backpressure_case(const PressureConfig& pressure) {
    BenchmarkCase result;
    result.name = "bounded queue";
    result.tasks = pressure.backpressure_tasks;

    ThreadPoolConfig config(2, 2, std::chrono::seconds(1), 4);
    ThreadPool pool(config);
    std::atomic<std::size_t> completed{0};
    std::condition_variable done_cv;
    std::mutex done_mutex;

    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < pressure.backpressure_tasks; ++i) {
        pool.submitTask([&]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(pressure.backpressure_sleep_ms));
            if (completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
                pressure.backpressure_tasks) {
                done_cv.notify_one();
            }
        });
    }
    const auto submitted = std::chrono::steady_clock::now();

    {
        std::unique_lock<std::mutex> lock(done_mutex);
        done_cv.wait(lock, [&]() {
            return completed.load(std::memory_order_acquire) ==
                   pressure.backpressure_tasks;
        });
    }
    const auto finished = std::chrono::steady_clock::now();

    pool.shutdown();

    result.submit_ms = elapsed_ms(begin, submitted);
    result.total_ms = elapsed_ms(begin, finished);
    result.throughput = result.tasks * 1000.0 / result.total_ms;
    result.checksum = completed.load(std::memory_order_relaxed);
    result.passed = completed.load() == pressure.backpressure_tasks;
    result.note = "queue size is 4, so submit should block";
    return result;
}

BenchmarkCase run_future_exception_case() {
    BenchmarkCase result;
    result.name = "future exception";
    result.tasks = 1;

    ThreadPoolConfig config(2, 2, std::chrono::seconds(1), 16);
    ThreadPool pool(config);

    const auto begin = std::chrono::steady_clock::now();
    auto future = pool.submit_with_result([]() -> int {
        throw std::runtime_error("intentional pressure-test exception");
    });
    const auto submitted = std::chrono::steady_clock::now();

    bool caught = false;
    try {
        (void)future.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    const auto finished = std::chrono::steady_clock::now();

    pool.shutdown();

    result.submit_ms = elapsed_ms(begin, submitted);
    result.total_ms = elapsed_ms(begin, finished);
    result.throughput = caught ? 1.0 : 0.0;
    result.checksum = caught ? 1 : 0;
    result.passed = caught;
    result.note = "exception should be rethrown by future.get";
    return result;
}

void print_result(const BenchmarkCase& result) {
    std::cout << "| " << result.name << " | " << result.tasks << " | "
              << result.submit_ms << " | " << result.total_ms << " | "
              << result.throughput << " | " << result.checksum << " | "
              << (result.passed ? "PASS" : "FAIL") << " | " << result.note
              << " |\n";
}

PressureConfig parse_config(int argc, char** argv) {
    PressureConfig config;
    if (argc > 1 && std::string(argv[1]) == "heavy") {
        config.future_tasks = 100000;
        config.future_iterations = 500;
        config.fire_and_forget_tasks = 200000;
        config.fire_and_forget_iterations = 120;
        config.backpressure_tasks = 256;
        config.backpressure_sleep_ms = 5;
    }
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    const PressureConfig pressure = parse_config(argc, argv);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "ThreadPool pressure test\n";
    std::cout << "hardware_threads=" << hardware_threads() << "\n";
    std::cout << "mode=" << ((argc > 1 && std::string(argv[1]) == "heavy")
                                  ? "heavy"
                                  : "default")
              << "\n\n";
    std::cout << "| case | tasks | submit_ms | total_ms | throughput_tasks_s | "
                 "checksum | status | note |\n";
    std::cout << "| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |\n";

    try {
        print_result(run_future_cpu_case(pressure));
        print_result(run_fire_and_forget_case(pressure));
        print_result(run_backpressure_case(pressure));
        print_result(run_future_exception_case());
    } catch (const std::exception& e) {
        std::cerr << "Pressure test failed: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Pressure test failed: unknown exception\n";
        return 1;
    }

    return 0;
}
