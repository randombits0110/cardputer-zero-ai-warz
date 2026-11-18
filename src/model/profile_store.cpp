// Stores one local profile under an exclusive lock and reads both supported save formats.

#include "profile_store.h"

#include "json_input.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wars {
namespace {

namespace fs = std::filesystem;

// Validate a saved integer before converting it to a signed money value.
Money integer(const Json& value, Money minimum, Money maximum) {
    if (!value.is_number_integer()) {
        throw std::runtime_error("Invalid save number");
    }
    if (value.is_number_unsigned() && value.get<std::uint64_t>() > static_cast<std::uint64_t>(maximum)) {
        throw std::runtime_error("Save number is too large");
    }
    const auto result = value.get<Money>();
    if (result < minimum || result > maximum) {
        throw std::runtime_error("Save number out of bounds");
    }
    return result;
}

// Validate a saved number within an int-sized range before narrowing it.
int small(const Json& value, int minimum, int maximum) {
    return static_cast<int>(integer(value, minimum, maximum));
}

// Read the full unsigned run seed from JSON.
std::uint64_t seed(const Json& value) {
    if (!value.is_number_unsigned()) {
        throw std::runtime_error("Invalid run seed");
    }
    return value.get<std::uint64_t>();
}

// Require the saved spelling to match a valid normalized name, with an optional empty value.
std::string saved_name(const Json& value, bool optional = false) {
    auto name = value.get<std::string>();
    if ((optional && name.empty()) || (!name.empty() && name == player_name(name))) {
        return name;
    }
    throw std::runtime_error("Invalid saved player name");
}

// Check the size of a saved array before callers index its entries.
const Json& array(const Json& value, std::size_t min, std::size_t max) {
    if (!value.is_array() || value.size() < min || value.size() > max) {
        throw std::runtime_error("Invalid save array size");
    }
    return value;
}

// Serialize a run's market and inventory state together with its economy snapshot.
Json encode_session(const Session& session) {
    const auto& run = session.run;
    auto market = Json::array();
    auto holdings = Json::array();
    auto effects = Json::array();
    for (const auto& quote : run.market) {
        market.push_back(quote.price);
    }
    for (const auto& holding : run.holdings) {
        holdings.push_back({holding.lots, holding.cost});
    }
    for (const auto& effect : run.effects) {
        effects.push_back({effect.event, effect.until_day, effect.city});
    }
    Json result = {
        {"economy", json_input::parse(session.economy.document, json_input::profile_bytes)},
        {"player", run.player}, {"seed", run.seed}, {"day", run.day}, {"city", run.city},
        {"cash", run.cash}, {"jacket", run.jacket}, {"capacity", run.capacity},
        {"market", std::move(market)}, {"holdings", std::move(holdings)},
        {"effects", std::move(effects)}, {"last_event_day", run.last_event_day},
        {"last_headline", run.last_headline}, {"pending_news", run.pending_news},
        {"pending_jacket", run.pending_jacket},
    };
    if (run.price_news) {
        const auto& news = *run.price_news;
        result["price_news"] = {
            {"day", news.day}, {"direction", news.direction},
            {"numerator", news.numerator}, {"denominator", news.denominator},
            {"all_goods", news.all_goods}, {"story", news.story},
        };
    }
    return result;
}

// Rebuild a run and validate saved indices and quantities against its own economy.
Session decode_session(Json& json, int version) {
    Session session{decode_economy(std::move(json.at("economy"))), {}};
    const auto& economy = session.economy;
    auto& run = session.run;
    const auto goods = economy.goods.size();
    const auto events = economy.events.size();
    const int last_event_index = static_cast<int>(events) - 1;
    const int last_city_index = static_cast<int>(economy.cities.size()) - 1;
    run.player = saved_name(json.at("player"));
    run.seed = seed(json.at("seed"));
    run.day = small(json.at("day"), 1, economy.days);
    run.city = small(json.at("city"), 0, last_city_index);
    run.cash = integer(json.at("cash"), 0, max_score);
    run.jacket = json.at("jacket").get<std::string>();
    if (run.jacket.empty() || run.jacket.size() > 30) {
        throw std::runtime_error("Invalid saved jacket");
    }
    run.capacity = small(json.at("capacity"), 1, max_cargo);
    run.last_headline = small(json.at("last_headline"), ai_wars_headline, last_event_index);
    run.pending_news = json.at("pending_news").get<bool>();
    run.pending_jacket = small(json.at("pending_jacket"), -1, static_cast<int>(economy.jackets.size()) - 1);
    if (run.pending_news && run.last_headline == -1) {
        throw std::runtime_error("Missing pending headline");
    }
    if (run.last_headline == ai_wars_headline
        && (run.day < ai_wars_day || (run.pending_news && run.day != ai_wars_day))) {
        throw std::runtime_error("Invalid AI Wars headline day");
    }
    const auto quote_limit = market_quote_limit(run.day);
    // Version 1 stored a pair per commodity quote. Only the first value supplies the current price.
    for (const auto& quote : array(json.at("market"), goods, goods)) {
        if (version == 1) {
            array(quote, 2, 2);
            run.market.push_back({integer(quote.at(0), 0, quote_limit)});
        } else {
            run.market.push_back({integer(quote, 0, quote_limit)});
        }
    }
    for (const auto& holding : array(json.at("holdings"), goods, goods)) {
        array(holding, 2, 2);
        run.holdings.push_back({
            small(holding.at(0), 0, run.capacity), integer(holding.at(1), 0, Money{quote_limit} * max_cargo),
        });
    }
    for (const auto& effect : array(json.at("effects"), 0, events)) {
        array(effect, 3, 3);
        run.effects.push_back({
            small(effect.at(0), 0, last_event_index), small(effect.at(1), run.day, economy.days + 364),
            small(effect.at(2), -1, last_city_index),
        });
    }
    for (const auto& day : array(json.at("last_event_day"), events, events)) {
        run.last_event_day.push_back(small(day, 0, run.day));
    }
    if (json.contains("price_news")) {
        const auto& news = json.at("price_news");
        if (run.last_headline < 0 || !economy.events[run.last_headline].price_direction
            || !news.at("all_goods").is_boolean()) {
            throw std::runtime_error("Invalid saved price headline");
        }
        PriceNews resolved{
            small(news.at("day"), 1, run.day), small(news.at("direction"), -1, 1),
            small(news.at("numerator"), 0, 1600), small(news.at("denominator"), 1, 1600),
            news.at("all_goods").get<bool>(), -1,
        };
        const auto& stories = resolved.direction > 0 ? economy.market_wide_news.up : economy.market_wide_news.down;
        resolved.story = resolved.all_goods
            ? small(news.at("story"), 0, static_cast<int>(stories.size()) - 1)
            : small(news.at("story"), -1, -1);
        if (resolved.day != run.last_event_day[run.last_headline]
            || resolved.direction != economy.events[run.last_headline].price_direction
            || (resolved.direction > 0 && resolved.numerator <= resolved.denominator)
            || (resolved.direction < 0 && resolved.numerator >= resolved.denominator)) {
            throw std::runtime_error("Inconsistent saved price headline");
        }
        run.price_news = resolved;
    }
    if (used_slots(session) > run.capacity) {
        throw std::runtime_error("Saved inventory exceeds jacket capacity");
    }
    return session;
}

// Read a nonempty environment setting as a path and return an empty path if unset.
fs::path env_directory(const char* name) {
    if (const char* value = std::getenv(name); value && *value) {
        return fs::path(value);
    }
    return {};
}

// Flush a sibling temporary file before replacing the profile so readers see a complete document.
void atomic_write(const fs::path& file, const std::string& document) {
    auto temporary = file;
#ifdef _WIN32
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
    std::FILE* output = _wfopen(temporary.c_str(), L"wbx");
#else
    temporary += ".tmp.XXXXXX";
    auto pattern = temporary.native();
    const int fd = mkostemp(pattern.data(), O_CLOEXEC);
    std::FILE* output = fd < 0 ? nullptr : fdopen(fd, "wb");
    if (fd >= 0 && !output) {
        close(fd);
        unlink(pattern.c_str());
    }
    temporary = std::move(pattern);
#endif
    if (!output) {
        throw std::runtime_error("Could not create save file");
    }
    bool flushed = std::fwrite(document.data(), 1, document.size(), output) == document.size()
        && std::fflush(output) == 0;
#ifdef _WIN32
    flushed = flushed && _commit(_fileno(output)) == 0;
#else
    flushed = flushed && fsync(fileno(output)) == 0;
#endif
    const bool closed = std::fclose(output) == 0;
#ifdef _WIN32
    const bool replaced = flushed && closed &&
        MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    const bool replaced = flushed && closed && std::rename(temporary.c_str(), file.c_str()) == 0;
#endif
    if (!replaced) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw std::runtime_error("Could not replace save file");
    }
#ifndef _WIN32
    // Sync the containing directory on Linux so the rename can persist across a restart.
    const int directory = open(file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        fsync(directory);
        close(directory);
    }
#endif
}

#ifndef _WIN32
bool is_private_file(int fd) {
    struct stat info{};
    return fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid()
        && info.st_nlink == 1 && ((info.st_mode & 0777) == 0600 || fchmod(fd, 0600) == 0);
}

void ensure_private_directory(const fs::path& path) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
        throw std::runtime_error("Cannot create AI Wars state directory");
    }
    struct stat info{};
    if (lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != geteuid()
        || (info.st_mode & 0022) != 0) {
        throw std::runtime_error("AI Wars state needs a user-owned directory without shared write access");
    }
}
#endif

bool profile_exists(const fs::path& path) {
#ifdef _WIN32
    const auto status = fs::symlink_status(path);
    if (!fs::exists(status)) return false;
    if (fs::is_regular_file(status)) return true;
#else
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) return false;
    if (fd >= 0) {
        const bool valid = is_private_file(fd);
        close(fd);
        if (valid) return true;
    }
#endif
    throw std::runtime_error("Cannot access AI Wars profile as a private regular file");
}

fs::path profile_backup(const fs::path& file) {
    auto backup = file;
    backup += ".bak";
    return backup;
}

void preserve_invalid_profile(const fs::path& file) {
    for (int index = 0; index < 100; ++index) {
        const auto suffix = index == 0 ? std::string{} : "-" + std::to_string(index);
        const auto directory = file.parent_path() / ("profile-recovery" + suffix);
#ifdef _WIN32
        const bool created = fs::create_directory(directory);
#else
        const bool created = mkdir(directory.c_str(), 0700) == 0;
        if (!created && errno != EEXIST) {
            throw std::runtime_error("Cannot create AI Wars recovery directory");
        }
#endif
        if (created) {
            fs::rename(file, directory / file.filename());
            return;
        }
    }
    throw std::runtime_error("Cannot preserve invalid AI Wars profile");
}

} // namespace

struct ProfileStore::Lock {
    // Hold a nonblocking exclusive lock for the store's lifetime and report another owner separately.
    explicit Lock(const fs::path& path) {
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_SHARING_VIOLATION) throw ProfileInUse();
            throw std::runtime_error("Cannot open AI Wars state lock");
        }
#else
        fd = open(path.c_str(), O_RDWR | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            throw std::runtime_error("Cannot open AI Wars state lock");
        }
        if (!is_private_file(fd)) {
            close(fd);
            throw std::runtime_error("AI Wars state lock must be a private regular file");
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            const auto error = errno;
            close(fd);
            if (error == EWOULDBLOCK) throw ProfileInUse();
            throw std::runtime_error("Cannot lock AI Wars state");
        }
#endif
    }

    // Close the handle or descriptor that holds the operating system lock.
    ~Lock() {
#ifdef _WIN32
        CloseHandle(handle);
#else
        close(fd);
#endif
    }

#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
};

// Resolve the override first, then look up a per-user state directory.
fs::path default_state_directory() {
    const auto override = env_directory("AI_WARS_STATE_DIR");
    if (!override.empty()) {
        return fs::absolute(override);
    }
#ifdef _WIN32
    const auto local = env_directory("LOCALAPPDATA");
    if (!local.empty()) {
        return local / "AI Wars";
    }
#else
    const auto xdg = env_directory("XDG_STATE_HOME");
    if (xdg.is_absolute()) {
        return xdg / "ai-wars";
    }
    const auto home = env_directory("HOME");
    if (home.is_absolute()) {
        return home / ".local" / "state" / "ai-wars";
    }
    if (const auto* user = getpwuid(geteuid()); user && user->pw_dir && *user->pw_dir) {
        return fs::path(user->pw_dir) / ".local" / "state" / "ai-wars";
    }
#endif
    throw std::runtime_error("Set AI_WARS_STATE_DIR to a writable directory");
}

// Set the profile path and lock it before allowing reads or writes.
ProfileStore::ProfileStore(const fs::path& directory) : file_(directory / "profile.json") {
#ifdef _WIN32
    fs::create_directories(directory);
#else
    ensure_private_directory(directory);
#endif
    lock_ = std::make_unique<Lock>(directory / "profile.lock");
}

// Destroy the lock with its complete type so the operating system lock is released.
ProfileStore::~ProfileStore() = default;

namespace {

Profile decode_profile(Json data) {
    const int version = small(data.at("version"), 1, 2);
    Profile profile;
    profile.last_player = saved_name(data.at("last_player"), true);
    if (version >= 2) {
        profile.sort_order = static_cast<SortOrder>(small(data.at("sort_order"), 0, 3));
    }
    profile.sound_enabled = data.value("sound_enabled", true);
    for (const auto& score : array(data.at("scores"), 0, score_count)) {
        profile.scores.push_back({saved_name(score.at("player")), integer(score.at("value"), 0, max_score),
                                  seed(score.at("seed"))});
    }
    std::stable_sort(profile.scores.begin(), profile.scores.end(), [](const Score& left, const Score& right) {
        return left.value > right.value;
    });
    if (!data.at("active").is_null()) {
        profile.active = decode_session(data.at("active"), version);
    }
    return profile;
}

std::optional<Profile> read_profile(const fs::path& path, std::string& document) {
    try {
        document = json_input::read(path, json_input::profile_bytes);
    } catch (const json_input::InvalidDocument&) {
        return std::nullopt;
    }
    try {
        return decode_profile(json_input::parse(document, json_input::profile_bytes));
    } catch (const Json::exception&) {
        return std::nullopt;
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

// Keep a known-good snapshot without adding another flush to each gameplay save.
void refresh_backup(const fs::path& backup, const std::string& document) {
    try {
        atomic_write(backup, document);
    } catch (const std::runtime_error& error) {
        std::fprintf(stderr, "AI Wars backup failed: %s\n", error.what());
    }
}

} // namespace

Profile ProfileStore::load(ProfileRecovery* recovery) const {
    if (recovery) *recovery = ProfileRecovery::None;
    const auto backup = profile_backup(file_);
    std::string document;
    if (profile_exists(file_)) {
        if (auto profile = read_profile(file_, document)) {
            refresh_backup(backup, document);
            return std::move(*profile);
        }
        preserve_invalid_profile(file_);
        if (recovery) *recovery = ProfileRecovery::Fresh;
    }
    if (profile_exists(backup)) {
        if (auto profile = read_profile(backup, document)) {
            atomic_write(file_, document);
            if (recovery) *recovery = ProfileRecovery::Backup;
            return std::move(*profile);
        }
    }
    return {};
}

// Write the current profile schema as one document under the existing profile lock.
void ProfileStore::save(const Profile& profile) const {
    auto scores = Json::array();
    for (const auto& score : profile.scores) {
        scores.push_back({{"player", score.player}, {"value", score.value}, {"seed", score.seed}});
    }
    const Json data{
        {"version", 2}, {"last_player", profile.last_player}, {"scores", std::move(scores)},
        {"sort_order", static_cast<int>(profile.sort_order)},
        {"sound_enabled", profile.sound_enabled},
        {"active", profile.active ? encode_session(*profile.active) : Json(nullptr)},
    };
    const auto document = data.dump() + '\n';
    if (document.size() > json_input::profile_bytes) {
        throw json_input::InvalidDocument("Profile exceeds its size limit");
    }
    atomic_write(file_, document);
    const auto backup = profile_backup(file_);
    std::error_code error;
    if (!fs::exists(fs::symlink_status(backup, error))) refresh_backup(backup, document);
}

} // namespace wars
