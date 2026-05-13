# ThreadPool 压测报告

测试日期：2026-05-11

## 测试环境

- 操作系统/工具链：Windows，Visual Studio 17 2022，MSVC 19.44.35225.0
- 构建类型：Release
- 测试程序检测到的 CPU 逻辑线程数：16
- 构建命令：`cmake -S . -B build && cmake --build build --config Release`
- 运行命令：
  - `.\build\Release\test_normaltest.exe`
  - `.\build\Release\test_normaltest.exe heavy`

## 测试用例

- `submit_with_result cpu`：提交大量 CPU 计算任务，并通过 `future.get()` 校验每个任务的返回结果。
- `submitTask cpu`：提交大量无返回值任务，并通过原子计数器校验任务是否全部完成。
- `bounded queue`：使用很小的任务队列容量（`max_queue_size = 4`）和慢任务，验证队列满时生产者是否会被阻塞。
- `future exception`：验证有返回值任务抛出的异常是否能通过 `future.get()` 重新抛出。

## 默认模式压测结果

| 用例 | 任务数 | 提交耗时(ms) | 总耗时(ms) | 吞吐量(任务/秒) | 校验值 | 状态 | 说明 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| submit_with_result cpu | 20000 | 45.72 | 46.94 | 426033.18 | 8153282785419977069 | PASS | 通过 future.get 校验每个任务结果 |
| submitTask cpu | 50000 | 84.35 | 84.39 | 592506.92 | 18111688763531478657 | PASS | 通过原子计数器校验完成数量 |
| bounded queue | 64 | 461.75 | 494.33 | 129.47 | 64 | PASS | 队列容量为 4，提交过程应该发生阻塞 |
| future exception | 1 | 0.00 | 0.30 | 1.00 | 1 | PASS | 异常应该由 future.get 重新抛出 |

## 重压模式压测结果

| 用例 | 任务数 | 提交耗时(ms) | 总耗时(ms) | 吞吐量(任务/秒) | 校验值 | 状态 | 说明 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| submit_with_result cpu | 100000 | 246.46 | 253.05 | 395180.07 | 2563857540366645632 | PASS | 通过 future.get 校验每个任务结果 |
| submitTask cpu | 200000 | 333.28 | 333.28 | 600096.74 | 8998371907874819738 | PASS | 通过原子计数器校验完成数量 |
| bounded queue | 256 | 1925.87 | 1972.31 | 129.80 | 256 | PASS | 队列容量为 4，提交过程应该发生阻塞 |
| future exception | 1 | 0.00 | 0.33 | 1.00 | 1 | PASS | 异常应该由 future.get 重新抛出 |

## 测试观察

- 本次压测中，基础任务执行、返回值获取、有界队列阻塞、`future.get()` 异常传播都通过了测试。
- 测试输出中每个用例结果前会出现 `subthread exit`，原因是 `ThreadPool::~ThreadPool()` 会向标准输出打印这行内容。
- `bounded queue` 用例的吞吐量明显较低，这是预期结果：任务会 sleep，同时队列容量只有 4，所以生产者提交任务时会被阻塞。
- 构建时 `ThreadPool.h` 和 `ThreadPool.cpp` 出现了 C4819 警告，说明源文件中存在当前代码页 936 无法表示的字符，建议后续统一保存为 UTF-8 编码。

## 当前完善度评估

这个线程池可以支撑基础使用场景，但还不算生产级完善。

- `submitTask` 在线程池关闭后没有拒绝新任务，生产者仍可能继续入队。
- `submit_with_result` 没有像 `submitTask` 一样使用 `max_queue_size_` 做队列限流。
- 临时线程扩容时，`max_threads_` 的检查发生在 `try_add_worker()` 外部；如果多个生产者并发提交，可能竞争并超过最大线程数。
- `active_thread_count_` 在核心线程退出时没有递减，所以它更像“已创建线程数”，不完全等于“当前存活线程数”。
- `ThreadPool(size_t threads)` 只设置了 `max_threads_`，部分配置仍使用默认值，且 `core_threads_` 和实际核心线程数量可能不一致。
- `shutdown()` 没有通知 `queue_condition_`，如果有生产者正阻塞在满队列上，可能出现无法唤醒的问题。
- `submitTask` 的异常只会在线程池内部打印，调用方无法直接感知任务失败；如果需要调用方处理异常，应该优先使用 `submit_with_result`。
