// Seeded balance experiments using the production rules, without rendering.
#include "game.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

using wars::Money;
using wars::Session;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::ofstream output(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    return stream;
}

void invariant(const Session& s) {
    const auto& r = s.run;
    require(r.cash >= 0 && r.cash <= wars::max_score, "cash outside supported range");
    require(r.day >= 1 && r.day <= s.economy.days, "invalid day");
    require(wars::used_slots(s) <= r.capacity && r.capacity <= wars::max_cargo, "cargo overflow");
    for (std::size_t g = 0; g < r.holdings.size(); ++g) {
        const auto& h = r.holdings[g];
        require(h.lots >= 0 && h.cost >= 0 && (h.lots || h.cost == 0), "invalid holding");
        require(r.market[g].price >= 0 && r.market[g].price <= wars::market_quote_limit(r.day),
                "invalid quote");
    }
}

struct Metrics {
    Money spent = 0, sales = 0, buyout = 0, lost_cost = 0, gift_value = 0;
    long long bought = 0, sold = 0, lost = 0, gifts = 0, lot_days = 0;
    int buys = 0, sells = 0, available_days = 0, affordable_days = 0;
    int first_buy_day = 0;
    json encode(const std::string& id) const {
        return {{"good", id}, {"spent", spent}, {"sales", sales}, {"buyout", buyout},
                {"net", sales + buyout - spent}, {"lost_cost", lost_cost},
                {"gift_value_at_receipt", gift_value}, {"bought_lots", bought}, {"sold_lots", sold},
                {"lost_lots", lost}, {"gift_lots", gifts}, {"lot_days", lot_days},
                {"buys", buys}, {"sells", sells}, {"available_days", available_days},
                {"affordable_days", affordable_days}, {"first_buy_day", first_buy_day}};
    }
};

struct Experiment {
    Session s;
    std::string mode, policy;
    int focus, excluded;
    std::vector<Metrics> stats;
    Money equipment = 0;
    json events = json::object();
    std::ostream* trace;

    Experiment(const wars::Economy& economy, std::uint64_t seed, std::string m,
               std::string p, int f, std::ostream* t, int x = -1)
        : s{economy, wars::start_run(economy, "Balance", seed)}, mode(std::move(m)),
          policy(std::move(p)), focus(f), excluded(x), stats(economy.goods.size()), trace(t) {}

    void record(const std::string& action, int g = -1, int lots = 0) {
        if (!trace) return;
        *trace << json{{"seed", s.run.seed}, {"mode", mode}, {"policy", policy},
            {"focus", focus < 0 ? "" : s.economy.goods[focus].id}, {"day", s.run.day},
            {"excluded", excluded < 0 ? "" : s.economy.goods[excluded].id},
            {"city", s.economy.cities[s.run.city].id}, {"action", action},
            {"good", g < 0 ? "" : s.economy.goods[g].id}, {"lots", lots},
            {"price", g < 0 ? 0 : s.run.market[g].price}, {"cash", s.run.cash}}.dump() << '\n';
    }

    void account_changes(const std::vector<wars::Holding>& before) {
        for (std::size_t g = 0; g < before.size(); ++g) {
            const auto& after = s.run.holdings[g];
            const int delta = after.lots - before[g].lots;
            if (delta < 0) {
                stats[g].lost -= delta;
                stats[g].lost_cost += before[g].cost - after.cost;
                record("cargo-loss", static_cast<int>(g), -delta);
            } else if (delta > 0) {
                stats[g].gifts += delta;
                stats[g].gift_value += delta * s.run.market[g].price;
                record("free-cargo", static_cast<int>(g), delta);
            }
        }
    }

    void acknowledge() {
        auto& r = s.run;
        if (r.pending_news) {
            const auto key = r.last_headline == wars::ai_wars_headline ? "day-25-news" : "news";
            events[key] = events.value(key, 0) + 1;
            r.pending_news = false;
        }
        if (r.pending_jacket >= 0) require(wars::answer_jacket(s, false), "could not decline jacket");
        const auto type = r.encounter.type;
        if (type != wars::EncounterType::None) {
            const auto key = "encounter-" + std::to_string(static_cast<int>(type));
            events[key] = events.value(key, 0) + 1;
        }
        if (type == wars::EncounterType::Officer) {
            const auto before = r.holdings;
            require(wars::resolve_officer(s, r.unused_exploits + r.exposed_exploits > 0),
                    "officer resolution failed");
            account_changes(before);
        } else if (type == wars::EncounterType::BlackHat && policy == "upgrades"
                   && r.day < s.economy.days - 5) {
            const auto cash = r.cash;
            if (r.unused_exploits + r.exposed_exploits == 0 && r.cash >= 4 * wars::exploit_price)
                require(wars::buy_equipment(s, wars::Equipment::Exploit), "exploit purchase failed");
            if (r.cash >= 4 * wars::jacket_upgrade_price
                && wars::can_buy_equipment(s, wars::Equipment::JacketUpgrade))
                require(wars::buy_equipment(s, wars::Equipment::JacketUpgrade), "upgrade failed");
            equipment += cash - r.cash;
        }
        // These are the same acknowledgement state changes made by GameUi.
        r.encounter = {};
        invariant(s);
    }

    void trade(int good, bool buy) {
        const int lots = wars::trade_limit(s, good, buy);
        if (!lots || s.run.market[good].price == 0) return;
        const Money value = lots * s.run.market[good].price;
        require(wars::trade(s, good, buy, lots), "valid trade was rejected");
        auto& m = stats[good];
        if (buy) {
            ++m.buys; m.bought += lots; m.spent += value;
            if (!m.first_buy_day) m.first_buy_day = s.run.day;
        } else {
            ++m.sells; m.sold += lots; m.sales += value;
        }
        record(buy ? "buy" : "sell", good, lots);
        invariant(s);
    }
};

// Forecasts use configured typical prices, never a future quote or future RNG.
double expected_price(const Session& s, int city, int good) {
    auto bounds = s.economy.goods[good].price;
    const int day = s.run.day + 1;
    if (day >= wars::ai_wars_day) {
        bounds.min = std::max(1, static_cast<int>(Money{bounds.min} * 60 / 100));
        bounds.max = static_cast<int>(Money{bounds.max} * 140 / 100);
    }
    double value = (double(bounds.min) + bounds.max) / 2
        * s.economy.cities[city].price_percent[good] / 100;
    // Only effects whose expiry is already known are used to plan the next day.
    for (const auto& effect : s.run.effects) {
        const auto& event = s.economy.events[effect.event];
        if (effect.until_day < day || (effect.city >= 0 && effect.city != city)
            || std::find(event.goods.begin(), event.goods.end(), good) == event.goods.end()) continue;
        value *= event.price_percent / 100.0;
    }
    return value <= 0 ? 0 : std::clamp(value, 1.0, double(wars::market_quote_limit(day)));
}

struct Plan { int city, good; double merit; };

Plan plan(Experiment& e) {
    const auto& s = e.s;
    const int n = static_cast<int>(s.economy.cities.size());
    // Seed offsets give the rotating strategy different routes across games.
    const int rotation = 1 + static_cast<int>(s.run.seed % (n - 1));
    const int next = (s.run.city + rotation) % n;
    // A deterministic fallback for policies without a profitable purchase.
    const int fallback = (s.run.city + 1) % n;
    Plan best{fallback, -1, 0};
    double best_fallback = -1;
    for (int city = 0; city < n; ++city) {
        if (city == s.run.city || (e.policy == "rotate" && city != next)) continue;
        double fallback_value = 0;
        if (e.focus >= 0) {
            // When a focused good is unaffordable, visit its cheapest market.
            const auto price = expected_price(s, city, e.focus);
            fallback_value = price > 0 ? 1.0 / price : 0;
        }
        if (best.good < 0 && fallback_value > best_fallback) {
            best.city = city;
            best_fallback = fallback_value;
        }
        for (int good = 0; good < static_cast<int>(s.economy.goods.size()); ++good) {
            if ((e.focus >= 0 && e.focus != good) || e.excluded == good) continue;
            const Money price = s.run.market[good].price;
            if (price <= 0) continue;
            const int lots = wars::trade_limit(s, good, true);
            if (!lots) continue;
            const double gain = expected_price(s, city, good) - price;
            const double merit = e.policy == "roi" ? gain / double(price) : gain * lots;
            // Sell at each arrival; buy only if the forecast shows a positive edge.
            if (merit > best.merit) best = {city, good, merit};
        }
    }
    return best;
}

json play(const wars::Economy& economy, std::uint64_t seed, const std::string& mode,
          const std::string& policy, int focus, std::ostream* trace, int excluded = -1) {
    Experiment e(economy, seed, mode, policy, focus, trace, excluded);
    try {
        for (;;) {
            e.acknowledge();
            auto& s = e.s;
            for (int good = 0; good < static_cast<int>(economy.goods.size()); ++good) {
                // Focus runs retain unrelated free cargo until settlement, preventing
                // an expensive focus commodity being financed by unrelated windfalls.
                if (focus < 0 || focus == good || s.run.day == economy.days) e.trade(good, false);
            }
            for (int good = 0; good < static_cast<int>(economy.goods.size()); ++good) {
                auto& m = e.stats[good];
                m.available_days += s.run.market[good].price > 0;
                m.affordable_days += s.run.market[good].price > 0
                    && s.run.market[good].price <= s.run.cash;
            }
            if (s.run.day == economy.days) break;
            const auto flight = plan(e);
            if (flight.good >= 0) e.trade(flight.good, true);
            for (std::size_t g = 0; g < e.stats.size(); ++g)
                e.stats[g].lot_days += s.run.holdings[g].lots;
            const auto before = s.run.holdings;
            require(wars::jet(s, flight.city), "planned flight was rejected");
            e.record("arrival");
            e.account_changes(before);
            invariant(s);
        }
        Money contribution = 0;
        json goods = json::array();
        for (std::size_t g = 0; g < e.stats.size(); ++g) {
            auto& m = e.stats[g];
            m.buyout = (e.s.run.market[g].price * economy.buyout_percent / 100)
                * e.s.run.holdings[g].lots;
            contribution += m.sales + m.buyout - m.spent;
            // Include zero-trade focus rows; omit unrelated zero rows to keep output small.
            if (m.buys || m.sells || m.gifts || m.lost || static_cast<int>(g) == focus)
                goods.push_back(m.encode(economy.goods[g].id));
        }
        const Money score = e.s.run.cash + wars::final_buyout(e.s);
        require(score - economy.starting_cash == contribution - e.equipment,
                "profit accounting does not reconcile to final score");
        return {{"type", "run"}, {"seed", seed}, {"mode", mode}, {"policy", policy},
                {"excluded", excluded < 0 ? "" : economy.goods[excluded].id},
                {"focus", focus < 0 ? "" : economy.goods[focus].id}, {"starting_cash", economy.starting_cash},
                {"score", score}, {"net_profit", score - economy.starting_cash},
                {"equipment_cost", e.equipment}, {"events", e.events}, {"goods", goods}};
    } catch (const std::exception& ex) {
        throw std::runtime_error("seed=" + std::to_string(seed) + " mode=" + mode + " policy=" + policy
            + " focus=" + (focus < 0 ? "all" : economy.goods[focus].id)
            + " excluded=" + (excluded < 0 ? "none" : economy.goods[excluded].id)
            + " day=" + std::to_string(e.s.run.day) + ": " + ex.what());
    }
}

// A common random route compares every good on the exact same market arrivals.
// This measures price opportunities only, without cargo losses or compounding.
void probe(const wars::Economy& economy, std::uint64_t seed, Money budget, std::ostream& out) {
    Experiment e(economy, seed, "probe", "none", -1, nullptr);
    std::mt19937_64 route(seed ^ 0xc6bc279692b5cc83ULL);
    const int count = static_cast<int>(economy.goods.size());
    std::vector<json> totals(count * 3);
    for (auto& item : totals) item = {{"hops", 0}, {"available", 0}, {"affordable", 0},
        {"starting_affordable", 0}, {"sale_unavailable", 0}, {"starting_profit", 0}, {"starting_spent", 0},
        {"selected", 0}, {"wins", 0}, {"roi_sum", 0.0}, {"gain_per_lot_sum", 0.0},
        {"budget_profit", 0}, {"budget_spent", 0}, {"roi_square_sum", 0.0}};
    while (e.s.run.day < economy.days) {
        e.acknowledge();
        const int cities = static_cast<int>(economy.cities.size());
        const int destination = (e.s.run.city + 1 + static_cast<int>(route() % (cities - 1))) % cities;
        const int phase = e.s.run.day <= 10 ? 0 : (e.s.run.day < wars::ai_wars_day ? 1 : 2);
        const auto quotes = e.s.run.market;
        std::vector<bool> selected(count);
        for (int g = 0; g < count; ++g)
            selected[g] = quotes[g].price > 0 && expected_price(e.s, destination, g) > quotes[g].price;
        require(wars::jet(e.s, destination), "probe flight failed");
        // Acknowledge before using arrival quotes, as an ordinary player would.
        e.acknowledge();
        for (int g = 0; g < count; ++g) {
            auto& t = totals[phase * count + g];
            t["hops"] = t["hops"].get<int>() + 1;
            const Money buy = quotes[g].price, sell = e.s.run.market[g].price;
            if (buy <= 0) continue;
            t["available"] = t["available"].get<int>() + 1;
            t["sale_unavailable"] = t["sale_unavailable"].get<int>() + (sell == 0);
            const Money lots = std::min<Money>(economy.starting_capacity, budget / buy);
            const Money starting_lots = std::min<Money>(economy.starting_capacity, economy.starting_cash / buy);
            if (lots > 0) t["affordable"] = t["affordable"].get<int>() + 1;
            if (starting_lots > 0) t["starting_affordable"] = t["starting_affordable"].get<int>() + 1;
            if (!selected[g]) continue;
            t["selected"] = t["selected"].get<int>() + 1;
            const double roi = double(sell - buy) / double(buy);
            t["roi_sum"] = t["roi_sum"].get<double>() + roi;
            t["roi_square_sum"] = t["roi_square_sum"].get<double>() + roi * roi;
            t["gain_per_lot_sum"] = t["gain_per_lot_sum"].get<double>() + double(sell - buy);
            t["wins"] = t["wins"].get<int>() + (sell > buy);
            t["budget_profit"] = t["budget_profit"].get<Money>() + lots * (sell - buy);
            t["budget_spent"] = t["budget_spent"].get<Money>() + lots * buy;
            t["starting_profit"] = t["starting_profit"].get<Money>() + starting_lots * (sell - buy);
            t["starting_spent"] = t["starting_spent"].get<Money>() + starting_lots * buy;
        }
    }
    for (int phase = 0; phase < 3; ++phase) for (int g = 0; g < count; ++g) {
        auto t = totals[phase * count + g];
        t.update({{"type", "probe"}, {"seed", seed}, {"good", economy.goods[g].id},
                  {"phase", phase == 0 ? "early-1-10" : phase == 1 ? "middle-11-24" : "late-25+"}});
        out << t.dump() << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 8, "expected economy output seeds first-seed funded-cash probe-cash trace");
        const auto economy = wars::load_economy(argv[1]);
        const fs::path directory(argv[2]);
        const auto seeds = std::stoull(argv[3]), first = std::stoull(argv[4]);
        auto funded = economy;
        funded.starting_cash = std::stoll(argv[5]);
        const Money probe_cash = std::stoll(argv[6]);
        require(seeds > 0 && first <= std::numeric_limits<std::uint64_t>::max() - (seeds - 1), "invalid seed range");
        require(funded.starting_cash > 0 && funded.starting_cash <= wars::max_quote && probe_cash > 0,
                "invalid experiment cash");
        fs::create_directories(directory);
        auto raw = output(directory / "results.jsonl");
        std::ofstream trace;
        const bool tracing = std::string(argv[7]) == "1";
        if (tracing) trace = output(directory / "trades.jsonl");
        std::uint64_t games = 0;
        for (std::uint64_t i = 0; i < seeds; ++i) {
            const auto seed = first + i;
            for (const auto* policy : {"profit", "roi", "rotate", "upgrades"}) {
                raw << play(economy, seed, "mixed", policy, -1, tracing ? &trace : nullptr).dump() << '\n';
                ++games;
            }
            for (int g = 0; g < static_cast<int>(economy.goods.size()); ++g) {
                raw << play(economy, seed, "focus-start", "profit", g, tracing ? &trace : nullptr).dump() << '\n';
                raw << play(funded, seed, "focus-funded", "profit", g, tracing ? &trace : nullptr).dump() << '\n';
                games += 2;
                for (const auto* policy : {"profit", "roi"}) {
                    raw << play(economy, seed, "without", policy, -1,
                                tracing ? &trace : nullptr, g).dump() << '\n';
                    ++games;
                }
            }
            probe(economy, seed, probe_cash, raw);
            if ((i + 1) % 100 == 0 || i + 1 == seeds)
                std::cout << "Seeds " << i + 1 << '/' << seeds << ", completed games " << games << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Balance test failed: " << error.what() << '\n';
        return 1;
    }
}
