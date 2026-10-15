#include "strategy/spread_detector.hpp"

#include <algorithm>
#include <cstring>

namespace arbitrade
{

    std::optional<TradeSignal> SpreadDetector::detect(const OrderBook &book,
                                                      const std::string &symbol,
                                                      uint64_t latest_sequence_number) const
    {
        std::vector<std::string> exchanges = book.exchanges_for_symbol(symbol);

        // Need at least two venues quoting the same symbol before a
        // cross-exchange arbitrage is even geometrically possible.
        if (exchanges.size() < 2)
        {
            return std::nullopt;
        }

        bool found_opportunity = false;
        double best_profit = 0.0;
        TradeSignal best_signal{};

        for (const std::string &sell_exchange : exchanges)
        {
            std::optional<TickData> sell_tick = book.get(sell_exchange, symbol);
            if (!sell_tick.has_value())
            {
                continue;
            }

            for (const std::string &buy_exchange : exchanges)
            {
                if (sell_exchange == buy_exchange)
                {
                    continue;
                }

                std::optional<TickData> buy_tick = book.get(buy_exchange, symbol);
                if (!buy_tick.has_value())
                {
                    continue;
                }

                // A crossed market exists when one venue's bid (where we
                // would sell) is strictly greater than another venue's ask
                // (where we would buy) for the same instrument.
                if (sell_tick->bid_price > buy_tick->ask_price)
                {
                    double spread = sell_tick->bid_price - buy_tick->ask_price;
                    double executable_volume = std::min(sell_tick->bid_amount, buy_tick->ask_amount);
                    double estimated_profit = spread * executable_volume;

                    if (!found_opportunity || estimated_profit > best_profit)
                    {
                        found_opportunity = true;
                        best_profit = estimated_profit;

                        std::memset(&best_signal, 0, sizeof(TradeSignal));
                        std::strncpy(best_signal.buy_exchange, buy_exchange.c_str(), sizeof(best_signal.buy_exchange) - 1);
                        std::strncpy(best_signal.sell_exchange, sell_exchange.c_str(), sizeof(best_signal.sell_exchange) - 1);
                        std::strncpy(best_signal.symbol, symbol.c_str(), sizeof(best_signal.symbol) - 1);
                        best_signal.buy_price = buy_tick->ask_price;
                        best_signal.sell_price = sell_tick->bid_price;
                        best_signal.spread = spread;
                        best_signal.executable_volume = executable_volume;
                        best_signal.estimated_profit = estimated_profit;
                        best_signal.sequence_number = latest_sequence_number;
                    }
                }
            }
        }

        if (!found_opportunity)
        {
            return std::nullopt;
        }

        return best_signal;
    }

} // namespace arbitrade