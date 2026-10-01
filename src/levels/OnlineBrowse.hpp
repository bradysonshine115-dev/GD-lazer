#pragma once

#include "LevelLibrary.hpp"
#include "MapPacks.hpp"

#include <Geode/Geode.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// GD's online level lists (search, featured, hall of fame, magic, recent...)
// and its level lists, for song select's online pages: one request at a time,
// its pages fetched as they're scrolled to, and kept for the session so a
// round trip into a level (or its page) comes back to the same results.
// GD does the fetching (GameLevelManager, with this as its delegate), and
// caches pages for a while, so coming back is instant.
namespace lazer::browse {

// GD's search screen's filters, the way its search object carries them.
struct Filters {
    uint32_t difficulty = 0;  // bits: N/A, easy, normal, hard, harder, insane, demon, auto
    int demon = 0;            // 0 any, 1-5 easy..extreme (with demons alone)
    uint32_t length = 0;      // bits: tiny, short, medium, long, XL, platformer
    uint32_t general = 0;     // bits: General
    int played = 0;           // 0 any, 1 uncompleted, 2 completed
    bool operator==(Filters const&) const = default;
    // How many are set (the badge on the filters button).
    int count() const;
    void clear() { *this = {}; }
};
// The "Rating" row's options are the first six bits, "Extras" the next three.
enum General { RATED, UNRATED, FEATURED, EPIC, LEGENDARY, MYTHIC, ORIGINAL, COINS, TWO_PLAYER };
inline constexpr int DIFF_DEMON = 6;
inline constexpr int EXTRAS_FIRST = ORIGINAL;

// What a page shows: the search page (a query, or one of GD's quick searches
// as its sort), or one of GD's fixed lists (featured, hall of fame...).
struct Request {
    SearchType type = SearchType::Downloaded;  // the fixed list, or the sort without a query
    std::string query;                         // search page only: searches by name or ID when set
    Filters filters;
    bool lists = false;                        // level lists instead of levels
    bool searchPage = false;
    bool operator==(Request const&) const = default;
    // GD's search object for `page` of this.
    GJSearchObject* make(int page) const;
    // What GD's browser was opened with.
    static Request fromSearch(GJSearchObject* search);
    // Whether GD's browser for this search is one of these pages.
    static bool wants(GJSearchObject* search);
};

// One of GD's fixed lists (featured, hall of fame...), and the search page.
inline Request pageRequest(SearchType type, bool lists = false) {
    Request r;
    r.type = type;
    r.lists = lists;
    return r;
}
inline Request searchRequest(std::string query) {
    Request r;
    r.searchPage = true;
    r.query = std::move(query);
    return r;
}

enum class State { Idle, Loading, Loaded, Failed };

// The pages loaded so far of the request.
struct Results {
    Request request;
    State state = State::Idle;         // of the page in flight, or the last one
    std::vector<levels::Entry> levels; // the levels, in GD's order
    std::vector<packs::Pack> lists;    // or the lists, as packs (their levels under them)
    int firstPage = 0;                 // the first page here (a jump starts further in)
    int pagesLoaded = 0;
    int total = -1;                    // items on the server (-1: not known yet)
    bool more = true;                  // another page may exist
    int generation = 0;                // bumps with every fresh request
    size_t count() const { return request.lists ? lists.size() : levels.size(); }
    int lastPage() const { return firstPage + pagesLoaded - 1; }
};

inline constexpr int PER_PAGE = 10;  // GD's pages

Results const& results();
// The lists, for the pack rows to open (their levels fill in).
std::vector<packs::Pack>& lists();

// A fresh request from `page`: the results go and that page is asked for.
// The same request as the one shown (with results) is kept, unless `force`.
void open(Request const& request, int page = 0, bool force = false);
// The page after the last one here (nothing while one is on its way).
void loadMore();
// The request again from its first page, fresh from the server.
void refresh();
// A list's levels (fetched by ID, like a map pack's); nothing while they're
// on their way or here.
void loadListLevels(size_t index);
bool loadingListLevels();
// A page is on its way.
bool loading();
// A request another of GD's screens cut off (they take GD's delegate for
// themselves) is sent again.
void resume();
// The page an item (a level, or a list) is on, 0-based.
int pageOf(size_t index);
// Every level's saved copy and progress again (after a play).
void refreshProgress();

// Who to tell when the results change (the song select that's up). Clear it
// with nullptr when leaving.
void setListener(std::function<void()> listener);

} // namespace lazer::browse
