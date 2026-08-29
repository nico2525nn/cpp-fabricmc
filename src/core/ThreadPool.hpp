// ThreadPool: fixed-size worker pool for async chunk I/O (plan21 W19).
// Mirrors Yarn ThreadedAnvilChunkStorage's mainThreadExecutor + workerExecutor
// split: RegionFile read/write (zlib) is offloaded to workers, main thread polls futures.
#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cppfm::core {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads = 4) : stop_(false) {
        if (threads == 0) threads = 2;
        for (std::size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(mu_);
                        cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using R = decltype(f());
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
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stop_;
};

} // namespace cppfm::core
