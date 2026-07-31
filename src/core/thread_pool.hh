/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * thread_pool.hh - Lightweight thread pool for async I/O operations
 * Optimized for resource-constrained systems like Raspberry Pi
 */

#ifndef VT_THREAD_POOL_HH
#define VT_THREAD_POOL_HH

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace vt {

/**
 * @brief A lightweight, efficient thread pool for async I/O operations.
 * 
 * Designed for resource-constrained systems:
 * - Default 2 threads (optimal for RPi with limited cores)
 * - Bounded queue to prevent memory exhaustion
 * - Graceful shutdown with task completion
 * 
 * Usage:
 *   auto& pool = ThreadPool::instance();
 *   auto future = pool.enqueue([](){ return heavy_io_operation(); });
 *   // ... do other work ...
 *   auto result = future.get();  // blocks until complete
 */
class ThreadPool {
public:
    // Singleton instance - 4 threads handles concurrent print jobs to multiple printers.
    // Queue bound of 256 is large enough for a busy shift; see enqueue_detached
    // for what happens when it is exceeded.
    static ThreadPool& instance(size_t num_threads = 4) {
        static ThreadPool pool(num_threads, 256);
        return pool;
    }

    /**
     * @brief Construct an independent pool.
     *
     * Production code should use instance().  This is public so that tests can
     * exercise a pool in isolation: the singleton's shutdown() is irreversible
     * (stop_ is never cleared and instance() returns a function-local static),
     * so a test that shut the shared pool down would silently break every test
     * that ran after it.  Being able to choose max_queue_size also makes the
     * bounded-queue drop policy testable without enqueueing 256 real jobs.
     */
    ThreadPool(size_t num_threads, size_t max_queue_size);

    // Delete copy/move operations
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() {
        shutdown();
    }

    /**
     * @brief Enqueue a task for async execution
     * @param f Function to execute
     * @param args Arguments to pass to the function
     * @return std::future for the result
     * 
     * Example:
     *   auto future = pool.enqueue(&save_file, filename, data);
     *   // ... later ...
     *   bool success = future.get();
     */
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result_t<F, Args...>>
    {
        using return_type = typename std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Don't allow enqueueing after stopping
            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            // Bounded queue - wait if full (prevents memory exhaustion)
            queue_not_full_.wait(lock, [this] {
                return tasks_.size() < max_queue_size_ || stop_;
            });

            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            tasks_.emplace([task]() { (*task)(); });
        }
        
        condition_.notify_one();
        return result;
    }

    /**
     * @brief Enqueue a task without caring about the result (fire-and-forget).
     *
     * IMPORTANT: This is intentionally non-blocking.  The caller may be the
     * main Xt event loop thread.  Blocking it even briefly causes the UI,
     * printing, and order saving to freeze under heavy load.
     *
     * If the queue is full the job is dropped and a message is written to
     * stderr.  A missed print is recoverable (operator can reprint); a frozen
     * POS is not.
     *
     * @return true if the job was queued, false if it was dropped.
     */
    template<typename F, typename... Args>
    bool enqueue_detached(F&& f, Args&&... args) {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            if (stop_) return false;

            if (tasks_.size() >= max_queue_size_) {
                fprintf(stderr,
                    "ThreadPool::enqueue_detached: queue full (%zu/%zu), "
                    "dropping job\n",
                    tasks_.size(), max_queue_size_);
                return false;
            }

            tasks_.emplace(std::move(task));
        }

        condition_.notify_one();
        return true;
    }

    /**
     * @brief Get current queue size (for monitoring)
     */
    size_t queue_size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    /**
     * @brief Check if pool is idle (no pending tasks)
     */
    bool idle() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.empty() && active_tasks_ == 0;
    }

    /**
     * @brief Wait for all currently queued tasks to complete
     */
    void wait_all() {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        all_done_.wait(lock, [this] {
            return tasks_.empty() && active_tasks_ == 0;
        });
    }

    /**
     * @brief Gracefully shutdown the pool (waits for pending tasks)
     */
    void shutdown() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) return;
            stop_ = true;
        }
        
        condition_.notify_all();
        queue_not_full_.notify_all();
        
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable queue_not_full_;
    std::condition_variable all_done_;
    
    std::atomic<bool> stop_;
    size_t active_tasks_;
    const size_t max_queue_size_;
};

inline ThreadPool::ThreadPool(size_t num_threads, size_t max_queue_size)
    : stop_(false)
    , active_tasks_(0)
    , max_queue_size_(max_queue_size)
{
    workers_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);

                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });

                    if (stop_ && tasks_.empty()) {
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                    ++active_tasks_;
                }

                queue_not_full_.notify_one();

                // Execute outside the lock.
                //
                // This must not let an exception escape. Tasks submitted through
                // enqueue() are wrapped in a std::packaged_task, which stores any
                // exception in the future -- but enqueue_detached() stores the
                // callable bare, so a throwing detached task would propagate out
                // of this thread function and call std::terminate. Detached is
                // what the autosave and print paths use, so an I/O failure that
                // threw would take down the whole POS. It would also skip the
                // --active_tasks_ below and hang wait_all() forever.
                try {
                    task();
                }
                catch (const std::exception &e) {
                    fprintf(stderr, "ThreadPool: task threw: %s\n", e.what());
                }
                catch (...) {
                    fprintf(stderr, "ThreadPool: task threw an unknown exception\n");
                }

                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    --active_tasks_;
                }
                all_done_.notify_all();
            }
        });
    }
}

/**
 * @brief Simple async file I/O helpers
 */
namespace async_io {

/**
 * @brief Async file write (fire-and-forget)
 * @param filepath Path to write to
 * @param data Data to write
 * @param callback Optional callback on completion (success/failure)
 * 
 * Safe for use from main thread - won't block UI.
 */
inline void write_file_async(
    const std::string& filepath,
    const std::string& data,
    std::function<void(bool)> callback = nullptr)
{
    ThreadPool::instance().enqueue_detached([filepath, data, callback]() {
        FILE* fp = fopen(filepath.c_str(), "w");
        bool success = false;
        if (fp) {
            success = (fwrite(data.c_str(), 1, data.size(), fp) == data.size());
            fclose(fp);
        }
        if (callback) {
            callback(success);
        }
    });
}

/**
 * @brief Async file read with callback
 * @param filepath Path to read from
 * @param callback Called with file contents (empty on error)
 */
inline void read_file_async(
    const std::string& filepath,
    std::function<void(const std::string&)> callback)
{
    ThreadPool::instance().enqueue_detached([filepath, callback]() {
        std::string content;
        FILE* fp = fopen(filepath.c_str(), "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            if (size > 0) {
                content.resize(static_cast<size_t>(size));
                size_t read = fread(content.data(), 1, static_cast<size_t>(size), fp);
                content.resize(read);
            }
            fclose(fp);
        }
        callback(content);
    });
}

} // namespace async_io

/**
 * @brief Write-behind buffer for batching frequent writes
 * 
 * Collects writes and flushes them periodically to reduce
 * disk I/O on systems with slow storage (SD cards).
 */
class WriteBehindBuffer {
public:
    using WriteFunc = std::function<bool(const std::string& key, const std::string& data)>;

    explicit WriteBehindBuffer(WriteFunc writer, 
                               std::chrono::milliseconds flush_interval = std::chrono::milliseconds(2000))
        : writer_(std::move(writer))
        , flush_interval_(flush_interval)
        , stop_(false)
    {
        flush_thread_ = std::thread([this]() {
            while (!stop_) {
                std::this_thread::sleep_for(flush_interval_);
                flush();
            }
            // Final flush on shutdown
            flush();
        });
    }

    ~WriteBehindBuffer() {
        stop_ = true;
        if (flush_thread_.joinable()) {
            flush_thread_.join();
        }
    }

    // Non-copyable
    WriteBehindBuffer(const WriteBehindBuffer&) = delete;
    WriteBehindBuffer& operator=(const WriteBehindBuffer&) = delete;

    /**
     * @brief Queue a write (returns immediately)
     * @param key Unique identifier for this write (later writes overwrite earlier)
     * @param data Data to write
     */
    void write(const std::string& key, const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_writes_[key] = data;
    }

    /**
     * @brief Force immediate flush of all pending writes
     */
    void flush() {
        std::unordered_map<std::string, std::string> to_write;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            to_write.swap(pending_writes_);
        }

        for (const auto& [key, data] : to_write) {
            writer_(key, data);
        }
    }

    /**
     * @brief Check if there are pending writes
     */
    bool has_pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !pending_writes_.empty();
    }

private:
    WriteFunc writer_;
    std::chrono::milliseconds flush_interval_;
    std::unordered_map<std::string, std::string> pending_writes_;
    mutable std::mutex mutex_;
    std::thread flush_thread_;
    std::atomic<bool> stop_;
};

} // namespace vt

#endif // VT_THREAD_POOL_HH
