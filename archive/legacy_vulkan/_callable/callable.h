#ifndef CALLABLE_H
#define CALLABLE_H

#include <functional>
#include <string>
#include <type_traits>
#include <variant>

namespace callable
{

/// @brief Represents the result of a computation that can either succeed or
/// fail
template <typename T>
using Result = std::variant<T, std::string>;

/// @brief Creates a successful result
template <typename T>
constexpr Result<std::decay_t<T>> ok(T&& value)
{
    return Result<std::decay_t<T>>{std::forward<T>(value)};
}

/// @brief Creates an error result
template <typename T>
constexpr Result<T> error(const std::string& message)
{
    return Result<T>{message};
}

/// @brief Checks if result contains a value
template <typename T>
constexpr bool is_ok(const Result<T>& r)
{
    return std::holds_alternative<T>(r);
}

/// @brief Monad-like chain for lazy evaluation and composition
/// @tparam T The type being transformed through the chain
template <typename T>
class Chainable
{
private:
    std::function<Result<T>()> computation_;

public:
    /// @brief Constructor from a computation function
    explicit Chainable(std::function<Result<T>()> comp) : computation_(std::move(comp)) { }

    /// @brief Constructor from a value (pure/return in monadic terms)
    explicit Chainable(T value)
        : computation_([val = std::move(value)]() -> Result<T> { return ok(std::move(val)); })
    {
    }

    /// @brief Constructor from a Result type (for error handling)
    explicit Chainable(Result<T> result)
        : computation_([res = std::move(result)]() -> Result<T> { return res; })
    {
    }

    /// @brief Monadic bind operation (flatMap/andThen)
    /// @tparam F Function type: T -> chain<U>
    template <typename F>
    auto and_then(F&& func) && -> Chainable<typename std::invoke_result_t<F, T>::value_type>
    {
        using U = typename std::invoke_result_t<F, T>::value_type;
        return Chainable<U>(
            [comp = std::move(computation_), f = std::forward<F>(func)]() -> Result<U>
            {
                auto current_result = comp();
                if (auto* value = std::get_if<T>(&current_result))
                {
                    return f(*value).evaluate();
                }
                else
                {
                    return error<U>(std::get<std::string>(current_result));
                }
            });
    }

    /// @brief Map operation (transform the value if present)
    /// @tparam F Function type: T -> U
    template <typename F>
    auto map(F&& func) && -> Chainable<std::invoke_result_t<F, T>>
    {
        using U = std::invoke_result_t<F, T>;
        return Chainable<U>(
            [comp = std::move(computation_), f = std::forward<F>(func)]() -> Result<U>
            {
                auto current_result = comp();
                if (auto* value = std::get_if<T>(&current_result))
                {
                    try
                    {
                        return ok(f(*value));
                    }
                    catch (const std::exception& e)
                    {
                        return error<U>(std::string("Error in map: ") + e.what());
                    }
                }
                else
                {
                    return error<U>(std::get<std::string>(current_result));
                }
            });
    }

    /// @brief Error handling operation
    template <typename F>
    auto or_else(F&& error_handler) && -> Chainable<T>
    {
        return Chainable<T>(
            [comp = std::move(computation_), handler = std::forward<F>(error_handler)]() -> Result<T>
            {
                auto current_result = comp();
                if (is_ok(current_result))
                {
                    return current_result;
                }
                else
                {
                    return handler(std::get<std::string>(current_result));
                }
            });
    }

    /// @brief Lazy evaluation - executes the entire chain
    Result<T> evaluate() const { return computation_(); }

    /// @brief Convenience operator for evaluation
    Result<T> operator()() const { return evaluate(); }

    // Type alias for easier template deduction
    using value_type = T;
};

/// @brief Helper function to create a chain from a value
template <typename T>
auto make_chain(T&& value)
{
    return Chainable<std::decay_t<T>>(std::forward<T>(value));
}

/// @brief Helper function to create a chain from a computation
template <typename F>
auto make_chain_from_computation(F&& computation)
{
    using T = typename std::invoke_result_t<F>::value_type;
    return Chainable<T>(std::forward<F>(computation));
}

/// @brief Pipe operator for chaining operations
template <typename T, typename F>
auto operator|(Chainable<T>&& c, F&& func)
{
    return std::move(c).and_then(std::forward<F>(func));
}

} // namespace callable

#endif // CALLABLE_H
