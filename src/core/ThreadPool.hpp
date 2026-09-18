// workerExecutor split: RegionFile read/write (zlib) is offloaded to workers, main thread polls futures.
#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <type_traits>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace cppfm::core {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads = 4) : stop_(false) {
        if (threads == 0) threads = 2;
        try {
            for (std::size_t i = 0; i < threads; ++i) {
                workers_.emplace_back([this] {
                    currentPool_ = this;
                    for (;;) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lk(mu_);
                            cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                            if (stop_ && tasks_.empty()) break;
                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }
                        task();
                    }
                    currentPool_ = nullptr;
                });
            }
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~ThreadPool() { shutdown(); }

    void shutdown() {
        // A task may request shutdown as part of its own cleanup.  Joining
        // the current std::thread is an immediate deadlock/termination path;
        // setting the stop flag is sufficient here and the owning thread can
        // perform the joins later (normally from the destructor).
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (currentPool_ == this) return;

        // Joining and clearing workers is separate from the task mutex so
        // concurrent owners cannot race through std::thread::join or mutate
        // the vector while another shutdown is finishing.
        std::lock_guard<std::mutex> joinLock(joinMtx_);
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (workers_.empty()) return;
        }
        for (auto& t : workers_) if (t.joinable()) t.join();
        workers_.clear();
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F>
    auto submit(F&& f)
        -> std::future<std::invoke_result_t<std::decay_t<F>&>> {
        // The callable is stored and invoked as an lvalue by the worker.  A
        // decltype(f()) return type accidentally inspected the forwarding
        // reference as a named lvalue and rejected otherwise valid callable
        // types or deduced the wrong result.
        using Function = std::decay_t<F>;
        using R = std::invoke_result_t<Function&>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_) throw std::runtime_error("ThreadPool stopped");
            tasks_.emplace([task]{ (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }
    std::size_t pending() const {
        std::lock_guard<std::mutex> lk(mu_);
        return tasks_.size();
    }

private:
    inline static thread_local ThreadPool* currentPool_ = nullptr;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::mutex joinMtx_;
    bool stop_;
};

} // namespace cppfm::core
