# TinyThreadPool 生产就绪扩展计划

## 背景

当前线程池核心功能已跑通，5 个关键 bug 已修复。但要用于生产环境还需补齐：
错误处理、非阻塞提交、析构超时保护、测试覆盖、可观测性。

---

## Phase 1 — 防止挂死 & 库卫生（8 项，预计 ~120 行改动）

### 1.1 修复 `max_queue_size_ = 0` 无界队列死锁
- **文件**: `ThreadPool.h` submitTask/submit_with_result 的 wait 谓词
- **问题**: `tasks_.size() < 0` 对 `size_t` 永远为 false，生产者永久阻塞
- **改法**: 谓词加 `config_.max_queue_size_ == 0 ||`

### 1.2 移除死代码 `while(true) { wait; break; }`
- **文件**: `ThreadPool.h` submitTask 和 submit_with_result
- **改法**: 去掉 `while(true)` / `break`，只留 `wait(lock, predicate)`

### 1.3 移除 `ThreadPool.h` 中的 `#include <iostream>`
- **文件**: `ThreadPool.h`
- **原因**: 头文件不需要 iostream，会污染所有引用者的编译单元

### 1.4 用错误回调替代 `std::cout`/`std::cerr`
- **文件**: `ThreadPool.h`, `ThreadPool.cpp`
- **改法**:
  - 删除析构函数中的 `std::cout << "subthread exit"`（库代码不应随意输出）
  - 新增 `std::function<void(std::exception_ptr)> error_handler_` 成员
  - 新增 `set_error_handler()` 方法
  - worker 的 catch 块改为调用 error_handler_（若已设置）

### 1.5 修复 `submit_with_result` 关闭后返回空 future 的 UB
- **文件**: `ThreadPool.h`
- **问题**: `std::future<T>()` 是无效 future，调用 `.get()` 是未定义行为
- **改法**: 返回一个设置了 `std::runtime_error` 异常的 promise-future

### 1.6 新增非阻塞 `trySubmit` / `trySubmit_with_result`
- **文件**: `ThreadPool.h`
- **行为**: 队列满时立即返回 false / 空 future，不阻塞
- **用途**: 调用者可自行实现降级策略

### 1.7 析构函数超时保护
- **文件**: `ThreadPool.h`, `ThreadPool.cpp`
- **问题**: 析构调 `shutdown()` 可能被慢任务卡住，永远 block
- **改法**: `ThreadPoolConfig` 新增 `destroy_timeout_`，析构时用 `std::async` + `wait_for` 实现超时 detach

### 1.8 CMake 静态库目标
- **文件**: `CMakeLists.txt`
- **改法**: 新增 `add_library(ThreadPool STATIC ...)`，测试链接库而非直接编译源文件

---

## Phase 2 — 测试补强 & 可调试性（3 项，预计 ~300 行）

### 2.1 新建 `testcase/edge_test.cpp`
覆盖场景：
- (a) 多生产者并发提交 4×10000 任务
- (b) `shutdown()` 后验证所有任务完成
- (c) move-only 类型（`std::unique_ptr` 等）
- (d) `shutdown_now()` 丢弃未执行任务
- (e) 关闭后提交被拒绝
- (f) `max_queue_size_ = 0` 无界队列回归测试

### 2.2 线程命名
- **文件**: `ThreadPool.h`, `ThreadPool.cpp`
- `ThreadPoolConfig` 新增 `thread_name_prefix_`
- 平台适配: Windows `SetThreadDescription` / Linux `pthread_setname_np`
- 新增 `std::atomic<size_t> thread_id_counter_` 给线程编号

### 2.3 基础统计计数器
- **文件**: `ThreadPool.h`, `ThreadPool.cpp`
- 新增 `tasks_submitted_` / `tasks_completed_` 原子计数器
- 提供 `tasks_submitted()` / `tasks_completed()` getter

---

## Phase 3 — 可观测性 & 发布就绪（4 项，预计 ~100 行）

### 3.1 错误 & 拒绝计数器
- 新增 `tasks_rejected_` / `tasks_failed_` 原子计数器
- 提供 getter

### 3.2 公开 `shutdown_with_timeout` 方法
- 从析构函数复用超时逻辑，提供显式 API

### 3.3 CMake install 规则
- `install(TARGETS ...)` + `install(FILES ThreadPool.h ...)`
- 支持 `find_package(ThreadPool)`

### 3.4 文档
- 中文注释转英文（或中英双语）
- 新建 `README.md`：快速开始、配置参考、线程生命周期说明

---

## 验证方式

每完成一个 Phase：
1. `cmake --build .` 编译通过
2. `./normal_test` 通过
3. `./stress_test` 通过（含 heavy 模式）
4. `./edge_test` 通过（Phase 2 起）
5. 手动验证：析构超时、错误回调触发、关闭后提交被拒

---

## 依赖关系

```
Phase 1（必须先完成）
  → Phase 2（新测试验证 Phase 1 修复）
    → Phase 3（计数器、安装、文档）
```
