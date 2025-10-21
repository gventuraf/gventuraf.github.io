---
layout: post
title: Introduction to Type Erasure - Simple Implementation of std::function and std::any
date:   2025-10-21 21:01:01 +0100
author: Gonçalo Ferreira
tags: c++ type-erasure
permalink: posts/type-erasure-intro
---

<h2>Table of Contents</h2>
* Table of Contents (this line is necessary but text does not show)
{:toc}

## Understanding Type Erasure

Type Erasure is way simpler than you might think.

The small example below showcases it perfectly.

{% highlight cpp %}
struct Concept {
    virtual ~Concept() = default;
};

template <typename T>
struct Model : Concept {
    T obj;
};

std::vector<Concept*> container;
{% endhighlight %}

Notice that `container` can store any class derived from `Concept`.
That's just simple polymorphism.

The key aspect is that `Model` is a class template. So...

We can have many derived classes, e.g:
- `Model<int>`
- `Model<std::pair<int, double>>`
- `Model<std::vector<int>>`

All of these are perfectly valid and could be an element in `container`.


## What is the point of Type Erasure?

The "point" is that it's useful.

Type Erasure is the technique used to implement `std::any` and `std::function`.

if those two classes are useful, then, by extension, the mechanisms used to implement
them must be useful too.

If you don't already know type erasure, any description of it will likely just confuse you more.
So, I will focus on practical examples.

Nevertheless, here are some definitions from the Internet:
- *Type erasure is the load-time process by which explicit type annotations are removed
from a program, before it is executed at run-time* - [Wikipedia][def_wiki_type_erasure]{:target="_blank"} 🔗

- *Type erasure can be explained as the process of enforcing type constraints only at compile time and
discarding the element type information at runtime.* - [Baeldung][def_baeldung_type_erasure]{:target="_blank"} 🔗

- *I have always understood the term type erasure to refer to situations in which type information which is
available at compile time becomes unavailable at run time.* - [Some post on a Rust's forum][def_rust_type_erasure]{:target="_blank"} 🔗

## How Easy is it to Implement `std::function`?

Without caring much about the code, let's think about it.

An object of type `std::function` can store any callable of a certain signature.

That signature is defined when the object is declared. So, if we have:

{% highlight cpp %}
std::function<int(float, std::vector<int>)> my_callable;
{% endhighlight %}

our `my_callable` can hold any **function** / **method** / **lambda** that follows
that signature.

We could try something like:
{% highlight cpp %}
template <typename ReturnT, typename... ParametersT>
class function {
    // ...
    using CallSignature = ReturnT(*)(ArgParametersTs...);
    CallSignature callable;
};
{% endhighlight %}

What that means is: we have a field that is a pointer to a function
with parameter list `ParametersT` and return type `ReturnT`.

And this would work if the language was C and only free-standing functions existed.

But C++ has other things we can invoke: methods and lambdas.
Which the `callable` field cannot hold as it currently is.

**Which type should `callable` be?**

Below is the answer, very similar to the code in the first section.
In fact, it's the code in the first section, just with a common interface.

{% highlight cpp %}
template <typename ReturnT, typename... ParametersT>
class function
{
    // ...

    struct Concept {
        virtual ~Concept() = default;
        virtual ReturnT operator(ParametersT&&...) = 0;
    };

    template <typename T>
    struct Model : Concept {
        T obj;
        ReturnT operator(ParametersT&&... args) override {
            return obj(std::forward<ParametersT&&>(args)...);
        };
    };

    Concept* callable;
};
{% endhighlight %}

Now, with a proper constructor, we can pass any callable to our `function`:

{% highlight cpp %}
template <typename ReturnT, typename... ParametersT>
class function
{
    template <typename Func>
    function(Func&& f) : callable{ new Model<Func>(std::forward<Func>(f)) } {}

    // ...
};
{% endhighlight %}

Of course, `callable` can store any `Model<T>`, thanks to polymorphism.

Then, to actually call the function our `function` object holds, we just need to
add the method:

{% highlight cpp %}
template <typename ReturnT, typename... ParametersT>
class function
{
    ReturnT operator()(ParametersT&&... args) {
        return (*callable)(std::forward<ParametersT>(args)...);
    }

    // ...
};
{% endhighlight %}

**Disclaimer:** this code is illustrative.
If you would like to see a working version, I made one too. It's still pretty simple,
but works pretty well.
[Check it on my github][my_function_implementation_github]{:target="_blank"} 🔗.


## About `std::any`

Now that we have implemented `std::function`, writing `std::any` is not that different.

But there is a tiny difference worth mentioning. What should be the common interface
in the `Concept` class?

Turns out we don't really care about a common interface, we just care about retrieving the
value back from the `std::any` instance. To do that, we use `std::any_cast<T>` and we must
provide the correct type T that the function holds.

The C++ reference website has a [very nice example][std_any_docs_example]{:target="_blank"} 🔗 on this.

Feel free to try implementing it yourself. It's pretty fun, if you're a nerd.

Here's my simple implementation of it: [on my github][my_any_implementation_github]{:target="_blank"} 🔗

I will just leave the `Get` method (which mimics `std::any_cast`) here, since it's the interesting one:

{% highlight cpp %}
class Any
{
    template<typename T>
    T& Get() {
        auto p = dynamic_cast<Derived<T>*>(obj_);
        if (!p) throw std::bad_any_cast{};
        return p->actual;
    }

    Concept* obj_;

    // ...
};
{% endhighlight %}

Also, `std::any` is **not** a class template. Pretty weird at first glance, no?

But it makes sense, otherwise its type (the type of the template parameter, which would be part of its type)
would have to change when assigning it an object of a different type.

## Bye

That's all. Get out of here!


<!-- <h2>References</h2> -->

[def_wiki_type_erasure]: https://en.wikipedia.org/wiki/Type_erasure
[def_baeldung_type_erasure]: https://www.baeldung.com/java-type-erasure
[def_rust_type_erasure]: https://users.rust-lang.org/t/on-the-meaning-of-type-erasure-in-rust/113100
[my_function_implementation_github]: https://github.com/gventuraf/gventuraf.github.io/tree/main/code_assets/simple_function_implementation.cpp
[std_any_docs_example]: https://en.cppreference.com/w/cpp/utility/any/type.html
[my_any_implementation_github]: https://github.com/gventuraf/gventuraf.github.io/tree/main/code_assets/simple_any_implementation.cpp