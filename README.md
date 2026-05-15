# TinyThreadPool — 轻量级 C++17 线程池

A lightweight, header-driven C++17 thread pool with dynamic scaling, configurable queues, and graceful shutdown.

轻量级 C++17 线程池，支持动态扩缩容、可配置队列、优雅关闭。

---

## Quick Start / 快速开始

```cpp
#include "ThreadPool.h"

int main() {
    ThreadPoolConfig config(
        4,                           // 核心线程数 core threads
        8,                           // 最大线程数 max threads
        std::chrono::seconds(10),    // 空闲超时 idle timeout
        64,                          // 队列最大长度 max queue size (0=无限 unlimited)
        std::chrono::seconds(5)      // 析构超时 destroy timeout (0=无限等待 wait forever)
    );
    config.thread_name_prefix = "MyPool";  // 线程名前缀 thread name prefix

    ThreadPool pool(config);

    // ---- 提交无返回值任务 / Submit fire-and-forget tasks ----

    // 阻塞提交（队列满时阻塞） Blocking submit
    pool.SubmitTask([]() { /* ... */ });

    // 非阻塞提交（队列满时返回 false） Non-blocking submit
    bool ok = pool.TrySubmit([]() { /* ... */ });

    // ---- 提交有返回值任务 / Submit tasks with return value ----

    std::future<int> f1 = pool.SubmitWithResult([]() { return 42; });
    int result = f1.get();

    std::future<int> f2 = pool.TrySubmitWithResult([]() { return 99; });
    int result2 = f2.get();

    // ---- 关闭线程池 / Shutdown ----

    pool.Shutdown();       // 等待所有任务完成 wait for all tasks
    // 或 / or
    pool.ShutdownNow();   // 立即丢弃未执行任务 discard pending tasks immediately
}
```

### CMake 集成 / CMake Integration

```cmake
find_package(ThreadPool 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE ThreadPool::ThreadPool)
```

---

## Configuration / 配置参考

| 参数 Parameter | 类型 Type | 默认值 Default | 说明 |
|---|---|---|---|
| `core_threads_` | `size_t` | 5 | 常驻线程数，始终存活 / Permanent threads, always alive |
| `max_threads_` | `size_t` | 10 | 线程数上限 / Upper bound of thread count |
| `idle_timeout_` | `std::chrono::seconds` | 10s | 临时线程空闲多久后退出 / Idle time before temp threads exit |
| `max_queue_size_` | `size_t` | 30 | 任务队列最大长度，0=无界 / Max queue length, 0=unbounded |
| `destroy_timeout_` | `std::chrono::seconds` | 0s | 析构超时，0=无限等待 / Destroy timeout, 0=wait forever |
| `thread_name_prefix` | `std::string` | `"TP"` | 线程名前缀，调试器可见 / Thread name prefix visible in debugger |

---

## Thread Lifecycle / 线程生命周期

```
        ┌─────────────────────────────────────────┐
        │  Constructor / 构造                       │
        │  创建 core_threads_ 条核心线程             │
        │  Spawn core_threads_ core threads        │
        └──────────────────┬──────────────────────┘
                           │
                           ▼
        ┌─────────────────────────────────────────┐
        │  运行时 / Runtime                         │
        │                                          │
        │  ┌─ 核心线程 Core thread ────────────┐   │
        │  │  永远存活，无任务时阻塞在条件变量   │   │
        │  │  Always alive, blocks on CV       │   │
        │  └───────────────────────────────────┘   │
        │                                          │
        │  ┌─ 临时线程 Temp thread ────────────┐   │
        │  │  队列堆积时动态创建               │   │
        │  │  空闲 idle_timeout_ 秒后自动退出  │   │
        │  │  Spawned when queue backs up      │   │
        │  │  Exits after idle_timeout_ idle   │   │
        │  └───────────────────────────────────┘   │
        └──────────────────┬──────────────────────┘
                           │
              ┌────────────┴────────────┐
              │                         │
              ▼                         ▼
  ┌───────────────────────┐ ┌────────────────────────┐
  │  Shutdown()           │ │  ShutdownNow()          │
  │  等待所有任务执行完毕  │ │  丢弃队列中所有任务     │
  │  线程 join 后完成     │ │  join 当前执行中的任务  │
  │  Wait & join all      │ │  Discard queued tasks   │
  └───────────────────────┘ │  Join running tasks     │
                            └────────────────────────┘
```

### 线程命名 / Thread Naming

每个线程在启动时自动命名，格式为 `prefix_core_N` 或 `prefix_tmp_N`。Windows 上通过 `SetThreadDescription` 设置，Linux 上通过 `pthread_setname_np` 设置。调试器（VS / WinDbg / GDB）和 `top -H` 可直接查看。

Each thread names itself on start as `prefix_core_N` or `prefix_tmp_N`. Set via `SetThreadDescription` on Windows, `pthread_setname_np` on Linux. Visible in debuggers (VS / WinDbg / GDB) and `top -H`.

---

## API Reference

### 构造函数 Constructors

```cpp
explicit ThreadPool(const ThreadPoolConfig& config);
explicit ThreadPool(size_t threads);  // 固定线程数 fixed thread count
```

### 任务提交 Task Submission

| 方法 Method | 阻塞行为 | 返回值 Return |
|---|---|---|
| `SubmitTask(T&&)` | 队列满时阻塞 / Waits when queue full | `void` |
| `TrySubmit(T&&)` | 立即返回 / Immediate return | `bool` |
| `SubmitWithResult(F&&, Args&&...)` | 队列满时阻塞 / Waits when queue full | `std::future<R>` |
| `TrySubmitWithResult(F&&, Args&&...)` | 立即返回 / Immediate return | `std::future<R>` |

关闭后提交：`SubmitTask` 静默忽略，`TrySubmit` 返回 `false`，带返回值的方法 future 携带 `std::runtime_error` 异常。

After shutdown: `SubmitTask` silently returns, `TrySubmit` returns `false`, value-returning methods set a `std::runtime_error` on the future.

### 关闭 Shutdown

```cpp
void Shutdown();     // 优雅关闭，等待任务完成 Graceful: waits for all queued tasks
void ShutdownNow();  // 立即关闭，丢弃队列 Immediate: discards queued tasks
```

### 可观测性 Observability

```cpp
size_t ActiveThread() const;     // 当前活跃线程数 / Active thread count
size_t MaxThreadCount() const;   // 最大线程数上限 / Max thread limit
size_t PendingTasks() const;     // 队列中待执行任务 / Tasks waiting in queue
bool   IsShutdown() const;       // 是否已关闭 / Whether pool is shut down

size_t TasksSubmitted() const;   // 累计提交数 / Total submissions
size_t TasksCompleted() const;   // 累计完成数 / Total completions
size_t TasksFailed() const;      // 累计异常数 / Total exceptions thrown
size_t TasksRejected() const;    // 累计拒绝数 / Total rejections
```

### 错误回调 Error Handler

```cpp
pool.SetErrorHandler([](std::exception_ptr e) {
    try { std::rethrow_exception(e); }
    catch (const std::exception& ex) {
        // 分发到日志系统 / Dispatch to logging
    }
});
```

---

## Build / 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 安装 / Install (optional)
cmake --install build --prefix /usr/local
```

---

## Thread Safety / 线程安全

所有公开方法均为线程安全。计数器（statistics counters）使用 `std::memory_order_relaxed`，仅用于快速观测，不保证与其他操作之间的 happens-before 关系。

All public methods are thread-safe. Statistics counters use `std::memory_order_relaxed` for fast observation only — no happens-before guarantee with other operations.
