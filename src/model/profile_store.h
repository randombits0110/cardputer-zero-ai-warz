// Declares persistent storage for the local profile, leaderboard, and active run.

#pragma once

#include "game.h"

#include <memory>
#include <stdexcept>

namespace wars {

// Find the state directory from an override or the platform's user directories.
std::filesystem::path default_state_directory();

class ProfileInUse : public std::runtime_error {
public:
    // Identify a lock conflict so startup can tell the player another copy is using the profile.
    ProfileInUse() : std::runtime_error("AI Wars state is already in use") {}
};

enum class ProfileRecovery { None, Backup, Fresh };

class ProfileStore {
public:
    // Create the state directory as needed and acquire its exclusive profile lock.
    explicit ProfileStore(const std::filesystem::path& directory);
    // Release the profile lock when the store goes out of scope.
    ~ProfileStore();

    // Preserve invalid profiles before restoring a backup or starting fresh.
    Profile load(ProfileRecovery* recovery = nullptr) const;
    // Replace the profile with a serialized snapshot of the supplied state.
    void save(const Profile& profile) const;

private:
    struct Lock;
    std::filesystem::path file_;
    std::unique_ptr<Lock> lock_;
};

} // namespace wars
