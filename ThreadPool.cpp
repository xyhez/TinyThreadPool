#include "ThreadPool.h"

ThreadPool::ThreadPool(const ThreadPoolConfig& config) :config_(config), active_thread_count_(config_.core_threads_) {
    // 启动线程池初始化---先创建5个核心线程。
    for (int i = 0; i < config_.core_threads_; ++i) {
        threads_.emplace_back(std::thread(&ThreadPool::worker_thread, this, true));
    }

}

ThreadPool::ThreadPool(size_t threads) : active_thread_count_(threads){
    config_.max_threads_ = threads;
    config_.core_threads_ = threads;
    for (int i = 0; i < config_.max_threads_; ++i) {
        threads_.emplace_back(std::thread(&ThreadPool::worker_thread, this, true));
    }
}

ThreadPool::~ThreadPool() {
    if (!stop_.load()) {
        shutdown();
    }
}

void ThreadPool::shutdown() {
    stop_.store(true);
    queue_condition_empty.notify_all();
    queue_condition_max.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();  // 等待线程执行完当前任务后退出
        }
        active_thread_count_.fetch_sub(1);
    }
    threads_.clear();
}

void ThreadPool::shutdown_now() {
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        while (!tasks_.empty()) {
            tasks_.pop();
        }
        stop_.store(true);
    }
    queue_condition_empty.notify_all();
    queue_condition_max.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();  // join()方法会等待线程执行完任务后退出
        }
        active_thread_count_.fetch_sub(1);
    }
    threads_.clear();
}

size_t ThreadPool::active_thread() const {
    return active_thread_count_.load();
}

size_t ThreadPool::max_thread_count() const {
    return config_.max_threads_;
}

size_t ThreadPool::pendingTasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

bool ThreadPool::is_shutdown() const {
    return stop_.load();
}

void ThreadPool::worker_thread(bool is_core) {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (is_core) {
            queue_condition_empty.wait(lock,[this] {
                return stop_.load() || !tasks_.empty();
            });
        }else {  // is_core==false---等待idle_timeout_(s)，没有任务则关闭临时线程。
            bool has_task = queue_condition_empty.wait_for(lock,config_.idle_timeout_,[this] {
                return stop_.load() || !tasks_.empty();
            });
            if (!has_task) {
                return ;
            }
        }
        // 如果任务队列为空且stop_为true，则直接退出
        if (stop_.load()&&tasks_.empty()) {
            return ;
        }
        auto task = tasks_.front();
        tasks_.pop();
        lock.unlock();
        try {
            task();  // 执行任务
        }catch (const std::exception_ptr& e) {
            m_error_handler(e);
        } catch (...) {
            m_error_handler(std::current_exception());
        }

        queue_condition_max.notify_one();
    }
}

void ThreadPool::try_add_worker() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (active_thread_count_.load()>=config_.max_threads_) return;
    if (tasks_.empty()) return;
    threads_.emplace_back(&ThreadPool::worker_thread, this, false);
    active_thread_count_.fetch_add(1);
}

void ThreadPool::set_error_handler(std::function<void(std::exception_ptr)> error_handler) {
    m_error_handler = error_handler;
}



