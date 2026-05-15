#pragma once
#include<thread>                // C++线程标准库
#include<condition_variable>    // C++条件变量标准库
#include<vector>
#include<future>
#include<queue>
#include<functional>
#include<chrono>
#include<string>

/**
 * @brief 线程池配置，先填写预期中的模式以及参数再启动线程池。
 */
struct ThreadPoolConfig {
    size_t core_threads_;            // 核心线程数
    size_t max_threads_;             // 最大线程数
    std::chrono::seconds idle_timeout_;  // 空闲超时(秒)
    size_t max_queue_size_;          // 队列最大长度(0=无限)
    std::chrono::seconds destroy_timeout_;  // 析构超时(0=无限等待)
    std::string thread_name_prefix = "TP";  // 线程名前缀

    ThreadPoolConfig(size_t core_threads = 5, size_t max_threads = 10,
                     std::chrono::seconds idle_timeout = std::chrono::seconds(10),
                     size_t max_queue_size = 30,
                     std::chrono::seconds destroy_timeout = std::chrono::seconds(0))
                     : core_threads_(core_threads)
                     , max_threads_(max_threads)
                     , idle_timeout_(idle_timeout)
                     , max_queue_size_(max_queue_size)
                     , destroy_timeout_(destroy_timeout)
                     {}

    // 设置不同的等级的线程池配置。

};

class ThreadPool {
public:
    /**
     * @param config 传入线程池配置构造线程池
     */
    explicit ThreadPool(const ThreadPoolConfig& config);
    /**
     * @param threads 传入固定线程数构造线程池，其他属性使用默认值
     */
    explicit ThreadPool(size_t threads);

    ~ThreadPool();

    /**
     * @brief 提交任务，无返回值---阻塞
     * @tparam T
     * @param task
     */
    template<class T>
    void SubmitTask(T&& task) {
        if (stop_.load()) {
            return;
        }
        // 检查是否需要添加临时(is_core==false)线程
        TryAddWorker();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            queue_condition_max.wait(lock, [this](){
               return tasks_.size()<config_.max_queue_size_ || stop_ || config_.max_queue_size_ == 0;
            });

            tasks_.emplace(std::forward<T>(task));
            tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
        }
        queue_condition_empty.notify_one();
    }

    /**
     * @brief 提交任务，无返回值---非阻塞
     * @tparam T
     * @param task
     * @return
     */
    template<class T>
    bool TrySubmit(T&& task) {
        if (stop_.load()) {
            return false;
        }
        // 检查是否需要添加临时(is_core==false)线程
        TryAddWorker();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (config_.max_queue_size_ > 0 && tasks_.size() >= config_.max_queue_size_) {
                return false;
            }

            tasks_.emplace(std::forward<T>(task));
            tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
        }
        queue_condition_empty.notify_one();
        return true;
    }

    /**
     * @brief 提交任务，有返回值---阻塞
     * @tparam F
     * @tparam Args
     * @param f
     * @param args
     * @return
     */
    template<class F, class... Args>
      auto SubmitWithResult(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        if (stop_.load()) {
            std::promise<std::invoke_result_t<F, Args...>> p;
            p.set_exception(std::make_exception_ptr(
                std::runtime_error("ThreadPool is shut down, task rejected")));
            return p.get_future();
        }
        using ReturnType = std::invoke_result_t<F, Args...>;
        // 将函数和参数打包成 shared_ptr<packaged_task>
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取 future 用于返回结果
        std::future<ReturnType> future = task->get_future();
        // 查看是否需要添加临时线程
        TryAddWorker();
        // 加锁，将任务包装成 void() 放入队列
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            queue_condition_max.wait(lock, [this](){
               return tasks_.size()<config_.max_queue_size_ || stop_|| config_.max_queue_size_ == 0;
            });

            tasks_.emplace([task]() {
                (*task)();
            });
            tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
        }
        // 通知一个工作线程
        queue_condition_empty.notify_one();
        return future;
    }


    /**
     * @brief 提交任务，有返回值---非阻塞
     * @tparam F
     * @tparam Args
     * @param f
     * @param args
     * @return
     */
    template<class F, class... Args>
    auto TrySubmitWithResult(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F,Args...>> {
        if (stop_.load()) {
            std::promise<std::invoke_result_t<F, Args...>> p;
            p.set_exception(std::make_exception_ptr(
                std::runtime_error("ThreadPool is shut down, task rejected.")));
            return p.get_future();
        }
        using ReturnType = std::invoke_result_t<F, Args...>;
        // 将函数和参数打包成 shared_ptr<packaged_task>
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取 future 用于返回结果
        std::future<ReturnType> future = task->get_future();

        TryAddWorker();
        // 加锁，将任务包装成 void() 放入队列
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (config_.max_queue_size_ > 0 && tasks_.size() >= config_.max_queue_size_) {
                std::promise<std::invoke_result_t<F, Args...>> p;
                p.set_exception(std::make_exception_ptr(
                    std::runtime_error("tasks is full,please retry.")));
                return p.get_future();
            }

            tasks_.emplace([task]() {
                (*task)();
            });
            tasks_submitted_.fetch_add(1, std::memory_order_relaxed);
        }
        // 通知一个工作线程
        queue_condition_empty.notify_one();
        return future;
    }

    /**
     * @brief 优雅的关闭，等待任务完成
     */
    void Shutdown();

    /**
     * @brief 立即关闭，放弃未执行的任务
     */
    void ShutdownNow();

    /**
     * @brief 当前活跃的线程数
     * @return
     */
    size_t ActiveThread() const;

    /**
     * @brief 最大线程数
     * @return
     */
    size_t MaxThreadCount() const;

    /**
     * @brief 待执行任务数
     * @return
     */
    size_t PendingTasks() const;

    /**
     * @brief 线程池是否已经关闭
     * @return
     */
    bool IsShutdown() const;

    /**
     * @brief 设置异常处理的回调函数
     * @param error_handler 异常处理回调函数
     */
    void SetErrorHandler(std::function<void(std::exception_ptr)> error_handler);

    /**
     *
     * @return 已提交的任务数量
     */
    size_t TasksSubmitted() const;

    /**
     *
     * @return 已完成的任务数量
     */
    size_t TasksCompleted() const;

private:
    /**
     * @brief 工作线程函数-从任务队列中取出任务进行处理
     * @param is_core 区分核心/非核心线程
     */
    void WorkerThread(bool is_core);

    // 动态扩缩容
    /**
     * @brief 尝试增加临时线程
     */
    void TryAddWorker();

    /**
     * @brief 线程命名，对于Windows和Linux系统有不同的实现
     * @param name
     */
    static void SetCurrentThreadName(const std::string& name);



private:
    ///< 配置
    ThreadPoolConfig config_;

    ///< 线程队列
    std::vector<std::thread> threads_;
    ///< 当前线程数
    std::atomic<size_t> active_thread_count_;
    ///< 线程ID计数器，每次新增线程时
    std::atomic<size_t> thread_id_counter_{0};

    ///< 任务队列
    std::queue<std::function<void()>> tasks_; // 接受无参数无返回值的函数指针
    ///< 记录提交任务数
    std::atomic<size_t> tasks_submitted_{0};
    ///< 记录完成任务数
    std::atomic<size_t> tasks_completed_{0};


    ///< 原语操作
    mutable std::mutex queue_mutex_;
    ///< 队列为空时阻塞工作线程，待有任务时唤醒工作
    std::condition_variable queue_condition_empty;
    ///< 队列满时阻塞生产者
    std::condition_variable queue_condition_max;

    ///< 状态管理
    std::atomic<bool> stop_{false};

    // 发生异常后的回调函数
    std::function<void(std::exception_ptr)> m_error_handler;
};