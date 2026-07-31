/*
 * Unit tests for vt::ThreadPool (src/core/thread_pool.hh)
 *
 * This is the pool that carries printing and check autosaving off the main Xt
 * event loop. It had no test coverage at all despite being the subject of the
 * June 2026 fixes for the POS freezing under heavy load and after long shifts,
 * so these tests pin down the behaviour those fixes depend on:
 *
 *   - enqueue_detached() must never block the caller, and must drop rather than
 *     wait when the queue is full (a missed print is recoverable, a frozen
 *     till is not).
 *   - A task that throws must not take the process down with it.
 *
 * Every test builds its own pool rather than touching ThreadPool::instance():
 * shutdown() is irreversible and the singleton is a function-local static, so a
 * test that shut the shared pool down would break every later test.
 */

#include <catch2/catch_all.hpp>
#include "src/core/thread_pool.hh"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

// Block a worker until released, so tests can hold the pool busy and fill the
// queue deterministically instead of racing against it.
struct Gate
{
    std::mutex m;
    std::condition_variable cv;
    bool open = false;

    void wait()
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [this] { return open; });
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            open = true;
        }
        cv.notify_all();
    }
};

} // namespace

TEST_CASE("ThreadPool executes enqueued work", "[thread_pool]")
{
    SECTION("enqueue returns a future carrying the result")
    {
        vt::ThreadPool pool(2, 16);

        auto future = pool.enqueue([](int a, int b) { return a + b; }, 20, 22);
        REQUIRE(future.get() == 42);
    }

    SECTION("every submitted task runs exactly once")
    {
        vt::ThreadPool pool(4, 256);
        std::atomic<int> counter{0};

        constexpr int kJobs = 200;
        for (int i = 0; i < kJobs; ++i)
            REQUIRE(pool.enqueue_detached([&counter] { ++counter; }));

        pool.wait_all();
        REQUIRE(counter.load() == kJobs);
    }

    SECTION("wait_all returns only once the pool is idle")
    {
        vt::ThreadPool pool(2, 16);
        std::atomic<int> done{0};

        for (int i = 0; i < 8; ++i)
            pool.enqueue_detached([&done] { std::this_thread::sleep_for(1ms); ++done; });

        pool.wait_all();
        REQUIRE(done.load() == 8);
        REQUIRE(pool.idle());
        REQUIRE(pool.queue_size() == 0);
    }
}

TEST_CASE("ThreadPool bounded queue drops instead of blocking", "[thread_pool][backpressure]")
{
    // This is the freeze fix. enqueue_detached is called from the main event
    // loop; if it ever waits for capacity, the UI, printing, and order saving
    // all stop. It must shed load instead.

    SECTION("enqueue_detached reports false once the queue is full")
    {
        // One worker, queue of 4. Occupy the worker so nothing drains.
        vt::ThreadPool pool(1, 4);
        Gate gate;

        REQUIRE(pool.enqueue_detached([&gate] { gate.wait(); }));

        // Wait for the worker to actually pick that job up, so the queue below
        // reflects only what we add next.
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (pool.queue_size() != 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);

        // Fill the queue to its bound.
        for (size_t i = 0; i < 4; ++i)
            REQUIRE(pool.enqueue_detached([] {}));

        REQUIRE(pool.queue_size() == 4);

        // Full: further work is shed, not queued, and the call returns promptly.
        REQUIRE_FALSE(pool.enqueue_detached([] {}));
        REQUIRE_FALSE(pool.enqueue_detached([] {}));
        REQUIRE(pool.queue_size() == 4);

        gate.release();
        pool.wait_all();
    }

    SECTION("enqueue_detached does not block the caller when the queue is full")
    {
        vt::ThreadPool pool(1, 2);
        Gate gate;

        pool.enqueue_detached([&gate] { gate.wait(); });

        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (pool.queue_size() != 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);

        for (size_t i = 0; i < 2; ++i)
            pool.enqueue_detached([] {});

        // The pool is saturated and its only worker is parked. A blocking
        // implementation would hang here until the gate opens.
        const auto start = std::chrono::steady_clock::now();
        const bool queued = pool.enqueue_detached([] {});
        const auto elapsed = std::chrono::steady_clock::now() - start;

        REQUIRE_FALSE(queued);
        REQUIRE(elapsed < 500ms);

        gate.release();
        pool.wait_all();
    }

    SECTION("capacity is reclaimed once tasks drain")
    {
        vt::ThreadPool pool(1, 2);
        Gate gate;

        pool.enqueue_detached([&gate] { gate.wait(); });

        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (pool.queue_size() != 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);

        REQUIRE(pool.enqueue_detached([] {}));
        REQUIRE(pool.enqueue_detached([] {}));
        REQUIRE_FALSE(pool.enqueue_detached([] {}));

        gate.release();
        pool.wait_all();

        // Draining restores capacity; the pool is not permanently poisoned.
        REQUIRE(pool.enqueue_detached([] {}));
        pool.wait_all();
    }
}

TEST_CASE("ThreadPool contains exceptions thrown by tasks", "[thread_pool][exceptions]")
{
    // enqueue() wraps work in a std::packaged_task, so exceptions travel to the
    // future. enqueue_detached() stores the callable bare -- without a catch in
    // the worker loop, a throwing detached task escapes the thread function and
    // calls std::terminate. Autosave and printing both use the detached path.

    SECTION("an exception from enqueue is delivered through the future")
    {
        vt::ThreadPool pool(2, 16);

        auto future = pool.enqueue([]() -> int { throw std::runtime_error("boom"); });
        REQUIRE_THROWS_AS(future.get(), std::runtime_error);
    }

    SECTION("a throwing detached task does not terminate the process")
    {
        vt::ThreadPool pool(2, 16);

        REQUIRE(pool.enqueue_detached([] { throw std::runtime_error("detached boom"); }));
        REQUIRE(pool.enqueue_detached([] { throw 42; }));  // non-std exception

        // Reaching this line at all is the assertion: an escaping exception
        // would have aborted the test binary.
        pool.wait_all();

        // The pool must still be usable and its accounting intact -- a throw
        // that skipped the active-task decrement would hang wait_all() above.
        std::atomic<bool> ran{false};
        REQUIRE(pool.enqueue_detached([&ran] { ran = true; }));
        pool.wait_all();
        REQUIRE(ran.load());
        REQUIRE(pool.idle());
    }
}

TEST_CASE("ThreadPool shutdown is graceful", "[thread_pool][shutdown]")
{
    SECTION("queued work completes before shutdown returns")
    {
        std::atomic<int> completed{0};
        {
            vt::ThreadPool pool(2, 64);
            for (int i = 0; i < 32; ++i)
                pool.enqueue_detached([&completed] { ++completed; });
            pool.shutdown();

            // shutdown() joins the workers, and workers only exit once the
            // queue is drained, so no task may be left behind.
            REQUIRE(completed.load() == 32);
        }
        REQUIRE(completed.load() == 32);
    }

    SECTION("enqueue_detached refuses work after shutdown")
    {
        vt::ThreadPool pool(1, 8);
        pool.shutdown();

        REQUIRE_FALSE(pool.enqueue_detached([] {}));
    }

    SECTION("enqueue throws after shutdown")
    {
        vt::ThreadPool pool(1, 8);
        pool.shutdown();

        REQUIRE_THROWS_AS(pool.enqueue([] { return 1; }), std::runtime_error);
    }

    SECTION("shutdown is idempotent")
    {
        vt::ThreadPool pool(2, 8);
        pool.shutdown();
        pool.shutdown();  // must not double-join
        REQUIRE_FALSE(pool.enqueue_detached([] {}));
    }

    SECTION("destruction without an explicit shutdown is safe")
    {
        std::atomic<int> completed{0};
        {
            vt::ThreadPool pool(2, 32);
            for (int i = 0; i < 16; ++i)
                pool.enqueue_detached([&completed] { ++completed; });
        }  // dtor calls shutdown()
        REQUIRE(completed.load() == 16);
    }
}
