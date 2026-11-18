// Defines the configurable economy and the API for loading and validating its JSON rules.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace wars {

using Money = std::int64_t;
inline constexpr Money max_score = 1'000'000'000'000'000LL;
inline constexpr int max_cargo = 999;
inline constexpr int max_quote = 1'000'000'000;

struct Range {
    int min;
    int max;
};

struct FamilyStory {
    std::string text;
    std::string image;
};

struct Good {
    std::string id;
    std::string name;
    std::string short_name;
    Range price;
    std::vector<FamilyStory> family_stories;
};

struct City {
    std::string id;
    std::string name;
    std::string hint;
    std::vector<int> price_percent;
};

struct Event {
    std::string id;
    std::string headline;
    int weight;
    int cooldown;
    int duration;
    bool local;
    int required_event;
    bool once;
    std::vector<int> goods;
    int price_percent;
    int price_direction = 0;
};

struct Jacket {
    std::string name;
    int capacity;
    Money cost;
    int weight;
};

struct MarketNewsStory {
    std::string title;
    std::string text;
};

struct MarketWideNews {
    int chance_basis_points = 0;
    std::vector<MarketNewsStory> up;
    std::vector<MarketNewsStory> down;
};

struct PriceNewsSkew {
    int early_days = 10;
    int minimum_price_threshold = 4000;
    // Economies saved before this rule retain their original headline selection.
    int bonus_percent = 0;
};

// Keep the normalized JSON alongside the indexed rules so a run can save its economy.
struct Economy {
    std::string document;
    int days;
    int start_city;
    Money starting_cash;
    std::string starting_jacket;
    int starting_capacity;
    int opening_good;
    int buyout_percent;
    std::vector<Good> goods;
    std::vector<City> cities;
    std::vector<Event> events;
    std::vector<Jacket> jackets;
    MarketWideNews market_wide_news;
    PriceNewsSkew price_news_skew;
};

// Validate a JSON document and resolve its IDs into indices. Throw on invalid rules.
Economy parse_economy(std::string document);
// Read and validate an economy file, throwing if it cannot be opened or parsed.
Economy load_economy(const std::filesystem::path& path);

} // namespace wars
