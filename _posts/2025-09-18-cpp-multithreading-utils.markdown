---
layout: post
title: Synchronization Mechanisms for Multi-threading ᛘ in C++
date:   2025-09-18 23:01:16 +0100
author: Gonçalo Ferreira
tags: c++ multithreading
permalink: posts/cpp-multithreading-mechanisms
---

<h2>Table of Contents</h2>
* Table of Contents (this line is necessary but text does not show)
{:toc}

## Intro

This post is an overview with some examples of the synchronization mechanisms
available in the standard library for multi-threaded programs.

Synchronization mechanisms are needed usually because there is
data shared across threads that are reading/writing from/to it.

<!--Another scenario why they might be needed is that we need all threads
to be in a certain state before any thread can proceed.-->

Just a quick note: some of these synchronizations mechanisms are a bit redundant.
In the sense that usually we can always find a way to use any mechanism in place of any other.
However, as most things are, usually we should choose the mechanism that is most appropriate for
the job at hand -- **we should choose the one that more clearly states our intent**.

## std::latch

Introduced in C++20.

You might use it when you want a group of threads to wait until a certain
**number** of tasks has been completed.

Essentially, a `std::latch` is a thread-safe countdown to zero.

In the example below we want to start our <ins>Game</ins>, which depends on
<ins>Graphics</ins>, <ins>Sound</ins>, and <ins>InOut</ins>.
For the game to work, these dependencies must always be running.
Also, before the game can start, these dependencies must first be initialized.

We can express this very nicely with a `std::latch`:

{% highlight cpp %}
#include <print>
#include <latch>
#include <thread>

enum class Task { Game, Graphics, Inout, Sound };

void RunTask(Task task, std::latch& countdown) {
    if (task == Task::Game) {
        countdown.wait();
        std::println("Game: starting");
    } else {
        // Init(task);
        std::println("Task {} has been initialized", static_cast<int>(task));
        countdown.arrive_and_wait();
    }
    // DoTask(task);
}

int main() {
    constexpr int dependencies_count{3};
    std::latch countdown{dependencies_count};

    std::jthread game{RunTask, Task::Game, std::ref(countdown)};

    std::jthread grahics{RunTask, Task::Graphics, std::ref(countdown)};
    std::jthread sound{RunTask, Task::Sound, std::ref(countdown)};
    std::jthread inout{RunTask, Task::Inout, std::ref(countdown)};

    return 0;
}
{% endhighlight %}

Below is a possible output. Note that the first three lines may show
in any order, but they will always appear before *"Game: starting"*.

{% highlight tex %}
Task 2 has been initialized
Task 3 has been initialized
Task 1 has been initialized
Game: starting
{% endhighlight %}

A key point is that after `countdown` reaches zero, it cannot be used again.
It does not reset: a `std::latch` is single-use.

<!--
GCC uses atomics on the internal implementation of `std::latch`.
As far as I could tell, it is lock-free: using a spin-lock for the *waits*.
-->

## std::barrier

Introduced in C++20.

This is essentially the same as the `std::latch`. The difference lies in the fact
that when the counter reaches zero (i.e all waiting threads can proceed), it is
**automatically reset** to its original value.

At the time of construction you can also give it a callback (i.e some function)
that will execute every time the counter reaches zero, but **before** the waiting
threads are unblocked.

Keeping the *game* theme. Let's say our game is multi-player and that
for each player we have one thread.

The game can only advance to the next level `N+1` when all players have completed level `N`.
In other words, once a player finishes a level, he must also wait for all other players
to finish that same level.

{% highlight cpp %}
#include <print>
#include <barrier>
#include <thread>

void LevelOne() {}
void LevelTwo() {}
void LevelThree() {}

template <typename Barrier>
void Player(Barrier& sync_point, int player_id) {
    std::println("Player {}: starting level", player_id);
    LevelOne();
    sync_point.arrive_and_wait();
    
    std::println("Player {}: starting level", player_id);
    LevelTwo();
    sync_point.arrive_and_wait();
    
    std::println("Player {}: starting level", player_id);
    LevelThree();
    sync_point.arrive_and_wait();
}

int main() {
    constexpr int players_count = 3;

    std::barrier sync_point{players_count, []() {
        static int level = 1;
        std::println("All players have completed level {}", level);
        ++level;
        std::println("Starting level {}", level);
    }};

    using BarrierType = decltype(sync_point);
    std::jthread player_one{Player<BarrierType>, std::ref(sync_point), 1};
    std::jthread player_two{Player<BarrierType>, std::ref(sync_point), 2};
    std::jthread player_three{Player<BarrierType>, std::ref(sync_point), 3};

    return 0;
}
{% endhighlight %}

Again, the order in which the players finish each level is random.

Below is one possible output.

{% highlight txt %}
Player 1: starting level
Player 3: starting level
Player 2: starting level
All players have completed level 1
Starting level 2
Player 2: starting level
Player 1: starting level
Player 3: starting level
All players have completed level 2
Starting level 3
Player 3: starting level
Player 1: starting level
Player 2: starting level
All players have completed level 3
Starting level 4
{% endhighlight %}

See how a player never starts a level if all the other players are not ready yet.

Notice how the `std::barrier` resets itself, `std::latch` doesn't do that.

<!--
Under the hood, this type is a bit more complex than `std::latch`. But from a quick scan
it looks like it follows the same principle of being lock-free.
-->

## \<semaphore\>

Introduced in C++20.

You can use a `semaphore` to restrict access to a shared resource to
a certain amount of threads.

In other words, a `semaphore` only allows up to `N` threads at a time
to acquire a certain resource.

As an example, let's say that in level 1 of our game only one player will be
able to acquire the *Sword of the Brave*. He must fight another player for it,
and only two players will get this chance.

It's a bit of a contrived example, but here it is:

{% highlight cpp %}
#include <print>
#include <semaphore>
#include <thread>

template <int N>
void LevelOne(std::counting_semaphore<N>& sem, bool& is_sword_captured, int player_id) {
    auto is_mine = sem.try_acquire();
    if (is_mine) std::println("Player {} is fighting for the sword", player_id);
    else std::println("Player {} could not fight", player_id);
    // sem.release(); usually, you'd release the semaphore after use
}

int main() {
    constexpr int max_count = 2;
    std::counting_semaphore<max_count> sem{max_count};

    bool is_sword_captured = false;

    std::jthread player_one{LevelOne<max_count>, std::ref(sem), std::ref(is_sword_captured), 1};
    std::jthread player_two{LevelOne<max_count>, std::ref(sem), std::ref(is_sword_captured), 2};
    std::jthread player_three{LevelOne<max_count>, std::ref(sem), std::ref(is_sword_captured), 3};

    return 0;
}
{% endhighlight %}

And a possible output is below. Only two players get to fight for the sword.

{% highlight txt %}
Player 2 is fighting for the sword
Player 3 could not fight
Player 1 is fighting for the sword
{% endhighlight %}

Notice how we must specify the maximum count of the semaphore as a template parameter.

Also, in its constructor we must specify the starting value;

There is also a type called `std::binary_semaphore`, defined as

{% highlight cpp %}
using binary_semaphore = counting_semaphore<1>;
{% endhighlight %}

## \<mutex\>

Introduced in C++11.

This is a classic.

The standard actually has a few types of mutexes:
- `std::mutex`
- `std::timed_mutex`
- `std::recursive_mutex`
- `std::recursive_timed_mutex`
- `std::shared_mutex`
- `std::shared_timed_mutex`

First, a `std::mutex` creates a **critical section** between its calls to `lock()` and `unlock()`.
This means that only one thread at a time can be executing code in that critical section. In fact,
it's more powerful than that: out of all the possible critical sections defined by the same mutex,
only one can *active* at any given time.

A `std::timed_mutex` is simple mutex with the added feature that if we cannot acquire it
for a certain amount of time, we give up on trying.

A `std::recursive_mutex` is also just a basic mutex with an added feature.
First, you need to know that with a normal `std::mutex`, if a thread trys to lock it
while it already owns it, that is undefined behavior (UB).

The only benefit of a `std::recursive_mutex` over a `std::mutex` is that the same thread
can repeatedly lock a mutex, while it owns it, without it being UB.

A `std::shared_mutex` is interesting. It almost deviates from the true meaning of what a mutex should be.
Essentially, it allows for many threads to own the mutex at the same time.

It has two modes: exclusive and shared.
If a thread owns the mutex by having called `lock()`, then no other thread can acquire it.

However, if a thread owns the mutex by having called `lock_shared()`, then other threads that
call `lock_shared()` will also own the mutex. But, threads that call `lock()` will block, since
they are requesting exclusive ownership.

The `<mutex>` header also provides some utility classes like `std::lock_guard`, `std::scoped_lock`,
and `std::call_once`.

`std::lock_guard` is the RAII-style way of locking and unlocking a mutex,
it's preferred to doing it manually: because the destructor will always
unlock the mutex, while manually we might *forget*.

You don't think you will forget? Fair enough, but...
Hope you called your mom today.

{% highlight cpp %}
bool Check() {
    mut.lock();
    if (!HasCalledMomToday()) {
        ShameOnYou();
        return false;
    }
    mut.unlock();
    return true;
}
{% endhighlight %}

`std::scoped_lock` is used when we have multiple mutexes and want to acquire them all.
If we do not do this carefully we may get a **deadlock**, this function prevents that.
However, have you heard of a **livelock**?

`std::call_once(std::once_flag, func)` is very aptly named. Given the same *flag* object
(first parameter), the callable *func* will only ever execute once in the entire program.
With the little caveat that if *func* is called and it fails via an exception,
then it does not count as having been called -- and it can be called again.

### mutex vs binary semaphore
They are pretty much equivalent in behavior, but there is one interesting difference:
a mutex can only be unlocked by the thread which locked it. Conversely, a semaphore
can be unlocked/released by any thread -- even one that did not lock it -- since it works
on the basis of a counter.

In a sense, a mutex is aware of its owning thread, while a semaphore is not.

And the same applies for a `std::shared_mutex` and a `std::counting_semaphore`.

<!--
Essentially, it's the same as a `binary semaphore`.

A thread tries to **own** the mutex by calling its `lock()` method: proceeding if it success,
and blocking if it fails.

A thread will only fail to acquire a mutex if another thread owns it.

In the example below, there is a checkpoint in level one, and only one player can be at that checkpoint
at any given time, the others must wait.

{% highlight cpp %}
#include <print>
#include <thread>
#include <mutex>

int counter_checkpoint{0};

void LevelOne(int player_id, std::mutex& checkpoint) {
    // play ...
    
    checkpoint.lock();
        // only one player is here at a time
        // this is called a Critical Section
        ++counter_checkpoint;
        std::println("Player {} has completed the checkpoint", player_id);
    checkpoint.unlock();

    // continue playing ...
}

int main() {
    int x = 0;
    std::mutex checkpoint;
    std::jthread player_one(LevelOne, 1, std::ref(checkpoint));
    std::jthread player_two(LevelOne, 2, std::ref(checkpoint));
    return 0;
}
{% endhighlight %}

And a possible output is:

{% highlight tex %}
Player 1 has completed the checkpoint
Player 2 has completed the checkpoint
{% endhighlight %}

As in this example, it is usual for a mutex to guard a specific variable that is shared among threads --
in this case it's `counter_checkpoint`.

This relationship between the mutex and the variable that it is *supposed* to guard is not enforced by the compiler.
In general, we'd want these important relationships to be more explicit.

One last note: doing `lock()` and `unlock()` is not the best practice, it is not RAII-style and can get cumbersome.

Usually, you should always use `std::lock_guard`, as we do in the code below.
Note how we create a new scope in the second approach. Once our `guard` object falls out of scope
in the last line, its destructor will unlock the mutex -- so we don't have to worry about it.

{% highlight cpp %}
my_mutex.lock()
// ...
my_mutex.unlock();

// is equivalent to:

{
std::lock_guard guard(my_mutex);
// ...
}
{% endhighlight %}
-->

## \<condition_variable\>

Introduced in C++11.

A `std::condition_variable` can be used by thread A to notify the sleeping thread B
that some task (i.e *condition*) has been completed -- `notify_one()` method.

In fact, thread A can notify many threads at the same time -- `notify_all()` method.

A `std::condition_variable` must be associated with a `std::mutex`.
Moreover, when calling any *wait* method on the condition variable the thread
**must** own the mutex.

As for the example, imagine the scenario where players 1 and 2 have to cooperate in order to
complete a level by going back and forth completing tasks between themselves,
and while one is doing a task, the other must wait until its finished.

{% highlight cpp %}
#include <print>
#include <thread>
#include <mutex>
#include <condition_variable>

void PlayerOne(std::condition_variable& cv, std::mutex& mut, bool& is_turn_one) {
    std::unique_lock guard{mut}; // implicitly locks the mutex
    
    std::println("#1: player 1 is working");
    
    // player 1 has finished his task, notify player 2
    is_turn_one = false; // change the var for player 2
    cv.notify_one();

    cv.wait(guard, [&]{ return is_turn_one; }); // wait for player 2 to finish his task

    std::println("#3: player 1 is working");

    // unlock the mutex before notifying,
    // or else player 2 might wake up and go back to sleep because it does not own the lock
    // causing a deadlock
    guard.unlock();
    
    is_turn_one = false;
    cv.notify_one();
}

void PlayerTwo(std::condition_variable& cv, std::mutex& mut, bool& is_turn_one) {
    std::unique_lock guard{mut}; // implicitly locks the mutex

    // the mutex gets unlocked while we wait
    cv.wait(guard, [&]{ return !is_turn_one; }); // wait until it is player 2's turn
    
    std::println("#2: player 2 is working");
    
    // player 2 has finished his task, notify player 1
    is_turn_one = true;
    cv.notify_all(); // can also use notify_all(), no difference in this case

    // wait for player 1 to finish
    cv.wait(guard, [&]{ return !is_turn_one; });

    std::println("#4: player 2 is working");
}

int main() {
    std::condition_variable cv;
    std::mutex mut;
    bool is_turn_of_player_one = true;

    std::jthread player_one{PlayerOne, std::ref(cv), std::ref(mut), std::ref(is_turn_of_player_one)};
    std::jthread player_two{PlayerTwo, std::ref(cv), std::ref(mut), std::ref(is_turn_of_player_one)};

    return 0;
}
{% endhighlight %}

The output will always be the same, as we have an order:

{% highlight txt %}
#1: player 1 is working
#2: player 2 is working
#3: player 1 is working
#4: player 2 is working
{% endhighlight %}

Note why we provide a predicate for the `wait()` method: **the condition variable
may wake up *spuriously*,** i.e without being notified. To prevent this from
causing wrong behavior, we ensure the thread can only advance when the
predicate returns *true*.

## \<stop_token\>

Introduced in C++20.

The stars of this header are `std::stop_token` and `std::stop_source`
They work together and interact with a `std::jthread`.

In essence, these *stop* mechanisms are way of **managing and controlling
the state** of a `std::jthread`.

They don't work with `std::thread`. More precisely, `std::thread`s do not work with them.

Hopefully by now you're as sick of the game examples as I am.
Just kidding...

Imagine player 1 has the ability to kill player 2.
It will actually kill the thread responsible for player 2, very brutal.

{% highlight cpp linenos %}
#include <print>
#include <thread>
#include <stop_token>

void PlayerOne(std::stop_source player_two_state_controller) {
    // sleep for a bit to ensure player 2's thread is already alive
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    std::println("Player 1 is about to kill player 2");
    player_two_state_controller.request_stop();
}

void PlayerTwo(std::stop_token state_manager) {
    while (!state_manager.stop_requested()) {
        // play the game, have fun
    }
    std::println("Player 2 has died");
    return;
}

int main() {
    std::jthread player_two{PlayerTwo};

    std::jthread player_one{PlayerOne, player_two.get_stop_source()};

    return 0;
}
{% endhighlight %}

The output is:

{% highlight txt %}
Player 1 is about to kill player 2
Player 2 has died
{% endhighlight %}

Notice how we pass no parameters to player 2 in line 21, but the function
actually receives a `std::stop_token`.

And note line 23, how we pass player 2 thread's `std::stop_source' object
to player 1, so it can request player 2 to stop.

However, the stop request only works if player 2 is actively listening to it,
like we do in line 13.


## \<future\>

Introduced in C++11.

This library mainly offers a nice and easy way of performing asynchronous operations.

Indeed, the star is the function `std::async()`.

Say that you have a function, with the signature `std::optional<int> f(std::vector<int>&, int)`,
that you want to execute right now, but you don't want to wait for it to finish.

The (not so good) solution is to launch it in a separate thread and fetch the result later when you want.
But how would you fetch the result? There are ways, but they're not very clean.

*Enters* `std::async()`:

{% highlight cpp %}
std::vector<int> data{1, 2, 3, 4, 5};
auto result = std::async(f, std::ref(data), 10);
{% endhighlight %}

Now the result is stored in `result`. And what is it exactly?

It is not simply a `std::optional<int>` like our function `f` returns.

It is a *future* that wraps our desired type: `std::future<std::optional<int>>`.

By calling the `get()` method of this *future* -- i.e `result.get()` we can retrieve
our `std::optional<int>`.

Note that `get()` will block if the result is not yet available. In other words,
if `f()` has not finished executing, then `get()` will block and wait until it does.

One more piece of information about `std::async()` is that you can also pass as
the first parameter either `std::launch::deferred` or `std::launch::async` (or both using `|`).

These are *launch policies*:
- `async` will likely (but not necessarily) execute the task in another thread
- `deferred` will not do anything until you call `get()` -- only then it will execute,
and it will be on the same thread

In essence, a `std::future` is the object that you await on to get an async result.

You can also get a `std::future` object from these two classes: `std::packaged_task` and `std::promise`.

If you call `set_value(x)` on a `std::promise`, it will make the value `x` available in the
corresponding *future*.

Similarly, when you **invoke** your `std::packaged_task` object, the return value will be available in
its corresponding *future*.

A cool thing about `std::packaged_task` is that it appropriately forwards everything you give it:
*lvalues* or *rvalues*, we don't care. It just works.

Consider this very real example (that was not sarcasm):

{% highlight cpp %}
template <typename Task, typename... Args>
std::future<std::invoke_result_t<Task, Args...>>
MyAsync(Task&& task, Args&&... args)
{
    using ReturnType = std::invoke_result_t<Task, Args...>;
    std::packaged_task<ReturnType()> package{std::bind(std::forward<Task>(task), std::forward<Args...>(args...))};
    // tell another thread to call package();
    return package.get_future();
}
{% endhighlight %}

That snippet of code was actually part of the code for a **thread pool**. I was going
to put a simple thread pool in this section. But making one actually came with an
interesting set of its own problems. So... It will be a separate post (I will
link it here once it's done).

~~Since somehow you're reading this so early that I haven't made the actual post (thank you,
by the way!), I will leave the link here to the current (maybe working)
version of [my threadpool][my_threadpool]{:target="_blank"} 🔗.~~

Here is the post: [Writing a Threadpool (feat Coroutines) 🧵]({% post_url 2025-09-25-threadpool %}){:target="_blank"} 🔗.

## \<atomic\> & Memory Model

Introduced in C++11.

We can leverage atomics and their memory order to synchronize between threads.

This is the realm of `wait-free` and `lock-free` programming. Very cool and very fast.

I plan to make a very in-dept post about this topic.

It should exist by end of October 2025. I will put the link here once it's done.

## \<coroutine\>

Check my other post [C++ Coroutines — In Depth 🔎]({% post_url 2025-08-30-cpp-coroutines %}){:target="_blank"} 🔗.

<h2>References</h2>

[C++ Multithreading Support Libraries][cpp_reference_multithread]{:target="_blank"} 🔗

[cpp_reference_multithread]: https://cppreference.com/w/cpp/atomic.html
[my_threadpool]: https://github.com/gventuraf/gventuraf.github.io/tree/main/vip_early_access/threadpool_experimental.cpp