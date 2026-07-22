#pragma once

// Binding API for OceanBase Connector/C (placeholders: ?).

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NTPCC {

class Params {
public:
    Params() = default;

    Params& operator()(std::nullptr_t) {
        values_.emplace_back(Null{});
        return *this;
    }

    Params& operator()(int32_t v) {
        values_.emplace_back(v);
        return *this;
    }

    Params& operator()(int64_t v) {
        values_.emplace_back(v);
        return *this;
    }

    Params& operator()(uint64_t v) {
        values_.emplace_back(v);
        return *this;
    }

    Params& operator()(double v) {
        values_.emplace_back(v);
        return *this;
    }

    Params& operator()(std::string_view v) {
        values_.emplace_back(std::string(v));
        return *this;
    }

    Params& operator()(const char* v) {
        values_.emplace_back(std::string(v ? v : ""));
        return *this;
    }

    Params& operator()(const std::string& v) {
        values_.emplace_back(v);
        return *this;
    }

    struct Timestamp {
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
    };

    Params& operator()(Timestamp v) {
        values_.emplace_back(v);
        return *this;
    }

    struct Null {};
    using Value = std::variant<Null, int32_t, int64_t, uint64_t, double, std::string, Timestamp>;

    const std::vector<Value>& Values() const { return values_; }
    size_t Size() const { return values_.size(); }
    bool Empty() const { return values_.empty(); }

private:
    std::vector<Value> values_;
};

template <typename... Args>
inline Params MakeParams(Args&&... args) {
    Params params;
    (params(std::forward<Args>(args)), ...);
    return params;
}

} // namespace NTPCC
