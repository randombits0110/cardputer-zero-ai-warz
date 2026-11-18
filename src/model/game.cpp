// Implements seeded market prices, travel encounters, inventory accounting, and leaderboard rules.

#include "game.h"
#include "encounter_text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace wars {
namespace {

struct EventOdds {
    EncounterType type;
    int weight;
};

// this is where you modify odds of special events at the start of each turn / day.
constexpr std::array arrival_odds{
    EventOdds{EncounterType::None, 2300},
    EventOdds{EncounterType::News, 3500},
    EventOdds{EncounterType::BlackHat, 1600},
    EventOdds{EncounterType::Mugging, 400},
    EventOdds{EncounterType::Family, 600},
    EventOdds{EncounterType::Stash, 600},
    EventOdds{EncounterType::Officer, 1000},
};
// Keep the arrival weights on the same 10,000-point scale as the event chances.
static_assert([] {
    int total = 0;
    for (const auto& entry : arrival_odds) total += entry.weight;
    return total == 10000;
}());

constexpr int patch_chance = 1500;
constexpr int escape_chance = 5000;

// Derive a repeatable random seed for a particular day of a run.
std::uint64_t day_seed(std::uint64_t seed, int day) {
    return seed + static_cast<std::uint64_t>(day) * 0x9e3779b97f4a7c15ULL;
}

class DayRandom {
public:
    // Start a predictable sequence of draws for the given seed and day.
    DayRandom(std::uint64_t seed, int day)
        : engine_(day_seed(seed, day)) {}

    // Draw uniformly from the inclusive bounds using rejection sampling to avoid modulo bias.
    int roll(int low, int high) {
        if (low > high) std::swap(low, high);
        const auto span = static_cast<std::uint64_t>(Money{high} - low) + 1;
        const auto threshold = (std::uint64_t{0} - span) % span;
        auto draw = engine_();
        while (draw < threshold) {
            draw = engine_();
        }
        return static_cast<int>(low + static_cast<Money>(draw % span));
    }

    // Evaluate a probability expressed in basis points, where 10,000 is certain.
    bool chance(int basis_points) {
        return roll(1, 10000) <= basis_points;
    }

private:
    std::mt19937_64 engine_;
};

// Select an index in proportion to its nonnegative weight, or return -1 if the total is zero.
template <typename Weight>
int weighted_choice(DayRandom& rng, int count, Weight weight) {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        total += weight(i);
    }
    if (!total) {
        return -1;
    }
    int draw = rng.roll(1, total);
    for (int i = 0; i < count; ++i) {
        draw -= weight(i);
        if (draw <= 0) {
            return i;
        }
    }
    return -1;
}

struct PriceRatio {
    int numerator;
    int denominator;
};

// Recreate the price swing from the headline day and a separate random stream.
PriceRatio news_ratio(std::uint64_t seed, int day, int direction) {
    constexpr std::uint64_t news_seed_salt = 0xa0761d6478bd642fULL;
    DayRandom rng(seed ^ news_seed_salt, day);
    const int tier = rng.roll(1, 100);
    const auto bounds = tier <= 80 ? Range{15, 40}
        : tier <= 90 ? Range{41, 60}
        : tier <= 98 ? Range{61, 100}
        : Range{200, 1500};
    const int magnitude = rng.roll(bounds.min, bounds.max);
    // Use the reciprocal for extreme drops so even a large swing leaves a positive multiplier.
    if (direction < 0 && tier > 98) {
        return {100, 100 + magnitude};
    }
    return {100 + direction * magnitude, 100};
}

// Choose an eligible headline and queue it for the player, storing any lasting effect.
void select_news(Session& session, DayRandom& rng) {
    auto& run = session.run;
    const auto& economy = session.economy;
    const auto& skew = economy.price_news_skew;
    // Scale every eligible weight equally before adding the bonus, without rounding.
    // A disabled skew keeps the original draw range for existing saved economies.
    const int scale = skew.bonus_percent > 0 ? 100 : 1;
    const int news = weighted_choice(rng, static_cast<int>(economy.events.size()), [&](int i) {
        const auto& event = economy.events[i];
        const int weight = event.weight * scale;
        if (event.price_direction) {
            const int good = event.goods.front();
            if (economy.cities[run.city].price_percent[good] <= 0) return 0;
            const int minimum = economy.goods[good].price.min;
            const bool favored = run.day <= skew.early_days
                ? minimum <= skew.minimum_price_threshold : minimum >= skew.minimum_price_threshold;
            return weight + (favored ? event.weight * skew.bonus_percent : 0);
        }
        if (event.required_event >= 0 && run.last_event_day[event.required_event] == 0) {
            return 0;
        }
        const int last = run.last_event_day[i];
        const int wait = std::max(event.cooldown, event.duration);
        return last && (event.once || run.day - last < wait) ? 0 : weight;
    });
    if (news < 0) return;

    const auto& event = economy.events[news];
    run.last_event_day[news] = run.day;
    run.last_headline = news;
    run.pending_news = true;
    run.price_news.reset();
    if (event.price_direction) {
        const auto ratio = news_ratio(run.seed, run.day, event.price_direction);
        run.price_news = PriceNews{run.day, event.price_direction, ratio.numerator, ratio.denominator};
        // Scope and story draws must not alter ordinary event selection or market prices.
        constexpr std::uint64_t market_news_seed_salt = 0xe7037ed1a0b428dbULL;
        DayRandom market_rng(run.seed ^ market_news_seed_salt, run.day);
        const auto& rules = economy.market_wide_news;
        if (rules.chance_basis_points > 0 && market_rng.chance(rules.chance_basis_points)) {
            const auto& stories = event.price_direction > 0 ? rules.up : rules.down;
            run.price_news->all_goods = true;
            run.price_news->story = market_rng.roll(0, static_cast<int>(stories.size()) - 1);
        }
    } else {
        run.effects.push_back({news, run.day + event.duration - 1, event.local ? run.city : -1});
    }
}

// Build quotes from seeded base prices, city percentages, and applicable news effects.
void refresh_market(const Economy& economy, Run& run) {
    constexpr std::uint64_t price_seed_salt = 0xd1b54a32d192ed03ULL;
    std::mt19937_64 price_rng(day_seed(run.seed ^ price_seed_salt, run.day));
    const auto& city = economy.cities[run.city];
    const auto quote_limit = market_quote_limit(run.day);
    for (std::size_t i = 0; i < economy.goods.size(); ++i) {
        auto bounds = economy.goods[i].price;
        if (run.day >= ai_wars_day) {
            bounds.min = std::max(1, static_cast<int>(Money{bounds.min} * (100 - ai_wars_price_change) / 100));
            bounds.max = static_cast<int>(Money{bounds.max} * (100 + ai_wars_price_change) / 100);
        }
        Money price = Money{generate_commodity_price(bounds.min, bounds.max, price_rng)}
            * city.price_percent[i] / 100;
        if (city.price_percent[i] > 0) price = std::clamp<Money>(price, 1, quote_limit);
        for (const auto& effect : run.effects) {
            if (effect.city >= 0 && effect.city != run.city) {
                continue;
            }
            const auto& event = economy.events[effect.event];
            if (std::find(event.goods.begin(), event.goods.end(), static_cast<int>(i)) == event.goods.end()) {
                continue;
            }
            if (price > 0) {
                price = event.price_percent == 0 ? 0
                    : std::clamp<Money>(price * event.price_percent / 100, 1, quote_limit);
            }
        }
        // Keep the guaranteed opening commodity affordable on the first day.
        if (run.day == 1 && static_cast<int>(i) == economy.opening_good) {
            price = std::clamp<Money>(price, 1, run.cash);
        }
        run.market[i] = {price};
    }
    if (run.pending_news && run.last_headline >= 0) {
        const auto& event = economy.events[run.last_headline];
        if (event.price_direction) {
            const auto ratio = run.price_news
                ? PriceRatio{run.price_news->numerator, run.price_news->denominator}
                : news_ratio(run.seed, run.day, event.price_direction);
            for (std::size_t i = 0; i < run.market.size(); ++i) {
                if ((!run.price_news || !run.price_news->all_goods)
                    && static_cast<int>(i) != event.goods.front()) continue;
                auto& price = run.market[i].price;
                if (price <= 0) continue;
                price = std::clamp<Money>((price * ratio.numerator + ratio.denominator / 2)
                                          / ratio.denominator, 1, quote_limit);
            }
        }
    }
}

// Check that a good index belongs to this session's economy.
bool valid_good(const Session& session, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < session.economy.goods.size();
}

// Add the new lots and their total acquisition cost to the holding.
void add_lots(Holding& holding, int lots, Money cost) {
    holding.lots += lots;
    holding.cost += cost;
}

// Remove lots and reduce the carrying cost proportionally, rounding down to whole dollars.
void remove_lots(Holding& holding, int lots) {
    const int remaining = holding.lots - lots;
    holding.cost = holding.cost * remaining / holding.lots;
    holding.lots = remaining;
}

// Draw a valid index for the supplied story table.
template <typename T, std::size_t N>
int story_index(DayRandom& rng, const std::array<T, N>&) {
    return rng.roll(0, static_cast<int>(N) - 1);
}

// Deduct lost cargo and record the loss for the encounter summary.
void lose_cargo(Session& session, int good, int lots) {
    remove_lots(session.run.holdings[good], lots);
    session.run.encounter.cargo.push_back({good, lots});
}

// Pick eligible cargo and a story, then remove a rounded percentage of the chosen holding.
void cargo_loss_encounter(Session& session, DayRandom& rng, EncounterType type) {
    auto& run = session.run;
    const bool family = type == EncounterType::Family;
    const int good = weighted_choice(rng, static_cast<int>(run.holdings.size()), [&](int i) {
        return run.holdings[i].lots > 0 && (!family || !session.economy.goods[i].family_stories.empty());
    });
    if (family && good < 0) return;

    const int story = family
        ? rng.roll(0, static_cast<int>(session.economy.goods[good].family_stories.size()) - 1)
        : story_index(rng, encounter_text::mugging);
    run.encounter = {type, story, {}};
    if (good >= 0) {
        const int percent = family ? rng.roll(10, 25) : rng.roll(15, 35);
        const int owned = run.holdings[good].lots;
        lose_cargo(session, good, std::clamp((owned * percent + 50) / 100, 1, owned));
    }
}

// Add free cargo that fits in the jacket, with larger possible hauls for cheaper goods.
void find_stash(Session& session, DayRandom& rng) {
    auto& run = session.run;
    const auto& goods = session.economy.goods;
    const int free_slots = run.capacity - used_slots(session);
    if (free_slots <= 0) return;
    const int good = rng.roll(0, static_cast<int>(goods.size()) - 1);

    const Money price_sum = Money{goods[good].price.min} + goods[good].price.max;
    const auto dearer = std::count_if(goods.begin(), goods.end(), [price_sum](const Good& item) {
        return Money{item.price.min} + item.price.max > price_sum;
    });
    const int maximum = 1 + 11 * static_cast<int>(dearer)
        / std::max(1, static_cast<int>(goods.size()) - 1);
    const int lots = std::min(rng.roll(1, maximum), free_slots);
    add_lots(run.holdings[good], lots, 0);
    run.encounter = {EncounterType::Stash, story_index(rng, encounter_text::stash), {{good, lots}}};
}

// Reserve day 25 for the AI Wars. On other days, check for a patch before rolling ordinary events.
void select_arrival_event(Session& session) {
    auto& run = session.run;
    if (run.day == ai_wars_day) {
        run.last_headline = ai_wars_headline;
        run.pending_news = true;
        run.price_news.reset();
        return;
    }
    DayRandom rng(run.seed, run.day);
    if (run.exposed_exploits > 0 && run.day > run.last_exposed_day && rng.chance(patch_chance)) {
        --run.exposed_exploits;
        run.encounter = {EncounterType::Patch, 0, {}};
        return;
    }

    const int choice = weighted_choice(rng, static_cast<int>(arrival_odds.size()), [](int i) {
        return arrival_odds[i].weight;
    });
    const auto type = arrival_odds[choice].type;
    switch (type) {
        case EncounterType::News:
            select_news(session, rng);
            break;
        case EncounterType::BlackHat:
            run.encounter = {type, story_index(rng, encounter_text::black_hat), {}};
            break;
        case EncounterType::Mugging:
        case EncounterType::Family:
            cargo_loss_encounter(session, rng, type);
            break;
        case EncounterType::Stash:
            find_stash(session, rng);
            break;
        case EncounterType::Officer:
            if (used_slots(session) > 0) {
                run.encounter = {type, story_index(rng, encounter_text::officer), {}};
            }
            break;
        default:
            break;
    }
}

// Confiscate a random share of the lots carried in the jacket.
void confiscate_cargo(Session& session, DayRandom& rng) {
    auto& run = session.run;
    int remaining = std::max(1, (used_slots(session) * rng.roll(20, 40) + 50) / 100);
    while (remaining > 0) {
        const int good = weighted_choice(rng, static_cast<int>(run.holdings.size()), [&](int i) {
            return run.holdings[i].lots > 0;
        });
        if (good < 0) break;
        const int lots = std::min(run.holdings[good].lots, remaining);
        lose_cargo(session, good, lots);
        remaining -= lots;
    }
}

} // namespace

// Use the median of nine draws to favor prices near the center of the configured range.
int generate_commodity_price(int min_price, int max_price, std::mt19937_64& rng) {
    if (min_price == max_price) return min_price;
    if (min_price > max_price) std::swap(min_price, max_price);

    // The median of nine independent uniform draws follows Beta(5,5).
    std::array<double, 9> samples;
    for (auto& sample : samples) {
        // Map the high 53 bits to [0, 1) with one engine draw.
        sample = static_cast<double>(rng() >> 11) * 0x1p-53;
    }
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const double sample = samples[i];
        auto j = i;
        while (j > 0 && sample < samples[j - 1]) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = sample;
    }

    const double span = static_cast<double>(max_price) - min_price;
    return static_cast<int>(std::round(min_price + span * samples[4]));
}

// Allow only ASCII letters, digits, spaces, hyphens, and underscores.
bool name_character(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == ' ' || c == '-' || c == '_';
}

// Strip outer spaces and reject names outside the allowed length and character set.
std::string player_name(std::string_view name) {
    const auto first = name.find_first_not_of(' ');
    if (first == std::string_view::npos) {
        return {};
    }
    name = name.substr(first, name.find_last_not_of(' ') - first + 1);
    if (name.size() > max_name_length || !std::all_of(name.begin(), name.end(), name_character)) {
        return {};
    }
    return std::string(name);
}

// Initialize the run from economy defaults and populate day-one quotes without an arrival encounter.
Run start_run(const Economy& economy, std::string player, std::uint64_t seed) {
    player = player_name(player);
    if (player.empty()) {
        throw std::invalid_argument("A player name is required");
    }
    Run run{};
    run.player = std::move(player);
    run.seed = seed;
    run.day = 1;
    run.city = economy.start_city;
    run.cash = economy.starting_cash;
    run.jacket = economy.starting_jacket;
    run.capacity = economy.starting_capacity;
    run.market.resize(economy.goods.size());
    run.holdings.resize(economy.goods.size());
    run.last_event_day.resize(economy.events.size());
    refresh_market(economy, run);
    return run;
}

// Count one jacket slot per lot.
int used_slots(const Session& session) {
    int used = 0;
    for (const auto& holding : session.run.holdings) {
        used += holding.lots;
    }
    return used;
}

// Divide the holding's remaining cost by its lot count using integer division.
Money average_cost(const Holding& holding) {
    return holding.lots > 0 ? holding.cost / holding.lots : 0;
}

// Sort the visible goods by name or displayed price, breaking price ties by name.
std::vector<int> market_listing(const Session& session, SortOrder order, bool jacket) {
    std::vector<int> items;
    for (int good = 0; good < static_cast<int>(session.economy.goods.size()); ++good) {
        if (!jacket || session.run.holdings[good].lots > 0) items.push_back(good);
    }

    // Compare displayed short names in ASCII order, regardless of letter case.
    const auto name_less = [&session](int left, int right) {
        const auto& a = session.economy.goods[left].short_name;
        const auto& b = session.economy.goods[right].short_name;
        const auto fold = [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        };
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
            [&](unsigned char x, unsigned char y) { return fold(x) < fold(y); });
    };
    // The jacket shows average acquisition cost while the market shows the current quote.
    const auto price = [&](int good) {
        return jacket ? average_cost(session.run.holdings[good]) : session.run.market[good].price;
    };
    std::stable_sort(items.begin(), items.end(), [&](int left, int right) {
        if (order == SortOrder::PriceDescending || order == SortOrder::PriceAscending) {
            const auto a = price(left);
            const auto b = price(right);
            if (a != b) return order == SortOrder::PriceAscending ? a < b : a > b;
        }
        return order == SortOrder::NameDescending ? name_less(right, left) : name_less(left, right);
    });
    return items;
}

// Block trades during pending events and limit purchases by available space and cash.
int trade_limit(const Session& session, int good, bool buy) {
    const auto& run = session.run;
    if (!valid_good(session, good) || run.pending_news || run.pending_jacket >= 0
        || run.encounter.type != EncounterType::None) {
        return 0;
    }
    if (!buy) {
        return run.holdings[good].lots;
    }
    const int room = run.capacity - used_slots(session);
    const auto price = run.market[good].price;
    return price == 0 ? room : static_cast<int>(std::min<Money>(room, run.cash / price));
}

// Validate the quantity before changing cash and the holding's lots and cost.
bool trade(Session& session, int good, bool buy, int lots) {
    if (lots <= 0 || lots > trade_limit(session, good, buy)) {
        return false;
    }
    auto& run = session.run;
    auto& holding = run.holdings[good];
    const Money total = run.market[good].price * lots;
    if (buy) {
        run.cash -= total;
        add_lots(holding, lots, total);
    } else {
        run.cash += total;
        remove_lots(holding, lots);
    }
    return true;
}

// Require a different city, time remaining, and no unresolved events or offers.
bool can_jet(const Session& session, int city) {
    const auto& run = session.run;
    return city >= 0 && static_cast<std::size_t>(city) < session.economy.cities.size()
        && city != run.city && run.day < session.economy.days && !run.pending_news
        && run.pending_jacket < 0 && run.encounter.type == EncounterType::None;
}

// Discard expired effects and select the arrival event before pricing the new market.
bool jet(Session& session, int city) {
    if (!can_jet(session, city)) return false;

    auto& run = session.run;
    ++run.day;
    run.city = city;
    auto& effects = run.effects;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [&run](const NewsEffect& effect) {
        return effect.until_day < run.day;
    }), effects.end());
    select_arrival_event(session);
    refresh_market(session.economy, run);
    return true;
}

// Clear the offer after a decline or after paying for a jacket with more capacity.
bool answer_jacket(Session& session, bool buy) {
    auto& run = session.run;
    if (run.pending_jacket < 0) {
        return false;
    }
    const auto& offer = session.economy.jackets[run.pending_jacket];
    if (buy) {
        if (run.cash < offer.cost || offer.capacity <= run.capacity) {
            return false;
        }
        run.cash -= offer.cost;
        run.capacity = offer.capacity;
        run.jacket = offer.name;
    }
    run.pending_jacket = -1;
    return true;
}

// Enforce the Black Hat encounter, cash requirement, and jacket capacity limit.
bool can_buy_equipment(const Session& session, Equipment equipment) {
    const auto& run = session.run;
    if (run.encounter.type != EncounterType::BlackHat) return false;
    if (equipment == Equipment::Exploit) return run.cash >= exploit_price;
    return run.cash >= jacket_upgrade_price && run.capacity <= max_cargo - jacket_upgrade_slots;
}

// Apply one equipment purchase to the player's cash and inventory or jacket capacity.
bool buy_equipment(Session& session, Equipment equipment) {
    if (!can_buy_equipment(session, equipment)) return false;
    auto& run = session.run;
    if (equipment == Equipment::Exploit) {
        run.cash -= exploit_price;
        ++run.unused_exploits;
    } else {
        run.cash -= jacket_upgrade_price;
        run.capacity += jacket_upgrade_slots;
    }
    return true;
}

// Resolve the in-game fight with an available exploit or roll for escape and cargo loss.
bool resolve_officer(Session& session, bool fight) {
    auto& run = session.run;
    if (run.encounter.type != EncounterType::Officer) return false;
    constexpr std::uint64_t officer_seed_salt = 0xe7037ed1a0b428dbULL;
    DayRandom rng(run.seed ^ officer_seed_salt, run.day);
    if (fight) {
        // Reuse a previously exposed exploit until a later arrival patches it.
        if (!run.exposed_exploits) {
            if (!run.unused_exploits) return false;
            --run.unused_exploits;
            ++run.exposed_exploits;
            run.last_exposed_day = run.day;
        }
        run.encounter = {EncounterType::Fight, 0, {}};
    } else if (rng.chance(escape_chance)) {
        run.encounter = {EncounterType::Escape, story_index(rng, encounter_text::escape), {}};
    } else {
        run.encounter = {EncounterType::Confiscation, story_index(rng, encounter_text::confiscation), {}};
        confiscate_cargo(session, rng);
    }
    return true;
}

// Return a fixed multiplier for older rules or reproduce the seeded price swing for newer ones.
int news_price_percent(const Run& run, const Event& event) {
    if (!event.price_direction) return event.price_percent;
    // New headlines retain the exact effect; older saves reproduce it from the saved day and seed.
    const auto ratio = run.price_news
        ? PriceRatio{run.price_news->numerator, run.price_news->denominator}
        : news_ratio(run.seed, run.last_event_day[run.last_headline], event.price_direction);
    return (100 * ratio.numerator + ratio.denominator / 2) / ratio.denominator;
}

// Select the saved story variant and fill in quantities for encounters that need them.
std::string encounter_story(const Session& session) {
    const auto& run = session.run;
    const auto& encounter = run.encounter;
    const auto story = encounter.story;
    switch (encounter.type) {
        case EncounterType::BlackHat: return encounter_text::black_hat[story];
        case EncounterType::Mugging:
            return encounter.cargo.empty() ? encounter_text::mugging[story].empty
                                           : encounter_text::mugging[story].carrying;
        case EncounterType::Family: {
            const auto& loss = encounter.cargo.front();
            auto message = session.economy.goods[loss.good].family_stories[story].text;
            const auto quantity = std::to_string(loss.lots);
            for (auto pos = message.find("{qty}"); pos != std::string::npos;
                 pos = message.find("{qty}", pos + quantity.size())) {
                message.replace(pos, 5, quantity);
            }
            return message;
        }
        case EncounterType::Stash: return encounter_text::stash[story];
        case EncounterType::Officer: return encounter_text::officer[story];
        case EncounterType::Patch:
            return "Your 0day exploit has been patched! Some overachieving security researcher just ruined your day. "
                "You have " + std::to_string(run.unused_exploits + run.exposed_exploits) + " exploits remaining.";
        case EncounterType::Fight: return encounter_text::fight;
        case EncounterType::Escape: return encounter_text::escape[story];
        case EncounterType::Confiscation: return encounter_text::confiscation[story];
        default: return {};
    }
}

// Round down the discounted price of each lot before totaling the remaining inventory.
Money final_buyout(const Session& session) {
    Money total = 0;
    for (std::size_t i = 0; i < session.run.holdings.size(); ++i) {
        const Money price = session.run.market[i].price;
        total += (price * session.economy.buyout_percent / 100) * session.run.holdings[i].lots;
    }
    return total;
}

// Insert ahead of lower scores, keeping earlier entries first on ties and trimming to the limit.
std::optional<std::size_t> record_score(Profile& profile, Score score) {
    auto& scores = profile.scores;
    const auto position = std::find_if(scores.begin(), scores.end(), [&score](const Score& existing) {
        return score.value > existing.value;
    });
    const auto rank = static_cast<std::size_t>(position - scores.begin());
    if (rank >= score_count) {
        return std::nullopt;
    }
    scores.insert(position, std::move(score));
    if (scores.size() > score_count) {
        scores.resize(score_count);
    }
    return rank;
}

// Insert commas in groups of three digits while keeping the sign at the front.
std::string format_number(Money value) {
    auto digits = std::to_string(value);
    const std::size_t first_digit = value < 0 ? 1 : 0;
    for (auto position = digits.size(); position > first_digit + 3;) {
        position -= 3;
        digits.insert(position, 1, ',');
    }
    return digits;
}

// Combine a player's name and formatted score into plain text for sharing.
std::string share_score(const Score& score) {
    return score.player + " - Highscore: " + format_number(score.value);
}

} // namespace wars
