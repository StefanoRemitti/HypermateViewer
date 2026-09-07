#include "threading/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include "test_support.hpp"

int main() {
    SECTION("multiple tasks are all executed");
    {
        devdisc::ThreadPool pool(4);
        std::atomic<int> sum{0};
        std::vector<std::future<void>> futures;
        for (int i = 1; i <= 100; ++i) {
            futures.push_back(pool.submit([&sum, i]() { sum += i; }));
        }
        for (auto& future : futures) {
            future.get();
        }
        CHECK_EQ(sum.load(), 5050);
        CHECK_EQ(pool.worker_count(), 4u);
    }

    SECTION("empty queue and clean shutdown");
    {
        devdisc::ThreadPool pool(8);
        CHECK_EQ(pool.pending(), std::size_t(0));
        pool.shutdown();
        pool.shutdown();  // idempotent
        CHECK_EQ(pool.worker_count(), 0u);
        bool threw = false;
        try {
            pool.submit([]() {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
    }

    SECTION("task exceptions propagate through the future");
    {
        devdisc::ThreadPool pool(2);
        auto future = pool.submit([]() -> int { throw std::runtime_error("boom"); });
        bool threw = false;
        std::string message;
        try {
            (void)future.get();
        } catch (const std::exception& ex) {
            threw = true;
            message = ex.what();
        }
        CHECK(threw);
        CHECK_EQ(message, std::string("boom"));

        // The pool stays usable after a failing task.
        CHECK_EQ(pool.submit([]() { return 42; }).get(), 42);
    }

    SECTION("tasks really run concurrently");
    {
        devdisc::ThreadPool pool(8);
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::future<void>> futures;
        for (int i = 0; i < 8; ++i) {
            futures.push_back(pool.submit(
                []() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }));
        }
        for (auto& future : futures) {
            future.get();
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        CHECK(elapsed < 600);
    }

    SECTION("worker count is never zero");
    {
        devdisc::ThreadPool pool(0);
        CHECK_EQ(pool.worker_count(), 1u);
        CHECK_EQ(pool.submit([]() { return 7; }).get(), 7);
    }

    return testing::summary("thread_pool");
}
