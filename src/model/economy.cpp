// Validates economy JSON and resolves references used by the game and its saved runs.

#include "economy.h"

#include "json_input.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace wars {
namespace {

using Ids = std::unordered_map<std::string, int>;

// Read a required integer field within the inclusive bounds.
int number(const Json& object, const char* field, int min, int max) {
    const auto& value = object.at(field);
    if (!value.is_number_integer() || value < min || value > max) {
        throw std::runtime_error(std::string(field) + " must be an integer from "
                                 + std::to_string(min) + " to " + std::to_string(max));
    }
    return value.get<int>();
}

// Read a nonempty single-line string within the given byte limit.
std::string text(const Json& object, const char* field, std::size_t max) {
    const auto value = object.at(field).get<std::string>();
    if (value.empty() || value.size() > max || value.find_first_of("\r\n\t") != std::string::npos) {
        throw std::runtime_error(std::string(field) + " is missing or too long");
    }
    return value;
}

// Return a required array by reference, preserving constness and enforcing its size range.
template <typename Value>
Value& array(Value& object, const char* field, std::size_t min, std::size_t max) {
    auto& value = object.at(field);
    if (!value.is_array() || value.size() < min || value.size() > max) {
        throw std::runtime_error(std::string(field) + " has an invalid number of entries");
    }
    return value;
}

// Read a pair of bounded integers and require the upper bound to be at least the lower bound.
Range range(const Json& object, const char* field, int min, int max) {
    const auto& bounds = object.at(field);
    const auto low = number(bounds, "min", min, max);
    return {low, number(bounds, "max", low, max)};
}

// Resolve a reference to its zero-based entry index or reject the unknown ID.
int index(const Ids& ids, const std::string& id) {
    const auto found = ids.find(id);
    if (found == ids.end()) {
        throw std::runtime_error("Unknown economy ID: " + id);
    }
    return found->second;
}

// Index IDs by document order and reject duplicates before resolving references.
Ids ids_for(const Json& entries) {
    Ids ids;
    for (const auto& entry : entries) {
        const auto id = text(entry, "id", 32);
        if (!ids.emplace(id, static_cast<int>(ids.size())).second) {
            throw std::runtime_error("Duplicate economy ID: " + id);
        }
    }
    return ids;
}

// Expand sparse city overrides into a percentage per good, defaulting to 100.
std::vector<int> modifiers(const Json& city, const char* key, const Ids& goods) {
    std::vector<int> values(goods.size(), 100);
    const auto& overrides = city.at(key);
    if (!overrides.is_object()) {
        throw std::runtime_error(std::string(key) + " must be an object of good IDs");
    }
    for (auto item = overrides.begin(); item != overrides.end(); ++item) {
        values[index(goods, item.key())] = number(overrides, item.key().c_str(), 0, 1000);
    }
    return values;
}

} // namespace

// Load both economy schema versions and retain a normalized snapshot for saved runs.
Economy decode_economy(Json data) {
    const bool commodity_events = number(data, "version", 1, 2) == 2;
    const auto& rules = data.at("rules");
    auto& goods = array(data, "goods", 1, 60);
    auto& cities = array(data, "cities", 2, 8);
    auto& events = array(data, "news", 0, commodity_events ? 600 : 100);
    const auto& jackets = array(data, "jackets", 0, 30);
    const auto good_ids = ids_for(goods);
    const auto city_ids = ids_for(cities);
    const auto event_ids = ids_for(events);

    Economy economy{};
    economy.days = number(rules, "days", 2, 365);
    economy.start_city = index(city_ids, text(rules, "start_city", 32));
    economy.starting_cash = number(rules, "starting_cash", 1, max_quote);
    economy.starting_jacket = text(rules, "starting_jacket", 30);
    economy.starting_capacity = number(rules, "starting_capacity", 1, max_cargo);
    economy.opening_good = index(good_ids, text(rules, "guaranteed_opening_good", 32));
    // Accept the older bid-based field name in economy snapshots from previous versions.
    const char* buyout_key = rules.contains("final_buyout_percent_of_market")
        ? "final_buyout_percent_of_market" : "final_buyout_percent_of_bid";
    economy.buyout_percent = number(rules, buyout_key, 0, 100);
    if (data.contains("price_news_skew")) {
        const auto& skew = data.at("price_news_skew");
        economy.price_news_skew.early_days = number(skew, "early_days", 1, 365);
        economy.price_news_skew.minimum_price_threshold = number(skew, "minimum_price_threshold", 1, max_quote);
        economy.price_news_skew.bonus_percent = number(skew, "bonus_percent", 0, 100);
    }
    // Missing rules keep saved economies from before market-wide news unchanged.
    if (data.contains("market_wide_news")) {
        const auto& news = data.at("market_wide_news");
        economy.market_wide_news.chance_basis_points = number(news, "chance_basis_points", 0, 10000);
        for (const auto* direction : {"up", "down"}) {
            auto& stories = std::string_view{direction} == "up"
                ? economy.market_wide_news.up : economy.market_wide_news.down;
            for (const auto& story : array(news, direction, 1, 32)) {
                MarketNewsStory entry{text(story, "title", 40), text(story, "text", 256)};
                if (std::any_of(stories.begin(), stories.end(), [&](const MarketNewsStory& existing) {
                    return existing.title == entry.title || existing.text == entry.text;
                })) {
                    throw std::runtime_error("Duplicate market-wide news story");
                }
                stories.push_back(std::move(entry));
            }
        }
    }
    for (auto& good : goods) {
        good.erase("stock");
        // Keep the legacy field at one so older builds can read saved economies.
        good["slots"] = 1;
        Good entry{
            text(good, "id", 32), text(good, "name", 42), text(good, "short_name", 23),
            range(good, "price", 1, max_quote), {},
        };
        if (commodity_events) {
            for (const auto& story : array(good, "family", 2, 20)) {
                const auto value = story.is_string() ? story.get<std::string>() : text(story, "text", 160);
                if (value.size() > 160 || value.find("{qty}") == std::string::npos
                    || value.find_first_of("\r\n\t") != std::string::npos
                    || std::any_of(entry.family_stories.begin(), entry.family_stories.end(),
                                   [&](const FamilyStory& existing) { return existing.text == value; })) {
                    throw std::runtime_error("Invalid or duplicate family story for " + entry.id);
                }
                auto image = story.is_object() && story.contains("image") ? text(story, "image", 80) : std::string{};
                if (!image.empty() && (std::filesystem::path(image).extension() != ".png"
                    || image.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-.") != std::string::npos)) {
                    throw std::runtime_error("Invalid family story image for " + entry.id);
                }
                entry.family_stories.push_back({value, std::move(image)});
            }
        }
        economy.goods.push_back(std::move(entry));
    }
    for (auto& city : cities) {
        city.erase("stock_percent");
        economy.cities.push_back({
            text(city, "id", 32), text(city, "name", 18), text(city, "hint", 41),
            modifiers(city, "price_percent", good_ids),
        });
    }
    std::vector<std::vector<std::string_view>> headlines(goods.size());
    // Keep event storage stable while temporary string views refer to headlines for duplicate checks.
    economy.events.reserve(events.size());
    for (auto& event : events) {
        event.erase("stock_percent");
        event.erase("stock_cap");
        if (commodity_events) {
            const auto direction = text(event, "direction", 4);
            if (direction != "up" && direction != "down") {
                throw std::runtime_error("News direction must be up or down");
            }
            const int good = index(good_ids, text(event, "good", 32));
            economy.events.push_back({
                text(event, "id", 32), text(event, "headline", 155),
                number(event, "weight", 0, 10000), 0, 1, true, -1, false, {good},
                100, direction == "up" ? 1 : -1,
            });
            auto& seen = headlines[good];
            const auto& headline = economy.events.back().headline;
            if (std::find(seen.begin(), seen.end(), headline) != seen.end()) {
                throw std::runtime_error("Duplicate headline for " + economy.goods[good].id);
            }
            seen.push_back(headline);
            continue;
        }
        const auto scope = text(event, "scope", 8);
        if (scope != "global" && scope != "local") {
            throw std::runtime_error("Event scope must be global or local");
        }
        const auto& requires = event.at("requires");
        Event rule{
            text(event, "id", 32), text(event, "headline", 80),
            number(event, "weight", 0, 10000), number(event, "cooldown_days", 0, 365),
            number(event, "duration_days", 1, 365), scope == "local",
            requires.is_null() ? -1 : index(event_ids, requires.get<std::string>()),
            event.at("once_per_run").get<bool>(), {},
            number(event, "price_percent", 0, 10000),
        };
        for (const auto& good : array(event, "goods", 1, goods.size())) {
            const int target = index(good_ids, good.get<std::string>());
            if (std::find(rule.goods.begin(), rule.goods.end(), target) != rule.goods.end()) {
                throw std::runtime_error("Duplicate target in headline: " + rule.id);
            }
            rule.goods.push_back(target);
        }
        if (rule.required_event == index(event_ids, rule.id)) {
            throw std::runtime_error("Event cannot require itself: " + rule.id);
        }
        economy.events.push_back(std::move(rule));
    }
    if (commodity_events) {
        for (std::size_t good = 0; good < headlines.size(); ++good) {
            if (headlines[good].size() < 5) {
                throw std::runtime_error("At least five headlines required for " + economy.goods[good].id);
            }
        }
    }
    for (const auto& jacket : jackets) {
        economy.jackets.push_back({
            text(jacket, "name", 30), number(jacket, "capacity", 1, max_cargo),
            number(jacket, "cost", 0, max_quote), number(jacket, "weight", 0, 10000),
        });
    }
    economy.document = data.dump();
    if (economy.document.size() > json_input::economy_bytes) {
        throw json_input::InvalidDocument("Economy snapshot exceeds its size limit");
    }
    return economy;
}

Economy parse_economy(std::string document) {
    return decode_economy(json_input::parse(document, json_input::economy_bytes));
}

Economy load_economy(const std::filesystem::path& path) {
    return parse_economy(json_input::read(path, json_input::economy_bytes));
}

} // namespace wars
