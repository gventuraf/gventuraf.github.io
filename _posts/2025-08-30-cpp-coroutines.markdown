---
layout: post
title:  C++ Coroutines — In Depth 🔎
date:   2025-08-30 21:23:34 +0100
author: Gonçalo Ferreira
tags: c++ coroutines deep-dive
permalink: posts/cpp-coroutines
---

<h2>Table of Contents</h2>
* Table of Contents (this line is necessary but text does not show)
{:toc}

## Some Quick Basics

This is probably not the first post you read about coroutines, so let's quickly run through the basics:
- A coroutine is basically a function that can be paused and resumed
- Any function containing one or more of the keywords `co_yield`, `co_await`, or `co_return` is considered a coroutine
- That's all, let's just jump in

## A Beautiful and Simple Coroutine - std::generator

Before diving deep, let's look at a very nice coroutine.
(If you don't think it's nice, wait until you see what's coming next.)

Don't worry about any details here, just look at how nice it is to write and use.

{% highlight cpp linenos %}
#include <print>
#include <generator> // since C++23

std::generator<int> fibonacci_numbers() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto next = a + b;
        a = b;
        b = next;
    }
}

int main() {
    auto fib = fibonacci_numbers();
    for (auto num : fib) { // infinite loop
        std::println("{}", num);
    }
    return 0;
}
{% endhighlight %}

First, we know `fibonacci_numbers()` is a coroutine because it has the `co_yield` keyword somewhere inside.

In coroutines lingo, it's called a **generator**.
Essentially, it's a coroutine that keeps outputting (yielding) values.

Something that might be confusing is line 15. It looks like a function call, but
remember that `fibonacci_numbers()` is not really a function - it's a coroutine.
In reality, that line is **lazy**, it does not execute any code inside `fibonacci_numbers()`,
it only returns a sort of _handle_ that we can use later to interact
with the coroutine (like we do in line 16).

Also, notice how we can use the `fib` handle as a range, pretty cool.

By the end of this post, you will know how to implement your own std::generator.

## Our First Coroutine

Let's start simple:

{% highlight cpp linenos %}
#include <print>

void f() {
    std::println("Hello coro");
    co_return;
}

int main() {
    std::println("Hello main");
    auto handle = f();
    return 0;
}
{% endhighlight %}

`f()` is a coroutine because it has the `co_return` keyword inside.
Now, if we try to compile it we get an error (with a nice message).

  ![basic_coro_compilation_error](/assets/basic_coro_compilation_error.png){: width="600" }

It tells us it cannot find the promise type for this coroutine.
It's not that obvious what this means. However, it's a crucial aspect of coroutines in C++.

**For coroutines to work, they must have a very specific return type**.

In specific, they must return a type that has a type named exactly `promise_type`.
Why? ~~Because yes.~~ Simply by definition: it's what the language asks from us.

{% highlight cpp linenos %}
#include <print>

struct MySpecialType {
  struct promise_type {};
};

MySpecialType f() {
    std::println("Hello coro");
    co_return;
}

int main() {
    std::println("Hello main");
    auto handle = f();
    return 0;
}
{% endhighlight %}

Let's compile it again. Now we get a slightly less pretty error message.
It complains about our promise type not implementing the methods:
- `get_return_object`
- `initial_suspend`
- `return_void`
- `unhandled_exception`
- `final_suspend`

Again, the compiler needs more from us: we must define these methods.
One big question is: what should their signature be?
Or maybe, first we should ask what their purpose is.

Let's discuss each one: (the line numbers mentioned refer to the code above)
- `get_return_object`: this is the method called when we call the coroutine in line 14.
The value of `handle` in line 14 will be whatever this method returns
- `initial_suspend`: this tells the compiler if it should immediately start executing
the coroutine code when it is called in line 14, or if it should not.
In our example above: it tells the compiler if the execution flow
should go to line 15 or to line 8
- `return_void`: this method gets called in line 9, when the coroutine returns.
When a coroutine returns `void`, the promise type must define this method.
If the coroutine does not return `void`, this method will be called `return_value` instead
(we will look at this case, too)
- `unhandled_exception`: this method is called if an exception is thrown and
not caught within the coroutine
- `final_suspend`: this method is called when the coroutine ends (line 10).
It tells the compiler wether it should destroy the coroutine frame or not

Just a quick note about the term **coroutine frame**. The _state_ of a coroutine lives
on the heap. Where _state_ is essentially what the stack frame of a normal function would be.

Our new code is:

{% highlight cpp linenos %}
#include <coroutine> // needed for std::suspend_always and std::suspend_never

struct MySpecialType {
    struct promise_type {
        MySpecialType get_return_object() { return MySpecialType{*this}; }
        void return_void() noexcept {}
        std::suspend_always initial_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {}
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    MySpecialType(promise_type&) {}
};

MySpecialType f() {
    std::println("Hello coro");
    co_return;
}

int main() {
    std::println("Hello main");
    auto handle = f();
    return 0;
}
{% endhighlight %}

This compiles, and the output is:
{% highlight txt %}
Hello main
{% endhighlight %}

Notice the return type of `get_return_object()`, that will be the type of `handle` in line 22.

Now let's address the (maybe) cryptic `std::suspend_always`. It's counterpart is `std::suspend_never`.
We can understand what they mean very easily by doing one small change.
Let's change the return type of the `initial_suspend()` method to `std::suspend_never`.
Now, if we run it again we get the output:

{% highlight txt %}
Hello main
Hello coro
{% endhighlight %}

With `std::suspend_never`, we are telling the compiler to start executing the coroutine
immediatelly when it is called (much like how it works with functions).

In other words, in this case: `std::suspend_never` gives the execution flow to the body of the coroutine.
While `std::suspend_always` keeps the flow where it is.

This might be a good time to address `final_suspend()`, since at least to me it is
not as easy to grasp as `inital_suspend()`.

As we mentioned before, `final_suspend()` is called when the coroutine ends, i.e falls out of scope
on line 18 in the code above. Since we are used to functions, we are used to thinking that when
the end of the scope is reached, everything in the function stack gets cleaned up _and that is that_.
The same is not entirely true to coroutines.

Remember that coroutines do not have a stack frame,
they have a **coroutine frame** that lives on the heap memory (not the stack memory).

Now it makes more sense that the compiler asks us for this `final_suspend()` method.
Since the coroutine frame lives on the heap, what should the compiler do with this frame
once the coroutine ends? Should it _free_ it (i.e deallocate the memory) or keep it alive?

If we want the coroutine frame to be kept alive, then we use `std::suspend_always`.
Otherwise, we use `std::suspend_never`.
This may not seem like a big deal now, and in our current code it actually is not.
But we will see later that it does matter (of course!).

For now, take a break, grab a coffee ☕️, and drink it while we appreciate how much
control the language gives us over our coroutine.

## Yielding a Value Out Of a Coroutine - co_yield

We now know the fundamentals. It's time to write a slightly cooler coroutine:

{% highlight cpp %}
MySpecialType f() {
    co_yield "Hello coro";
}
{% endhighlight %}

Now we don't just print, we **yield**. Meaning that we want the caller of the coroutine
to receive this value - much like when a function returns a value.

Also, note that we removed the co_return statement. Along with it, we also removed the
`return_void()` method (and the compiler did not complain).
`f()` is still a coroutine because it has the `co_yield` keyword.

Okay, let's try to compile:

  ![basic_coro_compilation_error](/assets/yield_coro_compilation_error.png){: width="600" }

We've dealt with this problem before.
Let's add this method to our promise type.

{% highlight cpp %}
  std::suspend_always yield_value(std::string) noexcept { return {}; }
{% endhighlight %}

This will be accepted by the compiler. Since we want to co_yield a `string`, we should have it be
the parameter of `yield_value()`. Lastly, with the return type we indicate wether after the `co_yield`
the coroutine continues to execute (`std::suspend_never`) or if it gives control back
to the caller (`std::suspend_always`). In our case, it will give back control.

The compiler will also accept other signatures, for example:
{% highlight cpp %}
  std::suspend_always yield_value(std::string_view) noexcept { return {}; }
  std::suspend_always yield_value(const char*) noexcept { return {}; }
{% endhighlight %}

If we compile again, it works.

But... How does the caller access the yielded value? Our type `MySpecialType` must
get it from the promise type.

{% highlight cpp %}
struct MySpecialType {
    struct promise_type {
      // ...
    };

    using HandleType = std::coroutine_handle<promise_type>;
    HandleType coro_handle_{};

    explicit MySpecialType(promise_type& prom)
      : coro_handle_{HandleType::from_promise(prom)} {}

    ~MySpecialType() {
        if (not coro_handle_.done()) coro_handle_.destroy();
    }
};
{% endhighlight %}

First, we must have some sort of handle in our `MySpecialType` that references
the _promise_. For our purposes, _coroutine promise_ and _coroutine frame_
are sort of the same thing: we can think of it as a pointer to the coroutine's data.
(In fact, `std::coroutine_handle<T>` is essentially a pointer to the coroutine frame).

Next, let's hold the yielded value in our promise type. Why? Because with our
`coro_handle_` we can get to the promise type - by doing `coro_handle_.promise().{field}`.

This is how our promise type looks now: (notice the field `out_` and the method `yield_value()`)

{% highlight cpp %}
struct promise_type {
    std::string out_;

    MySpecialType get_return_object() { return MySpecialType{*this}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    
    std::suspend_always yield_value(std::string val) noexcept {
        out_ = std::move(val);
        return {};
    }

    void unhandled_exception() noexcept {}
    std::suspend_always final_suspend() noexcept { return {}; }
};
{% endhighlight %}

Finally, let's add a method in `MySpecialType` to retrieve this `out_` value from the promise.
We can call this method anyting we want, I will call it `get()`.

The final code is this:

{% highlight cpp linenos %}
struct MySpecialType {
    struct promise_type {
        std::string out_;

        MySpecialType get_return_object() { return MySpecialType{*this}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        
        std::suspend_always yield_value(std::string val) noexcept {
            out_ = std::move(val);
            return {};
        }

        void unhandled_exception() noexcept {}
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    using HandleType = std::coroutine_handle<promise_type>;
    HandleType coro_handle_{};

    explicit MySpecialType(promise_type& prom)
      : coro_handle_{HandleType::from_promise(prom)} {}

    ~MySpecialType() {
        if (not coro_handle_.done()) coro_handle_.destroy();
    }

    std::string get() { // may have any name
        if (not coro_handle_.done()) coro_handle_.resume();
        return std::move(coro_handle_.promise().out_);
    }
};

MySpecialType f() {
    co_yield "Hello coro";
}

int main() {
    std::println("Hello main");
    auto handle = f();
    std::println("Coroutine says: {}", handle.get());
    return 0;
}
{% endhighlight %}

The output is:
{% highlight txt %}
Hello main
Coroutine says: Hello coro
{% endhighlight %}

An explanation is due about line 28.
Mainly, why do we call `resume` on the coroutine?
We actually already have all the knowledge to know why.
Let's go through it step by step:
  - we call the coroutine in line 39, but because `initial_suspend()` returns `std::suspend_always`
  this line is lazy - the flow of execution is kept by the main function
  - then we call `handle.get()` in line 40
  - now, we are in `get()`
    - if line 28 did not exist and we just return the value of `out_`, we would return an empty string,
    because the coroutine has not executed any of its body yet
  - but line 28 exists, so we resume the coroutine - in our case, we resume it for the first time, so
    in plain english it is more accurate to say we _start_ the coroutine
    - Now the flow of execution goes to the first instruction of the coroutine: line 34. This is the yield instruction, which
    will essentially call the `yield_value()` method in the promise type - initializing `out_`.
    - Moreover, since `yield_value()` returns a `std::suspend_always`, the coroutine gets paused, and the flow of
    execution goes back to the caller (line 29).
    - Now, when we read from `out_`, the value is actually there

I will take this chance to show you the importance of the return type of `final_suspend()`.
Let's imagine changing all `std::suspend_always` in the code above with `std::suspend_never`.
Now, when we call the coroutine in line 39, it will:
1. immediately start executing
2. when it hits the `co_yield` it will just keep on going to the next line
3. when it reaches the end of the scope in line 35 it will deallocate the coroutine frame in the heap

Essentially, **it will behave like a function**. So, what is the problem?
Well, now in line 40 when we call `handle.get()` to fetch the value, we will get
a **SEGMENTATION FAULT** - 2025 and still hitting these... 😑

Why? Because the coroutine frame no longer exists. A few paragraphs ago, we said that
`coro_handle_` is essentially a pointer to the coroutine frame. If this frame no longer exists,
we should not use `coro_handle_` anymore. And indeed the frame has been destroyed,
because our `final_suspend()` method returns `std::suspend_never`, which tell the coroutine not
to suspend/pause.

If we keep the coroutine frame alive, by only changing the return type of `final_suspend()` back to
`std::suspend_always`, it works again. Like so:

{% highlight cpp %}
std::suspend_never initial_suspend() noexcept {...}
std::suspend_never yield_value(std::string) noexcept {...}
std::suspend_always final_suspend() noexcept {...}
{% endhighlight %}

Now, the end result will be the same as before, **but the flow of execution will still be different** - 
as we have just seen.

We deserve another ☕️ break by now.


## Revisiting our Basic Coroutine - co_return

Before we move on to the next beast, let's quickly look at co_return again.
In our first example, the coroutine had a _print_ and simple empty `co_return` statement,
with a return type of `void`. This forced us to define the `return_void()` method
in our promise type.

What if we wanted to return something else? Let's try the following setup:

{% highlight cpp linenos %}
struct MySpecialType {
    struct promise_type {
        MySpecialType get_return_object() { return MySpecialType{*this}; }
        // void return_void() noexcept {}
        std::suspend_always initial_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {}
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    MySpecialType(promise_type&) {}
};

MySpecialType f() {
    std::println("Hello coro");
    co_return 10;
}

int main() {
    std::println("Hello main");
    auto handle = f();
    return 0;
}
{% endhighlight %}

If we compile, we get an error:

  ![coreturn_int_compile_error](/assets/coreturn_int_compile_error.png){: width="600" }

As we mentioned before, we need a method called `return_value` instead of `return_void`:
{% highlight cpp %}
void return_value(int) noexcept {}
{% endhighlight %}

The way we get this value out is identical to how we did it before with `co_yield`. We need a new `int` field
in our promise type, and a new method in our `MySpecialType` to read this value from the promise type.

Also, notice that the return type of `return_value` is just `void`. This is because after `co_return`
gets executed, the method `final_suspend` will be called to let the coroutine know if it should
suspend or not - so we do not need to specify this behavior in `return_value`.

Note that we used an `int`, but of course it can be any type.

The final code looks like this:

{% highlight cpp linenos %}
struct MySpecialType {
    struct promise_type {
        int out_;
        MySpecialType get_return_object() { return MySpecialType{*this}; }
        void return_value(int x) { out_ = x; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {}
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    using HandleType = std::coroutine_handle<promise_type>;
    HandleType coro_handle_{};

    explicit MySpecialType(promise_type& prom)
      : coro_handle_{HandleType::from_promise(prom)} {}

    ~MySpecialType() {
        if (not coro_handle_.done()) coro_handle_.destroy();
    }

    int get() {
        if (not coro_handle_.done()) coro_handle_.resume();
        return coro_handle_.promise().out_;
    }
};

MySpecialType f() {
    std::println("Hello coro");
    co_return 10;
}

int main() {
    std::println("Hello main");
    auto handle = f();
    std::println("Coroutine co_returned {}", handle.get());
    return 0;
}
{% endhighlight %}

And the output is:

{% highlight txt %}
Hello coro
Coroutine co_returned 10
{% endhighlight %}

Before we move on. Let's revisit our discussion about the importance of the return type of `final_suspend`.
Previously, we established that this method tells the compiler wether to deallocate the coroutine frame or not.
Let me add another, and the last piece of information, so we are fully aware of how things work.

Independently of the return type we choose for `final_suspend`, once the coroutine reaches the end of the scope,
all local variables it contains will be _destroyed_ - much like when a function ends.

The devil is in the details: only the state of the promise depends on the return value of `final_suspend`.
The local variables of the coroutine always get cleaned up at the end of the scope.

This is, more often than not, a technicality. But it is good to know. For example, now you know that even if you
have `std::suspend_always final_suspend() {...}`, you should not keep any pointers in your promise
to any local local variables in the finished coroutine.

## Getting a Value Into the Coroutine - co_await

Finally, we will talk about `co_await'.

As usual, let's just add a `co_await` to our code and see what the compiler tells us.

{% highlight cpp linenos %}
struct MySpecialType {
    struct promise_type {
        MySpecialType get_return_object() { return MySpecialType{*this}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {}
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    using HandleType = std::coroutine_handle<promise_type>;
    HandleType coro_handle_{};

    explicit MySpecialType(promise_type& prom)
      : coro_handle_{HandleType::from_promise(prom)} {}

    ~MySpecialType() {
        if (not coro_handle_.done()) coro_handle_.destroy();
    }
};

MySpecialType f() {
    auto in = co_await std::string{};
    std::println("Coroutine received {}", in):
}

int main() {
    std::println("Hello main");
    auto handle = f();
    return 0;
}
{% endhighlight %}

Compiling gives us the error:

  ![coawait_compile_error](/assets/coawait_compile_error.png){: width="600" }

This one is a bit more cryptic. It indicates that something is missing in the type `std::string` itself.
I was expecting something like:

{% highlight txt %}
missing method 'await_value' in promise_type
{% endhighlight %}

As it happens, the method we need to define in order to use `co_await` is slightly less nice than the others.
Let's go step by step. Indeed, we can solve this error by adding another method to our promise type:

{% highlight c++ %}
auto await_transform(auto) noexcept { ... }
{% endhighlight %}

But what should its signature be? This one is hard to guess. But we at least know 2 things:
- we want to be able to tell the compiler if before the `co_await` it should suspend always or never.
- we want to receive a std::string.

Before we go any further, let me assure you that if you think this is annoying you are not alone.
Why is _await_ not as simple as the other methods?
Well, let's write it the way we want to and try to spot the issue.

{% highlight c++ %}
std::suspend_always await_value(std::string s) noexcept {
  // what to do with 's' ?
  return {};
}
{% endhighlight %}

Remember that when the compiler sees `co_await` it will replace it by a call to our `await_value` method,
so it should return a `std::string`. But, if it returns a `std::string` it cannot return `std::suspend_always`.
This is the problem, we need more than a single function.

The proper way to do it is as follows:

{% highlight c++ %}
auto await_transform(std::string) noexcept {
    struct awaiter { // we can name this type whatever we want
        promise_type& promise;
        bool await_ready() const noexcept { return true; }
        std::string await_resume() const noexcept { ... }
        void await_suspend(HandleType) const noexcept {}
    };

    return awaiter{*this};
}
{% endhighlight %}

We do need all these methods to make `co_await_` work. Let's look at each one individually.

- `await_ready`: this method is called when the execution flow hits the `co_await` instruction.
If it returns `true`, the coroutine will not pause - i.e it will consider that the value it was awaiting for
is already ready to be consumed. If it returns `false', it will pause and give up the flow control.
(we will come back to this, don't worry 😉)
- `await_resume`: this method is called when the coroutine wants to fetch the value it is _awaiting_ for.
After it consumes the value, it keeps on executing.
- `await_suspend`: this method is only called if `await_ready` returns `false`. Its argument
is the handle to the coroutine. Its return type can be `void` or `std::coroutine_handle<>` -
this is very interesting and powerful, we will discuss it shortly.

Oh! By the way... We just implemented our own `std::suspend_never`/`std::suspend_always`.
Really! Check what the standard says about these types: [std::suspend_never][suspend_never]{:target="_blank"} 🔗
and [std::suspend_always][suspend_always]{:target="_blank"} 🔗 - note the 3 methods they define.
Also, note that the only difference between them is the return value of the method `await_ready`.

The standard library developers got nothing on us. 😎

Finally, we need to return a string from `await_resume`.
This requires communication between the handle and the coroutine. We have done this before.

The final code is below.
Notice the methods `MySpecialType::put` and `await_ready`, and what we do in `main()`.

{% highlight c++ linenos %}
struct MySpecialType {
    struct promise_type {
        std::string in_;

        MySpecialType get_return_object() { return MySpecialType{*this}; }
        std::suspend_always initial_suspend() noexcept { return {}; }

        auto await_transform(std::string) noexcept {
            struct awaiter {
                promise_type& promise;
                bool await_ready() const noexcept { return true; }
                std::string await_resume() const noexcept {
                  return std::move(promise.in_);
                }
                void await_suspend(HandleType) const noexcept {}
            };

            return awaiter{*this};
        }

        void unhandled_exception() noexcept {}
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    using HandleType = std::coroutine_handle<promise_type>;
    HandleType coro_handle_{};

    explicit MySpecialType(promise_type& prom)
      : coro_handle_{HandleType::from_promise(prom)} {}

    ~MySpecialType() {
        if (not coro_handle_.done()) coro_handle_.destroy();
    }

    void put(std::string s) { // can be named anything
        coro_handle_.promise().in_ = std::move(s);
        if (not coro_handle_.done()) coro_handle_.resume();
    }
};

MySpecialType f() {
    auto val = co_await std::string{};
    std::println("{}", val);
}

int main() {
    std::println("Hello main");
    auto handle = f();
    handle.put("Hello coro");
    return 0;
}
{% endhighlight %}

Let's make sure we really, really understand what is going on here.

Look at line 47: this is a lazy line, because `initial_suspend` returns `std::suspend_always`.
Look also at the method `await_ready`: we are returning true, which means that when the execution flow
reaches the `co_await` instruction it will not actually wait. It will immediately invoke `await_resume`
and continue executing -  so we must ensure we have already initialized `in_` when we reach the `co_await`.

This is why in `put()` we first initialize `in_`, and only then resume (in our case, start)
the coroutine.

That's pretty much it, elementary.

## Some More Details So You Can Have Even More Fun

- In one single coroutine, we can `co_yield` many types. All we have to do
is overload the method `yield_value`, for example:

{% highlight c++ %}
std::suspend_always yield_value(std::string) noexcept { ... }
std::suspend_always yield_value(std::vector) noexcept { ... }
{% endhighlight %}

- And the same goes to values we `co_await`, we just need to overload `await_transform`.

- In the method `await_suspend` inside `await_transform`, we can return two different types:
  - by returning `void`, we give control back to the caller
  - or we can return a handle to another coroutine - handle type is `std::coroutine_handle<>` -
  and now we pass control (i.e the execution flow) to that coroutine instead

- If your coroutines has parameters, you can also receive them in your promise type.
Just write a constructor that takes in the same parameter list.

## References

Videos on Youtube:
- [C++20’s Coroutines for Beginners - Andreas Fertig - CppCon 2022](https://www.youtube.com/watch?v=8sEe-4tig_A){:target="_blank"} 🔗
- [Deciphering C++ Coroutines - A Diagrammatic Coroutine Cheat Sheet - Andreas Weis - CppCon 2022](https://www.youtube.com/watch?v=J7fYddslH0Q){:target="_blank"} 🔗

Books:
- Chapter 8 of 'Asynchronous Programming with C++' by Javier Reguera-Salgado and Juan Antonio Rufes


[suspend_never]:  https://en.cppreference.com/w/cpp/coroutine/suspend_never.html
[suspend_always]: https://en.cppreference.com/w/cpp/coroutine/suspend_always.html
