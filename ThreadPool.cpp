#include "ThreadPool.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#endif

ThreadPool::ThreadPool(const ThreadPoolConfig& config) :config_(config), active_thread_count_(config_.core_threads_) {
    // 启动线程池初始化---先创建5个核心线程。
    for (int i = 0; i < config_.core_threads_; ++i) {
        threads_.emplace_back(std::thread(&ThreadPool::WorkerThread, this, true));
    }

}

ThreadPool::ThreadPool(size_t threads) : active_thread_count_(threads){
    config_.max_threads_ = threads;
    config_.core_threads_ = threads;
    for (int i = 0; i < config_.max_threads_; ++i) {
        threads_.emplace_back(std::thread(&ThreadPool::WorkerThread, this, true));
    }
}

ThreadPool::~ThreadPool() {
    if (stop_.load()) {
        return;
    }
    if (config_.destroy_timeout_.count() == 0) {
        ShutdownNow();
    } else {
        auto future = std::async(std::launch::async, [this]() {
            ShutdownNow();
        });
        if (future.wait_for(config_.destroy_timeout_) == std::future_status::timeout) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                while (!tasks_.empty()) {
                    tasks_.pop();
                }
                stop_.store(true);
            }
            queue_condition_empty.notify_all();
            queue_condition_max.notify_all();
            for (auto& t : threads_) {
                if (t.joinable()) {
                    t.detach();
                }
            }
            threads_.clear();
        }
    }
}

void ThreadPool::Shutdown() {
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

void ThreadPool::ShutdownNow() {
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

size_t ThreadPool::ActiveThread() const {
    return active_thread_count_.load();
}

size_t ThreadPool::MaxThreadCount() const {
    return config_.max_threads_;
}

size_t ThreadPool::PendingTasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

bool ThreadPool::IsShutdown() const {
    return stop_.load();
}

void ThreadPool::WorkerThread(bool is_core) {
    std::string name = config_.thread_name_prefix;
    name += is_core ? "_core_" : "_tmp_";
    name += std::to_string(thread_id_counter_.fetch_add(1, std::memory_order_relaxed));
    SetCurrentThreadName(name);

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
            tasks_failed_.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            m_error_handler(std::current_exception());
            tasks_failed_.fetch_add(1, std::memory_order_relaxed);
        }
        tasks_completed_.fetch_add(1, std::memory_order_relaxed);
        queue_condition_max.notify_one();
    }
}

void ThreadPool::TryAddWorker() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (active_thread_count_.load()>=config_.max_threads_) return;
    if (tasks_.empty()) return;
    threads_.emplace_back(&ThreadPool::WorkerThread, this, false);
    active_thread_count_.fetch_add(1);
}

void ThreadPool::SetErrorHandler(std::function<void(std::exception_ptr)> error_handler) {
    m_error_handler = error_handler;
}

void ThreadPool::SetCurrentThreadName(const std::string &name) {
#ifdef _WIN32
    using SetThreadDesc_t = HRESULT (WINAPI *)(HANDLE, PCWSTR);
    static SetThreadDesc_t pSetThreadDesc = []() -> SetThreadDesc_t {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        return reinterpret_cast<SetThreadDesc_t>(
            GetProcAddress(kernel32, "SetThreadDescription"));
    }();
    if (!pSetThreadDesc) return;

    int len = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    std::wstring wname(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wname[0], len);
    pSetThreadDesc(GetCurrentThread(), wname.c_str());
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
#endif
}

size_t ThreadPool::TasksSubmitted() const {
    return tasks_submitted_.load();
}

size_t ThreadPool::TasksCompleted() const {
    return tasks_completed_.load();
}

size_t ThreadPool::TasksFailed() const {
    return tasks_failed_.load();
}

size_t ThreadPool::TasksRejected() const {
    return task_rejected_.load();
}

