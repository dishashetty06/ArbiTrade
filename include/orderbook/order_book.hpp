#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>

#include "common/types.hpp"

namespace arbitrade
{

    // Composite key for the order book map: a given (exchange, symbol) pair
    // identifies one venue's top-of-book entry. A single concatenated-key
    // hash map keeps lookups a single hash + compare instead of a nested
    // map-of-maps, which matters when this sits on the agent's hot path.
    struct BookKey
    {
        std::string exchange;
        std::string symbol;

        bool operator==(const BookKey &other) const
        {
            return exchange == other.exchange && symbol == other.symbol;
        }
    };

    struct BookKeyHash
    {
        std::size_t operator()(const BookKey &key) const
        {
            std::size_t h1 = std::hash<std::string>{}(key.exchange);
            std::size_t h2 = std::hash<std::string>{}(key.symbol);
            return h1 ^ (h2 << 1); // boost::hash_combine-style mixing
        }
    };

    // Pure data-holding class: maintains the most recent TickData seen for
    // every (exchange, symbol) pair. Contains zero networking and zero
    // console output — strategy and transport are completely decoupled
    // from this class by design.
    class OrderBook
    {
    public:
        OrderBook() = default;

        // Updates (or inserts) the top-of-book entry for the tick's
        // (exchange, symbol) pair. Overwrites whatever was there before,
        // since this models top-of-book only, not full depth.
        void update(const TickData &tick);

        // Read-only lookup. Returns std::nullopt if no tick has been seen
        // yet for that (exchange, symbol) pair.
        std::optional<TickData> get(const std::string &exchange, const std::string &symbol) const;

        // Returns every exchange currently known to the book for a given
        // symbol. Lets the strategy layer enumerate venues to compare
        // without needing to know the book's internal storage structure.
        std::vector<std::string> exchanges_for_symbol(const std::string &symbol) const;

        // Total number of distinct (exchange, symbol) entries currently held.
        std::size_t size() const { return book_.size(); }

    private:
        std::unordered_map<BookKey, TickData, BookKeyHash> book_;
    };

} // namespace nexustrade