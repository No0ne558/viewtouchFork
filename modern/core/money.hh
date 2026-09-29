#pragma once

// Money as a signed count of cents. Replaces the legacy mix of int cents and
// Flt rates; all arithmetic is integral and rounding is explicit.

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace vt {

class Money {
public:
    constexpr Money() = default;
    static constexpr Money fromCents(std::int64_t cents) { return Money(cents); }

    constexpr std::int64_t cents() const { return cents_; }

    constexpr Money operator+(Money o) const { return Money(cents_ + o.cents_); }
    constexpr Money operator-(Money o) const { return Money(cents_ - o.cents_); }
    constexpr Money operator-() const { return Money(-cents_); }
    constexpr Money operator*(std::int64_t qty) const { return Money(cents_ * qty); }
    constexpr Money &operator+=(Money o) { cents_ += o.cents_; return *this; }
    constexpr Money &operator-=(Money o) { cents_ -= o.cents_; return *this; }

    constexpr auto operator<=>(const Money &) const = default;

    // Percentage expressed in basis points (1 bp = 0.01%), e.g. 8.25% = 825.
    // Rounds half away from zero to the nearest cent.
    constexpr Money percent(std::int64_t basisPoints) const
    {
        const std::int64_t num = cents_ * basisPoints;
        const std::int64_t q = num / 10000;
        const std::int64_t r = num % 10000;
        if (r >= 5000) return Money(q + 1);
        if (r <= -5000) return Money(q - 1);
        return Money(q);
    }

    // "12.34", "-0.05"
    std::string toString() const
    {
        const std::int64_t a = cents_ < 0 ? -cents_ : cents_;
        std::string frac = std::to_string(a % 100);
        if (frac.size() < 2) frac.insert(0, "0");
        return (cents_ < 0 ? "-" : "") + std::to_string(a / 100) + "." + frac;
    }

private:
    constexpr explicit Money(std::int64_t cents) : cents_(cents) {}
    std::int64_t cents_ = 0;
};

} // namespace vt
