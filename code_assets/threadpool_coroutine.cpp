#include <coroutine>
#include <vector>
#include <thread>
#include <condition_variable>
#include <queue>
#include <functional>
#include <optional>
#include <future>
#include <print>
#include <atomic>

struct ThreadPool {
    struct promise_type {
        using OptionalTask = std::optional<std::move_only_function<void()>>;
        OptionalTask new_task_{};

        ThreadPool get_return_object() { return ThreadPool{*this}; }
        
        std::suspend_never initial_suspend() noexcept { return {}; }
        
        void unhandled_exception() noexcept {}
        
        std::suspend_always final_suspend() noexcept { return {}; }

        auto await_transform(OptionalTask) noexcept {
            struct awaiter {
                promise_type& promise;
                
                bool await_ready() const noexcept { return false; }
                
                OptionalTask await_resume() const noexcept {
                  return std::move(promise.new_task_);
                }
                
                void await_suspend(HandleType) const noexcept {}
            };

            return awaiter{*this};
        }
    };
    
    using HandleType = std::coroutine_handle<promise_type>;
    HandleType coro_handle_{};

    explicit ThreadPool(promise_type& prom)
      : coro_handle_{HandleType::from_promise(prom)} {}

    ~ThreadPool() {
        if (not coro_handle_.done()) coro_handle_.destroy();
    }

    //! not thread-safe: mostly because of coro_handle_.resume()
    template <typename Task, typename... Args>
    std::future<std::invoke_result_t<Task, Args...>>
    Async(Task&& task, Args&&... args) {
        using ReturnType = std::invoke_result_t<Task, Args...>;
        std::packaged_task<ReturnType()> package{std::bind(std::forward<Task>(task), std::forward<Args>(args)...)};
        auto fut = package.get_future();
        coro_handle_.promise().new_task_.emplace([task = std::move(package)]() mutable { task(); });
        if (not coro_handle_.done()) coro_handle_.resume();
        return fut;
    }

    //! not thread-safe
    void Wait() {
        coro_handle_.promise().new_task_.reset();
        if (not coro_handle_.done()) coro_handle_.resume();
    }
};

ThreadPool StartThreadPool(int n) {
    std::vector<std::jthread> workers;
    workers.reserve(n);
    std::mutex mut;
    std::condition_variable cond_var;
    bool stop{false};
    std::queue<std::move_only_function<void()>> tasks{};
    
    while (n--) {
        workers.emplace_back([&]{
            while (true) {
                std::unique_lock guard{mut};
                cond_var.wait(guard, [&]{ return stop || !tasks.empty(); });
                
                if (stop) break;

                if (tasks.empty()) continue;
                
                auto work = std::move(tasks.front());
                tasks.pop();
                
                guard.unlock();
                
                work();
            }
        });
    }

    auto wait_for_empty_queue{[&] {
        std::unique_lock guard{mut};
        while (not tasks.empty()) {
            guard.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            guard.lock();
        }
    }};

    while (true) {
        auto next_task = co_await std::optional<std::move_only_function<void()>>{};

        if (not next_task) {
            wait_for_empty_queue();
            stop = true;
            cond_var.notify_all();
            break;
        }

        {
            std::lock_guard guard{mut};
            tasks.push(std::move(*next_task));
        }

        cond_var.notify_one();
    }
}

void f(std::atomic<int>& global_sum, int i) {
    global_sum.fetch_add(i, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::microseconds{100}); // simulate a big-ish task
}

int main() {
    auto pool = StartThreadPool(2);

    std::atomic<int> global_sum{};

    std::jthread worker_one{[&] {
        for (int i = 0; i < 10000; ++i)
            auto fut = pool.Async(f, std::ref(global_sum), 1);
    }};

    worker_one.join();

    pool.Wait();

    std::println("Global Sum {} is correct? {}",
        global_sum.load(std::memory_order_relaxed),
        global_sum.load(std::memory_order_relaxed) == 10000);
    
    
    return 0;
}
