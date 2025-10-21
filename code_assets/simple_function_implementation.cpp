#include <print>
#include <functional>

template <typename T>
class Function;

template <typename R, typename... Args>
struct Function<R(Args...)>
{
    template <typename F>
    Function(F&& f) : callable_{new Derived<F>{std::forward<F>(f)}}
    {}

    Function(R(*f)(Args...))
    : callable_{new Derived<R(*)(Args...)>{std::forward<R(*)(Args...)>(f)}} {}

    ~Function() { if (callable_) delete callable_; }

    //todo missing some constructors

    R operator()(Args&& ... args) { return (*callable_)(std::forward<Args>(args)...); }

    bool HasValue() const { return callable_; }

    // Type Erasure part:

    struct Base {
        virtual R operator()(Args&&...) = 0;
        virtual ~Base() {}
    };
    
    template<typename T>
    struct Derived : Base {
        T obj;
        Derived(T&& data) : obj{ std::forward<T>(data) } {}
        R operator()(Args&&... args) override { return obj(std::forward<Args>(args)...); }
    };

    Base* callable_{nullptr};
};

// Deduction guide for function pointers
// google "CTAD: Class Template Argument Deduction" to learn more about this
template <typename F>
Function(F) -> Function<typename std::remove_pointer_t<std::decay_t<F>>>;


//*
//*
//* Example

int add(int a, int b) {
    return a + b;
}

struct S {
    void operator()(int) { std::println("In S"); }
};

int main() {
    std::println("Hello Function");

    // do not need to specify template parameters
    Function m1{&add};

    // same here
    Function m2 = add;

    // here we do need to specify, because the compiler cannot deduce
    // the same happens with the real std::function
    Function<void(int)> m3 = S{};

    Function<void()> m5 = []{
        std::println("In lambda");
    };

    std::println("1 + 1 = {}", m1(1, 1));
    std::println("2 + 2 = {}", m2(2, 2));
    m3(0);
    m5();

    return 0;
}