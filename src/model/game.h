// Defines game state and the rules API used by the UI and the profile store.

#pragma once

#include "economy.h"

#include <optional>
#include <random>
#include <string_view>

namespace wars {

inline constexpr std::size_t max_name_length = 16;
inline constexpr std::size_t score_count = 10;
inline constexpr Money exploit_price = 20'000;
inline constexpr Money jacket_upgrade_price = 12'000;
inline constexpr int jacket_upgrade_slots = 25;
inline constexpr int ai_wars_day = 25;
inline constexpr int ai_wars_price_change = 40;
// The scheduled headline has no index in the economy's random news table.
inline constexpr int ai_wars_headline = -2;

constexpr int market_quote_limit(int day) {
    return day >= ai_wars_day
        ? static_cast<int>(Money{max_quote} * (100 + ai_wars_price_change) / 100)
        : max_quote;
}

struct Quote {
    Money price;
};

struct Holding {
    int lots;
    Money cost;
};

struct NewsEffect {
    int event;
    int until_day;
    int city;
};

// Resolve a price headline once, retaining its exact ratio and optional market-wide story.
struct PriceNews {
    int day;
    int direction;
    int numerator;
    int denominator;
    bool all_goods = false;
    int story = -1;
};

enum class EncounterType {
    None, News, BlackHat, Mugging, Family, Stash, Officer, Patch, Fight, Escape, Confiscation,
};

struct CargoChange {
    int good;
    int lots;
};

struct Encounter {
    EncounterType type = EncounterType::None;
    int story = 0;
    std::vector<CargoChange> cargo;
};

enum class Equipment { Exploit, JacketUpgrade };

// Indices in a run refer to its session's economy, including goods, cities, and news events.
struct Run {
    std::string player;
    std::uint64_t seed;
    int day;
    int city;
    Money cash;
    std::string jacket;
    int capacity;
    std::vector<Quote> market;
    std::vector<Holding> holdings;
    std::vector<NewsEffect> effects;
    std::vector<int> last_event_day;
    int last_headline = -1;
    bool pending_news = false;
    int pending_jacket = -1;
    Encounter encounter;
    Money unused_exploits = 0;
    Money exposed_exploits = 0;
    int last_exposed_day = 0;
    std::optional<PriceNews> price_news;
};

// Pair a run with the rules it started with so a resumed run uses the same economy.
struct Session {
    Economy economy;
    Run run;
};

struct Score {
    std::string player;
    Money value;
    std::uint64_t seed;
};

enum class SortOrder { NameAscending, NameDescending, PriceDescending, PriceAscending };

struct Profile {
    std::string last_player;
    std::vector<Score> scores;
    std::optional<Session> active;
    SortOrder sort_order = SortOrder::PriceAscending;
    bool sound_enabled = true;
};

// Check whether a single byte is allowed in a player name.
bool name_character(char c);
// Trim and validate a name. Return an empty string if it cannot be used.
std::string player_name(std::string_view name);
// Draw an integer quote within the given bounds, weighted toward their midpoint.
int generate_commodity_price(int min_price, int max_price, std::mt19937_64& rng);
// Create the first day's state and quotes from the economy and run seed.
Run start_run(const Economy& economy, std::string player, std::uint64_t seed);
// Total the slots occupied by all lots in the jacket.
int used_slots(const Session& session);
// Return the whole-dollar cost per lot, or zero for an empty holding.
Money average_cost(const Holding& holding);
// Return sorted good indices, limited to owned goods when viewing the jacket.
std::vector<int> market_listing(const Session& session, SortOrder order, bool jacket);
// Return the maximum lots that can be traded in the current state and direction.
int trade_limit(const Session& session, int good, bool buy);
// Buy or sell the requested lots if permitted. Return whether the trade succeeded.
bool trade(Session& session, int good, bool buy, int lots);
// Check whether the run can advance one day by flying to the given city.
bool can_jet(const Session& session, int city);
// Advance a day, apply an arrival event, and refresh the destination market.
bool jet(Session& session, int city);
// Accept or dismiss a pending jacket offer and report whether it was resolved.
bool answer_jacket(Session& session, bool buy);
// Check whether the current Black Hat offer and player resources permit the purchase.
bool can_buy_equipment(const Session& session, Equipment equipment);
// Charge for an available Black Hat upgrade and apply it to the run.
bool buy_equipment(Session& session, Equipment equipment);
// Apply the selected officer action if valid and record the encounter outcome.
bool resolve_officer(Session& session, bool fight);
// Return the headline's price multiplier as a percentage, with 100 representing the original price.
int news_price_percent(const Run& run, const Event& event);
// Build the current encounter's text, including any cargo quantity substitutions.
std::string encounter_story(const Session& session);
// Value the remaining cargo at the current city's end-of-run buyout prices.
Money final_buyout(const Session& session);
// Insert a qualifying score and return its zero-based leaderboard rank.
std::optional<std::size_t> record_score(Profile& profile, Score score);
// Format a whole-dollar value with comma grouping.
std::string format_number(Money value);
// Build the leaderboard entry text encoded in a share QR code.
std::string share_score(const Score& score);

} // namespace wars
