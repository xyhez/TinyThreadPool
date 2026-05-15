#include "../ThreadPool.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

static void check(bool condition, const std::string& msg) {
    if (condition) {
        ++g_passed;
        std::cout << "  PASS: " << msg << '\n';
    } else {
        ++g_failed;
        std::cout << "  FAIL: " << msg << '\n';
    }
}

// =============================================================================
// (a) 多生产者并发提交 4×10000 任务
// =============================================================================
static void test_multi_producer_concurrent_submit() {
    std::cout << "\n[Test] (a) multi-producer 4×10000 concurrent submit\n";

    ThreadPoolConfig config(8, 16, std::chrono::seconds(10), 0);
    ThreadPool pool(config);

    constexpr int producers = 4;
    constexpr int per_producer = 10000;
    constexpr int total = producers * per_producer;
    std::atomic<int> counter{0};
    std::vector<std::thread> producers_list;

    auto submitter = [&pool, &counter](int n) {
        for (int i = 0; i < n; ++i) {
            pool.SubmitTask([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
    };

    for (int i = 0; i < producers; ++i) {
        producers_list.emplace_back(submitter, per_producer);
    }

    for (auto& t : producers_list) {
        t.join();
    }

    // 给线程池一些时间处理完所有任务
    while (pool.PendingTasks() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    pool.Shutdown();

    check(counter.load() == total,
          "all " + std::to_string(total) + " tasks executed, got " + std::to_string(counter.load()));
    check(pool.IsShutdown(), "pool marked shutdown");
}

// =============================================================================
// (b) shutdown() 后验证所有任务完成
// =============================================================================
static void test_shutdown_completes_all_tasks() {
    std::cout << "\n[Test] (b) shutdown() completes all queued tasks\n";

    ThreadPoolConfig config(4, 8, std::chrono::seconds(10), 0);
    ThreadPool pool(config);

    constexpr int tasks = 5000;
    std::atomic<int> counter{0};

    for (int i = 0; i < tasks; ++i) {
        pool.SubmitTask([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.Shutdown();
    check(counter.load() == tasks,
          "after shutdown(), all " + std::to_string(tasks) + " tasks completed, got " + std::to_string(counter.load()));
    check(pool.IsShutdown(), "pool marked shutdown");
    check(pool.ActiveThread() == 0, "all threads exited");
}

// =============================================================================
// (c) shared_ptr 捕获 + unique_ptr 返回值
// =============================================================================
static void test_smart_pointer_tasks() {
    std::cout << "\n[Test] (c) shared_ptr capture + unique_ptr return\n";

    ThreadPoolConfig config(2, 4, std::chrono::seconds(5), 8);
    ThreadPool pool(config);

    // 1) shared_ptr 通过 SubmitTask + lambda capture（std::function 要求可拷贝）
    {
        auto ptr = std::make_shared<int>(42);
        int* raw = ptr.get();
        std::atomic<bool> executed{false};

        pool.SubmitTask([ptr, &executed, raw]() {
            check(*ptr == 42, "shared_ptr value preserved");
            check(ptr.get() == raw, "shared_ptr same address");
            executed.store(true, std::memory_order_relaxed);
        });

        while (!executed.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // 2) std::unique_ptr 通过 SubmitWithResult 返回值（无捕获 lambda，可拷贝）
    {
        auto fut = pool.SubmitWithResult([]() -> std::unique_ptr<int> {
            return std::make_unique<int>(99);
        });
        auto result = fut.get();
        check(result != nullptr && *result == 99, "SubmitWithResult returns unique_ptr, value=99");
    }

    // 3) TrySubmit + shared_ptr（std::function 要求可拷贝）
    {
        auto ptr = std::make_shared<std::string>("hello shared_ptr");
        bool ok = pool.TrySubmit([ptr]() {
            check(*ptr == "hello shared_ptr", "TrySubmit shared_ptr lambda string preserved");
        });
        check(ok, "TrySubmit accepted shared_ptr lambda");
    }

    pool.Shutdown();
}

// =============================================================================
// (d) shutdown_now() 丢弃未执行的任务
// =============================================================================
static void test_shutdown_now_discards_tasks() {
    std::cout << "\n[Test] (d) shutdown_now() discards unexecuted queued tasks\n";

    ThreadPoolConfig config(2, 2, std::chrono::seconds(5), 0);
    ThreadPool pool(config);

    std::atomic<int> executed{0};

    // 先提交慢任务占满线程
    for (int i = 0; i < 2; ++i) {
        pool.SubmitTask([&executed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            executed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 大量快速任务填满队列
    constexpr int fast_tasks = 1000;
    for (int i = 0; i < fast_tasks; ++i) {
        pool.SubmitTask([&executed]() {
            executed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 确保有任务在队列中
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    size_t pending_before = pool.PendingTasks();
    check(pending_before > 0, "tasks pending in queue before shutdown_now");

    pool.ShutdownNow();

    int done = executed.load();
    std::cout << "  INFO: executed=" << done << " / submitted=" << (2 + fast_tasks)
              << " (workers that dequeued tasks before shutdown_now still finish)\n";
    check(done < (2 + fast_tasks),
          "not all tasks executed (some discarded), done=" + std::to_string(done));
    check(done > 0, "at least some tasks did execute");
    check(pool.IsShutdown(), "pool marked shutdown");
}

// =============================================================================
// (e) 关闭后提交被拒绝
// =============================================================================
static void test_submit_rejected_after_shutdown() {
    std::cout << "\n[Test] (e) submission rejected after shutdown\n";

    ThreadPoolConfig config(2, 4, std::chrono::seconds(5), 8);
    ThreadPool pool(config);
    pool.Shutdown();
    check(pool.IsShutdown(), "pool shut down");

    // SubmitTask — 应静默忽略
    std::atomic<bool> called{false};
    pool.SubmitTask([&called]() { called.store(true); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(!called.load(), "SubmitTask rejected after shutdown");

    // TrySubmit — 应返回 false
    bool accepted = pool.TrySubmit([&called]() { called.store(true); });
    check(!accepted, "TrySubmit returns false after shutdown");

    // SubmitWithResult — future 应携带异常
    auto fut1 = pool.SubmitWithResult([]() { return 42; });
    bool caught = false;
    try {
        fut1.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        std::cout << "  INFO: exception message: " << e.what() << '\n';
    }
    check(caught, "SubmitWithResult future carries exception after shutdown");

    // TrySubmitWithResult — future 应携带异常
    auto fut2 = pool.TrySubmitWithResult([]() { return 42; });
    caught = false;
    try {
        fut2.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        std::cout << "  INFO: exception message: " << e.what() << '\n';
    }
    check(caught, "TrySubmitWithResult future carries exception after shutdown");
}

// =============================================================================
// (f) max_queue_size_ = 0 无界队列回归测试
// =============================================================================
static void test_unbounded_queue_regression() {
    std::cout << "\n[Test] (f) max_queue_size=0 unbounded queue regression\n";

    // 构造 max_queue_size=0 的线程池，验证不会死锁
    ThreadPoolConfig config(2, 4, std::chrono::seconds(10), 0);
    ThreadPool pool(config);

    constexpr int tasks = 20000;
    std::atomic<int> counter{0};

    // 一次性提交大量任务，如果 max_queue_size=0 的死锁未修复，这里会挂死
    for (int i = 0; i < tasks; ++i) {
        pool.SubmitTask([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    while (pool.PendingTasks() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    pool.Shutdown();

    check(counter.load() == tasks,
          "all " + std::to_string(tasks) + " tasks executed with unbounded queue, got "
              + std::to_string(counter.load()));
    check(pool.IsShutdown(), "pool marked shutdown");
}

// =============================================================================
// 额外: TrySubmit 满队列拒绝 (非阻塞验证)
// =============================================================================
static void test_try_submit_queue_full() {
    std::cout << "\n[Test] TrySubmit rejection when queue full\n";

    ThreadPoolConfig config(2, 2, std::chrono::seconds(10), 4);
    ThreadPool pool(config);

    std::atomic<int> counter{0};

    // 占满2个线程
    for (int i = 0; i < 2; ++i) {
        pool.SubmitTask([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 填满队列 (size=4)
    for (int i = 0; i < 4; ++i) {
        bool ok = pool.TrySubmit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        check(ok, "TrySubmit accepted while queue has space");
    }

    // 队列已满，TrySubmit 应拒绝
    bool rejected = !pool.TrySubmit([&counter]() {
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    check(rejected, "TrySubmit rejected when queue full");

    pool.ShutdownNow();
}

int main() {
    std::cout << "=== ThreadPool Edge Test Suite ===\n";

    test_multi_producer_concurrent_submit();
    test_shutdown_completes_all_tasks();
    test_smart_pointer_tasks();
    test_shutdown_now_discards_tasks();
    test_submit_rejected_after_shutdown();
    test_unbounded_queue_regression();
    test_try_submit_queue_full();

    std::cout << "\n=== Results: " << g_passed << " passed, " << g_failed << " failed ===\n";
    return g_failed == 0 ? 0 : 1;
}
