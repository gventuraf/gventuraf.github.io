#include <print>
#include <vector>
#include <thread>
#include <future>
#include <stop_token>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <queue>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(int n) {
        threads_.reserve(n);
        while (n--) threads_.emplace_back(&ThreadPool::Worker, this);
    }

    // This method is thread-safe: multiple threads can call it simultaneously
    template <typename Task, typename... Args>
    std::future<std::invoke_result_t<Task, Args...>>
    Async(Task&& task, Args&&... args) {
        using ReturnType = std::invoke_result_t<Task, Args...>;

        std::packaged_task<ReturnType()> package{std::bind(std::forward<Task>(task), std::forward<Args>(args)...)};

        auto fut = package.get_future();

        {
            std::lock_guard guard{mut_};
            tasks_.push([t = std::move(package)]() mutable { t(); }); // must be mutable because 't' is const
        }

        cond_var_.notify_one();

        return fut;
    }

    void Wait() {
        std::unique_lock guard{mut_};
        while (not tasks_.empty()) {
            guard.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds{1}); // whatever... be nice
            guard.lock();
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard guard{mut_};
            stop_ = true;
        }
        cond_var_.notify_all();
    }

private:
    void Worker() {
        while (true) {
            std::unique_lock guard{mut_};
            cond_var_.wait(guard, [this]{ return stop_ || !tasks_.empty(); });
            // at this point, we own the lock again
            if (stop_) break;
            if (tasks_.empty()) continue; // I *think* we should check again here now that we own the lock
            auto task = std::move(tasks_.front());
            tasks_.pop();
            guard.unlock(); // unlock it before executing the task, so other threads can work
            task();
        }
    }

private:
    std::vector<std::jthread> threads_;
    std::mutex mut_;
    std::condition_variable cond_var_;
    bool stop_{false};
    std::queue<std::move_only_function<void()>> tasks_{}; // a simple std::function will not work
};


void f(std::atomic<int>& global_sum, int i) {
    global_sum.fetch_add(i, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::microseconds{100}); // simulate a big-ish task
}

int main() {
    ThreadPool pool{2};

    std::atomic<int> global_sum{};

    std::jthread worker_one{[&] {
        for (int i = 0; i < 5000; ++i) auto fut = pool.Async(f, std::ref(global_sum), 1);
    }};
    std::jthread worker_two{[&] {
        for (int i = 0; i < 5000; ++i) auto fut = pool.Async(f, std::ref(global_sum), 1);
    }};

    worker_one.join();
    worker_two.join();

    pool.Wait();
    
    std::println("Global Sum {} is correct? {}",
        global_sum.load(std::memory_order_relaxed),
        global_sum.load(std::memory_order_relaxed) == 10000);

    return 0;
}
