#pragma once
#include<thread>                // C++线程标准库
#include<condition_variable>    // C++条件变量标准库
#include<vector>
#include<future>
#include<queue>
#include<iostream>
#include<functional>

/**
 * @brief 线程池配置，先填写预期中的模式以及参数再启动线程池。
 */
struct ThreadPoolConfig {
    size_t core_threads_;            // 核心线程数
    size_t max_threads_;             // 最大线程数
    std::chrono::seconds idle_timeout_;  // 空闲超时(秒)
    size_t max_queue_size_;          // 队列最大长度(0=无线)

    ThreadPoolConfig(size_t core_threads = 5, size_t max_threads = 10,
                     std::chrono::seconds idle_timeout = std::chrono::seconds(10),
                     size_t max_queue_size = 30)
                     : core_threads_(core_threads)
                     , max_threads_(max_threads)
                     , idle_timeout_(idle_timeout)
                     , max_queue_size_(max_queue_size)
                     {}
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
     * @brief 提交任务，无返回值
     * @tparam T
     * @param task
     */
    template<class T>
    void submitTask(T&& task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            while(true) {
                queue_condition_.wait(lock, [this](){
                   return tasks_.size()<config_.max_queue_size_ || stop_;
                });
                break;
            }
            tasks_.emplace(std::forward<T>(task));
        }
        if (should_add_worker()) {
            ///< 添加临时线程
            try_add_worker();
        }
        condition_.notify_one();
    }

    /**
     * @brief 提交任务，有返回值
     * @tparam F
     * @tparam Args
     * @param f
     * @param args
     * @return
     */
    template<class F, class... Args>
      auto submit_with_result(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;
        // 将函数和参数打包成 shared_ptr<packaged_task>
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取 future 用于返回结果
        std::future<ReturnType> future = task->get_future();

        // 加锁，将任务包装成 void() 放入队列
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task]() {
                (*task)();
            });
        }
        if (should_add_worker()) {
            try_add_worker();
        }
        // 通知一个工作线程
        condition_.notify_one();
        return future;
    }

    /**
     * @brief 优雅的关闭，等待任务完成
     */
    void shutdown();

    /**
     * @brief 立即关闭，放弃未执行的任务
     */
    void shutdown_now();

    /**
     * @brief 当前活跃的线程数
     * @return
     */
    size_t active_thread() const;

    /**
     * @brief 最大线程数
     * @return
     */
    size_t max_thread_count() const;

    /**
     * @brief 待执行任务数
     * @return
     */
    size_t pendingTasks() const;

    /**
     * @brief 线程池是否已经关闭
     * @return
     */
    bool is_shutdown() const;

private:
    /**
     * @brief 工作线程函数
     * @param is_core 区分核心/非核心线程
     */
    void worker_thread(bool is_core);

    // 动态扩缩容
    /**
     * @brief尝试增加线程
     */
    void try_add_worker();

    /**
     * @brief 判断是否需要增加线程
     * @return
     */
    bool should_add_worker() const;
private:
    ///< 配置
    ThreadPoolConfig config_;

    ///< 线程队列
    std::vector<std::thread> threads_;
    ///< 当前线程数
    std::atomic<size_t> active_thread_count_;

    ///< 任务队列
    std::queue<std::function<void()>> tasks_; // 接受无参数无返回值的函数指针


    ///< 原语操作
    mutable std::mutex queue_mutex_;
    ///< 队列为空时阻塞工作线程，待有任务时唤醒工作
    std::condition_variable condition_;
    ///< 队列满时阻塞生产者
    std::condition_variable queue_condition_;

    ///< 状态管理
    std::atomic<bool> stop_{false};
};