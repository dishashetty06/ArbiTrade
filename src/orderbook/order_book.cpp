#include "orderbook/order_book.hpp"

namespace arbitrade
{

    void OrderBook::update(const TickData &tick)
    {
        BookKey key{std::string(tick.exchange), std::string(tick.symbol)};
        book_[key] = tick;
    }

    std::optional<TickData> OrderBook::get(const std::string &exchange, const std::string &symbol) const
    {
        BookKey key{exchange, symbol};
        auto it = book_.find(key);
        if (it == book_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<std::string> OrderBook::exchanges_for_symbol(const std::string &symbol) const
    {
        std::vector<std::string> result;
        for (const auto &entry : book_)
        {
            if (entry.first.symbol == symbol)
            {
                result.push_back(entry.first.exchange);
            }
        }
        return result;
    }

} // namespace arbitrade