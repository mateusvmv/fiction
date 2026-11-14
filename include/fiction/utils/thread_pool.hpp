/*
 * Copyright (c) 2018 - 2026 Marcel Walter
 * Copyright (c) 2026 - present Chair for Design Automation, Technical University of Munich
 * All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License
 */

/**
 * @file
 * @brief A contextful thread pool to avoid thread creation overheads
 * @author Mateus Vasconcelos (mateusvmv)
 */

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <stdexcept>
#include <algorithm>

namespace fiction::utils
{

/**
 * @brief A simple, robust thread pool for executing tasks concurrently.
 */
class thread_pool
{
  public:
    explicit thread_pool(const std::size_t num_threads = std::thread::hardware_concurrency()) : stop(false)
    {
        threads.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([this]
            {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty())
                        {
                            return;
                        }
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~thread_pool()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : threads)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>
    {
        using return_type = typename std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (stop)
            {
                throw std::runtime_error("enqueue on stopped thread_pool");
            }
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

  private:
    std::vector<std::thread>          threads;
    std::queue<std::function<void()>> tasks;
    std::mutex                        queue_mutex;
    std::condition_variable           condition;
    bool                              stop;
};

/**
 * @brief Context that manages a retained, global thread pool for operational domain simulations.
 */
class operational_domain_thread_pool_context
{
  public:
    explicit operational_domain_thread_pool_context(
        const std::size_t num_threads = std::max(std::size_t{std::thread::hardware_concurrency()}, std::size_t{1})) :
        pool(num_threads)
    {
        std::lock_guard<std::mutex> lock(global_pool_mutex);
        if (active_pool_instance != nullptr)
        {
            throw std::runtime_error("An operational_domain_thread_pool_context is already active.");
        }
        active_pool_instance = &pool;
    }

    ~operational_domain_thread_pool_context()
    {
        std::lock_guard<std::mutex> lock(global_pool_mutex);
        active_pool_instance = nullptr;
    }

    operational_domain_thread_pool_context(const operational_domain_thread_pool_context&) = delete;
    operational_domain_thread_pool_context& operator=(const operational_domain_thread_pool_context&) = delete;
    operational_domain_thread_pool_context(operational_domain_thread_pool_context&&) = delete;
    operational_domain_thread_pool_context& operator=(operational_domain_thread_pool_context&&) = delete;

    [[nodiscard]] static thread_pool* get_active_pool()
    {
        std::lock_guard<std::mutex> lock(global_pool_mutex);
        return active_pool_instance;
    }

  private:
    thread_pool pool;

    inline static thread_pool* active_pool_instance = nullptr;
    inline static std::mutex   global_pool_mutex{};
};

}