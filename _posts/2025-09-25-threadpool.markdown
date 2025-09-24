---
layout: post
title: Writing a Threadpool (feat Coroutines) 🧵
date:   2025-09-25 00:14:02 +0100
author: Gonçalo Ferreira
tags: c++ threadpool coroutines multithreading
permalink: posts/writing-a-threadpool
---

<h2>Table of Contents</h2>
* Table of Contents (this line is necessary but text does not show)
{:toc}


## Intro

This post will present two implementations for a threadpool.

The first implementation is the usual way you would think to implement a threadpool (it being a class).

And the second way uses coroutines.


**What is a threadpool?**

Creating and killing threads for each task we need can be expensive.
A threadpool creates a certain number of threads once, and as tasks arrive
it assigns a task to an available thread.
Once a thread finishes a task, that thread is not killed - it picks up the next task
or, if there are no more tasks, goes to sleep.

**You should know...**

If you get lost with anything, I have 2 posts that pretty much cover everything this post mentions.

As an introduction to synchronization mechanisms like `std::future`, `std::async`, `std::mutex`,
`std::condition_variable`, and others, you can check this other post of mine:
[Synchronization Mechanisms for Multi-threading ᛘ in C++]({% post_url 2025-09-18-cpp-multithreading-utils %}){:target="_blank"} 🔗.

And if you want to know more about C++ coroutines, I have a post on that, too:
[C++ Coroutines — In Depth 🔎]({% post_url 2025-08-30-cpp-coroutines %}){:target="_blank"} 🔗.


## Use Case

Below is how we may want to use a threadpool, the API is simple.

We submit a task to the threadpool and that's it.

The task will eventually be executed by one of the threads in the pool.

Then, that thread will either do the next task or go to sleep (waiting for the next task).

{% highlight cpp %}
struct ThreadPool {
    ThreadPool(int number_of_threads);
    SubmitTask(...);
}

int main() {
    std::vector<Task> all_tasks = {...};
    ThreadPool pool{4}; // create 4 threads
    for (auto& task : all_tasks) pool.SubmitTask(task);
    return 0;
}
{% endhighlight %}



## Class Threadpool

The usual way to implement a threadpool is using a class.

I will be explaining and showing the code here, and you can also
[check it on github]({{ site.baseurl }}{% link /code_assets/threadpool_class.cpp %}){:target="_blank"} 🔗.

Here is the real API:

{% highlight cpp %}
class ThreadPool {
public:
    // initialize pool with n threads
    ThreadPool(int n);

    // submit task async
    template <typename Task, typename... Args>
    std::optional<std::future<std::invoke_result_t<Task, Args...>>>
    Async(Task&& task, Args&&... args);
};
{% endhighlight %}

The `Async()` method here is the `SubmitTask()` method from before.
I prefer this name, as it aligns with the standard's `std::async()` function
and it shows the tasks are being executed asynchronously (in parallel by another thread,
in this case).

The method must be templated, since a task may return a value and take in arguments.

The returned value is delivered to the caller using a `std::future`.

As an example, we'd use it like this:

{% highlight cpp %}
int sum_ends(std::vector<int>& v) {
    return v.size() >= 1 ? v.front() + v.back() : 0;
}

int main() {
    ThreadPool pool{4};
    std::vector<int> nums{1, 2, 3, 4, 5};
    auto future_result = pool.Async(sum_ends, std::cref(nums));
    std::println("Result is {}", future_result.get());
    return 0;
}
{% endhighlight %}

Okay, so how do we implement `Async()`? If you do a quick Google search for it,
you'll likely find [this github repo][github_threadpool_repo]{:target="_blank"} 🔗.

It has a very nice and concise implementation, and it was a big help to me.

Even though it's pretty old (12 years old, using C++11), a more modern implementation
doesn't get much better.

Having given credit to where credit is due, there is mainly one thing I would like
to improve on that implementation: I don't want to use `std::make_shared()`, and with C++23
it's *more possible*, easier.

### Async - Task Submission

Okay, let's implement `Async()`, then.

{% highlight cpp %}
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
{% endhighlight %}

First, we package the task, perfectly forwarding the callable and its arguments.

Second, we get the future to return to the caller, so it can retrieve the value later at some point.

Third, we want to add the new task to the queue of tasks `tasks_`. For that we must first acquire
a lock (this will be obvious soon), and only then we can push the task into the queue.

Fourthly, we notify a thread, any thread that may be sleeping waiting for work, we wake it up.

Finally, we return the future to the caller.

<br>

I should now introduce the private fields of this class: `tasks_`, `mut_`, `cond_var_`, and the others.

{% highlight cpp %}
std::vector<std::jthread> threads_;
std::mutex mut_;
std::condition_variable cond_var_;
bool stop_{false};
std::queue<std::move_only_function<void()>> tasks_{}; // a simple std::function will not work
{% endhighlight %}

I won't discuss things like `std::mutex` and `std::condition_variable` here, you can check my other post for that.

Something that might've caught your attention is:

**Why was the lambda we pushed to `tasks_` marked as mutable?**, And

**Why isn't `tasks_` just a container of `std::function`?**

Well... Initially it wasn't like this, but also initially, it wouldn't compile.

The reason is interesting - if you're a nerd, of course.

First, the lambda is marked as `mutable` because when we call `t()` inside the lambda,
the `operator()` of `std::packaged_task` is not marked as a `const` method, so we need `mutable`
in order to "modify" `t`, i.e to call it.

And we must use `std::move_only_function` again because of `std::packaged_task`: since it is *move-only*.
Meaning that it cannot be copied, but a `std::function` can be copied... Hence the problem.

The solution is `std::move_only_function` introduced in C++23. As the name implies, an object of this type
can only be moved, and not copied. So, now it aligns nicely with the requirements for a `std::packaged_task`.

<br>

Also, notice how we store the tasks: we **reduce** them to a function that takes in no arguments and has no return value.

This is necessary, otherwise how would we store different types in the same container and retrieve them back?

That is it for how we submit tasks. Pretty neat and simple.


### Thread Management - keeping them alive

Now, how do we manage the threads?

The threads/workers are always alive and if there is no work then they are sleeping.

First, the constructor:

{% highlight cpp %}
ThreadPool(int n) {
    threads_.reserve(n);
    while (n--) threads_.emplace_back(&ThreadPool::Worker, this);
}
{% endhighlight %}

We spawn `n` threads, and this is what they're doing:

{% highlight cpp linenos %}
// private method
void Worker() {
    while (true) {
        std::unique_lock guard{mut_};

        // gives up the lock before sleeping
        cond_var_.wait(guard, [this]{ return stop_ || !tasks_.empty(); });
        
        // thread woke up: at this point, we own the lock again
        
        if (stop_) break;
        
        // I *think* we should check again here now that we own the lock
        if (tasks_.empty()) continue;
        
        auto task = std::move(tasks_.front());
        tasks_.pop();
        
        guard.unlock(); // unlock it before executing the task, so other threads can work
        
        task();
    }
}
{% endhighlight %}

As I've been saying, if there's no work, they go to sleep (line 7).

There's not much to the `stop_`, it's just a boolean, as we've seen, to indicate threads should stop
and be ready to die 🌹.

As line 7 indicates, if we should stop or if the queue is not empty, then the thread does not go to sleep
and advances to line 8.

Then, in line 14 I check again if the queue is empty. I'm not sure if it's necessary, but consider the scenario:
two threads, A and B, wake up from line 7 because there is one task is the queue. Now, they both want to go to line 8, but first
they must acquire the same mutex.

If thread A gets the mutex first, it will take the task first, too. Only after thread A releases the mutex will thread B
acquire it, but at this point there is no task in the queue anymore - hence why I check again.

Finally, the thread unlocks the mutex before executing the task.

It could not unlock it before, since `tasks_` is shared with other threads and it could cause a race condition.

Again, for the full version,
[check github]({{ site.baseurl }}{% link /code_assets/threadpool_class.cpp %}){:target="_blank"} 🔗.


## Threadpool - Coroutines Style

Guess what... The code is
[on github]({{ site.baseurl }}{% link /code_assets/threadpool_coroutine.cpp %}){:target="_blank"} 🔗.

Everything is very similar, so there's not much more to cover.

The usage is like this:

{% highlight cpp %}
struct ThreadPool;

ThreadPool StartThreadPool(int number_of_threads);

int main() {
    std::vector<Task> all_tasks = {...};
    auto pool = StartThreadPool(4); // create 4 threads
    for (auto& task : all_tasks) pool.SubmitTask(task);
    return 0;
}
{% endhighlight %}

Pretty much the same as before.

The coroutine itself, `StartThreadPool` is very nice to read.

Moat of the complexity is hidden away in its return type, `ThreadPool`.

Here is the coroutine:

{% highlight cpp %}
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

    while (true) {
        auto next_task = co_await std::optional<std::move_only_function<void()>>{};

        if (not next_task) {
            // exit...
        }

        {
            std::lock_guard guard{mut};
            tasks.push(std::move(*next_task));
        }

        cond_var.notify_one();
    }
}
{% endhighlight %}

Very similar in concept to what we've seen.

I won't show the implementation of the `ThreadPool` type here, since it's not as pretty.



## Quick Review

As cool as coroutines are, I prefer the *typical* implementation using a class.
In this case, it just looks simpler and more manageable.

Maybe someone out there can implement the coroutine better and simpler.
Do let me know if you do (maybe open an issue on my github or something, or
maybe by the time you're reading this I will have linked my twitter here).

One last note, and this one **really makes a difference**.

The 'Async()' method is thread-safe. Meaning that multiple threads can enqueue
tasks simultaneously without a problem.

Meanwhile, the `StartThreadPool()` coroutine is not thread safe (this lies
in the implementation of its return type).

Of course, we could make the coroutine thread-safe as well, but that's extra effort.
With the class-style, it just comes by default, pretty much.

Alright, get out of here.


<h2>References</h2>

[github_threadpool_repo]: https://github.com/progschj/ThreadPool
