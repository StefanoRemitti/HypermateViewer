#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace devdisc {

// Fixed-size thread pool with a thread-safe task queue.
//
// * Worker count is configurable and fixed for the lifetime of the pool
//   (no thread-per-IP designs).
// * submit() returns a std::future so task failures (exceptions) propagate to
//   the caller instead of terminating the process.
// * shutdown() drains the queue, joins every worker and is idempotent.
class ThreadPool {
public:
    explicit ThreadPool(unsigned int worker_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using Result = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("ThreadPool: submit() after shutdown()");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    // Stops accepting work, lets queued tasks finish and joins all workers.
    void shutdown();

    unsigned int worker_count() const { return static_cast<unsigned int>(workers_.size()); }
    std::size_t pending() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

}  // namespace devdisc
