#include <print>
#include <vector>
#include <thread>
#include <future>
#include <any>
#include <stop_token>
#include <any>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <queue>

class ThreadPool {
public:
    ThreadPool(int n) {
        threads_.reserve(n);
        while(n--) {
            threads_.emplace_back(&ThreadPool::Worker, this);
        }
    }

    //! not thread safe itself
    template <typename Task, typename... Args>
    std::future<std::invoke_result_t<Task, Args...>>
    Async(Task&& task, Args&&... args) {
        using ReturnType = std::invoke_result_t<Task, Args...>;

        std::packaged_task<ReturnType()> package{std::bind(std::forward<Task>(task), std::forward<Args...>(args...))};
        
        auto fut = package.get_future();

        {
            std::lock_guard guard{mut_};
            tasks_.push([t = std::move(package)]() mutable { return t(); }); // must be mutable
        }

        cond_var_.notify_one();
        
        return fut;
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

int f(int i) {
    return i + 5;
}

int main() {
    ThreadPool thread_pool{2};
    auto fut = thread_pool.Async(f, 15);
    std::println("result is {}", fut.get());

    return 0;
}
