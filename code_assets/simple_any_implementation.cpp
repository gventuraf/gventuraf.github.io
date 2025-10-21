#include <print>
#include <any>

struct Any
{
    Any() = default;

    ~Any() { if (obj_) delete obj_; }

    Any(const Any& other) : obj_{ other.obj_->clone() } {}

    template <typename T>
    requires (!std::is_same_v<std::decay_t<T>, Any>) // call the ctor above when given an instance of Any
    Any(T&& data) : obj_{ new Derived<std::decay_t<T>>{std::forward<T>(data)} } {}

    //? why use std::decay_t? to avoid problems with string literals

    Any& operator=(const Any& other) {
        if (obj_) delete obj_;
        obj_ = other.obj_->clone();
        return *this;
    }

    template <typename T>
    requires (!std::is_same_v<std::decay_t<T>, Any>) // call the ctor above when given an instance of Any
    Any& operator=(T&& data) {
        if (obj_) delete obj_;
        obj_ = new Derived<std::decay_t<T>>{std::forward<T>(data)};
        return *this;
    }

    Any(Any&& other) : obj_{ other.obj_ } {
        other.obj_ = nullptr;
    }

    Any& operator=(Any&& other) {
        obj_ = other.obj_;
        other.obj_ = nullptr;
        return *this;
    }

    template<typename T>
    T& Get() {
        auto p = dynamic_cast<Derived<T>*>(obj_);
        if (!p) throw std::bad_any_cast{};
        return p->actual;
    }

    struct Base {
        virtual ~Base() {}
        virtual Base* clone() const = 0;
    };
    
    template<typename T>
    struct Derived : Base {
        T actual;
        Derived(const T& data) : actual{data} {}
        Base* clone() const override { return new Derived<T>{actual}; }
    };
    
    Base* obj_{nullptr};
};

int main() {
    std::println("Hello any");
    
    Any a1 = 10;
    std::println("a1 = {}", a1.Get<int>());

    a1 = std::string{"hello"};
    std::println("a1 = {}", a1.Get<std::string>());

    a1 = "hi";
    std::println("a1 = {}", a1.Get<const char*>());

    a1 = 5.5;
    std::println("a1 = {}", a1.Get<double>());

    Any a2 = a1;
    std::println("a2 = {}", a2.Get<double>());

    return 0;
}