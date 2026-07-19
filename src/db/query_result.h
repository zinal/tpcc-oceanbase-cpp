#pragma once

// Materialized query result for OceanBase Connector/C sessions.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NTPCC {

class QueryResult {
public:
    QueryResult() = default;

    QueryResult(std::vector<std::string> columns,
                std::vector<std::vector<std::optional<std::string>>> rows)
        : columns_(std::move(columns))
        , rows_(std::move(rows))
    {
        for (size_t i = 0; i < columns_.size(); ++i) {
            columnIndex_[columns_[i]] = i;
        }
    }

    bool TryNextRow() {
        if (rowPos_ + 1 >= rows_.size()) {
            return false;
        }
        ++rowPos_;
        return true;
    }

    size_t GetRowsCount() const { return rows_.size(); }
    bool IsEmpty() const { return rows_.empty(); }

    // Position before the first row (same usage pattern as baseline).
    void Reset() { rowPos_ = static_cast<size_t>(-1); }

    int32_t GetInt32(std::string_view col) const { return static_cast<int32_t>(GetInt64(col)); }

    int64_t GetInt64(std::string_view col) const {
        auto s = Require(col);
        return std::stoll(s);
    }

    uint64_t GetUint64(std::string_view col) const {
        auto s = Require(col);
        return std::stoull(s);
    }

    double GetDouble(std::string_view col) const {
        auto s = Require(col);
        return std::stod(s);
    }

    std::string GetString(std::string_view col) const { return Require(col); }

    std::optional<int32_t> GetOptionalInt32(std::string_view col) const {
        auto s = Optional(col);
        if (!s) {
            return std::nullopt;
        }
        return static_cast<int32_t>(std::stoll(*s));
    }

    std::optional<std::string> GetOptionalString(std::string_view col) const {
        return Optional(col);
    }

private:
    size_t IndexOf(std::string_view col) const {
        auto it = columnIndex_.find(std::string(col));
        if (it == columnIndex_.end()) {
            throw std::runtime_error("Unknown column: " + std::string(col));
        }
        return it->second;
    }

    void EnsureRow() const {
        if (rowPos_ >= rows_.size()) {
            throw std::runtime_error("No current row in QueryResult");
        }
    }

    std::string Require(std::string_view col) const {
        EnsureRow();
        const auto& cell = rows_[rowPos_][IndexOf(col)];
        if (!cell) {
            throw std::runtime_error("NULL in column: " + std::string(col));
        }
        return *cell;
    }

    std::optional<std::string> Optional(std::string_view col) const {
        EnsureRow();
        return rows_[rowPos_][IndexOf(col)];
    }

    std::vector<std::string> columns_;
    std::vector<std::vector<std::optional<std::string>>> rows_;
    std::unordered_map<std::string, size_t> columnIndex_;
    size_t rowPos_ = static_cast<size_t>(-1);
};

} // namespace NTPCC
