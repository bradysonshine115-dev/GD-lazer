#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer::levels {

// A level you can play from song select: one of RobTop's (official) or a
// saved online level.
struct Entry {
    geode::Ref<GJGameLevel> level;
    bool official = false;
    int id = 0;
    std::string name;
    std::string creator;
    std::string songTitle;
    std::string songArtist;
    int songID = 0;            // custom song ID, or MusicPlayer::officialSongID for GD's tracks
    std::string songPath;      // empty if the song isn't downloaded (see resolve())
    int difficulty = 0;        // GJDifficultySprite frame (-1 auto, 0 N/A, 1-5, 6-10 demons)
    int stars = 0;
    int normalPercent = 0;
    int practicePercent = 0;
    int coins = 0;             // coins in the level
    int coinsCollected = 0;    // see resolve()
    bool coinsVerified = false;
    int length = 0;            // 0 tiny .. 4 XL, 5 platformer
    bool platformer = false;   // rated in moons instead of stars
    int bestTime = 0;          // platformer best time, ms (0 = none)
    int folder = 0;            // GD's saved-level folder (0 = none)
    std::string search;        // lower-cased name, creator and song, for filtering
    bool resolved = false;     // songPath and coinsCollected filled in
    // Map packs (Kind::MapPacks): a pack's header row (level is null, name is
    // the pack's) or one of its levels, both with the pack's index.
    int pack = -1;
    bool packHeader = false;
};

// An entry for any level object (a pack's levels, fetched online).
Entry fromLevel(GJGameLevel* level, bool official);

// Your saved copy of a level carries its data and your progress. GD also
// keeps nameless stubs for levels you've played but not saved: those only
// lend their progress to the fetched copy, which is returned.
GJGameLevel* withSavedCopy(GJGameLevel* level);
// Whether the level object is your saved copy (its data and progress are kept).
bool isSaved(GJGameLevel* level);

// all() only reads what filtering and sorting need, so song select opens fast
// with thousands of saved levels. Whether the song is downloaded (a file check
// per level) and which coins you have are filled in here, once a level is
// actually shown or played. Cheap to call again.
void resolve(Entry& entry);

// GD 2.2 has two kinds of level: classic (stars) and platformer (moons).
// MapPacks: RobTop's map packs, each a header with its levels under it.
// Online: GD's online lists (see OnlineBrowse.hpp), nothing from here.
enum class Kind { Classic, Platformer, MapPacks, Online };

// RobTop's levels of that kind in order, then every saved online level of that kind.
// Classic: the main levels. Platformer: the Tower's levels (vanilla hides them
// behind the last page of the main levels), each once the one before is beaten.
std::vector<Entry> all(Kind kind);

// Official levels first by number, saved ones newest-saved first (GD's order).
enum class Sort { Default, Title, Difficulty, Progress };

// Hearted in GD (the level page's heart). Saved levels only.
bool favorited(Entry const& entry);
void setFavorited(Entry const& entry, bool favorited);

// A level's song title: the custom song's ("Song 123" until GD knows its
// name), or the name of GD's own track.
std::string songTitle(GJGameLevel* level);

// Name of a saved-levels folder ("folder 3" if GD has no name for it).
std::string folderName(int folder);

// Saved levels that "delete unhearted" removes: not hearted and not in a folder
// (of either kind). deleteUnhearted() returns how many it deleted.
int countUnhearted();
int deleteUnhearted();
// Deletes one saved level (GD's level page delete). Not for official levels.
void deleteLevel(Entry const& entry);

cocos2d::ccColor3B difficultyColor(int difficulty);
char const* difficultyName(int difficulty);
char const* lengthName(int length);

// Whether the level can go straight to gameplay (level data and song present);
// otherwise GD's level page downloads what's missing.
bool readyToPlay(Entry const& entry);

} // namespace lazer::levels
