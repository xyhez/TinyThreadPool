#include "ThreadPool.h"

ThreadPool::ThreadPool(const ThreadPoolConfig& config) :config_(config), active_thread_count_(config_.core_threads_) {
    // 启动线程池初始化---先创建5个核心线程。
    for (int i = 0; i < config_.core_threads_; ++i) {
        threads_.emplace_back(std::thread(&ThreadPool::worker_thread, this, true));
    }

}

ThreadPool::ThreadPool(size_t threads) : active_thread_count_(threads){
    config_.max_threads_ = threads;
    for (int i = 0; i < config_.max_threads_; ++i) {
        threads_.emplace_back(std::thread(&ThreadPool::worker_thread, this, false));
    }
}

ThreadPool::~ThreadPool() {
    if (!stop_.load()) {
        shutdown();

    }
    std::cout<<"subthread exit"<<std::endl;
}

void ThreadPool::shutdown() {
    stop_.store(true);
    condition_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();  // 等待线程执行完当前任务后退出
        }
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
    condition_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();  // join()方法会等待线程执行完任务后退出
        }
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
    return tasks_.size();
}

bool ThreadPool::is_shutdown() const {
    return stop_.load();
}

void ThreadPool::worker_thread(bool is_core) {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (is_core) {
            condition_.wait(lock,[this] {
                return stop_.load() || !tasks_.empty();
            });
        }else {  // is_core==false
            bool has_task = condition_.wait_for(lock,config_.idle_timeout_,[this] {
                return stop_.load() || !tasks_.empty();
            });
            if (!has_task) {
                active_thread_count_.fetch_sub(1);
                return ;
            }
        }
        ///< 如果任务队列为空且stop_为true，则直接退出
        if (stop_.load()&&tasks_.empty()) {
            return ;
        }
        auto task = tasks_.front();
        tasks_.pop();
        lock.unlock();
        task();  // 执行任务
    }
}

void ThreadPool::try_add_worker() {
    //TODO:当任务队列堆积，核心线程无法处理时对线程池进行扩容，在此之前先判断是否需要添加线程
    if (active_thread_count_<config_.max_threads_&&!tasks_.empty()) {
        std::thread t(&ThreadPool::worker_thread, this, false);
        t.detach();
        active_thread_count_.fetch_add(1);
    }
}


bool ThreadPool::should_add_worker() {
    return false;
}



