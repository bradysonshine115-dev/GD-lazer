#include "OnlineBrowse.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <unordered_set>

using namespace geode::prelude;

namespace lazer::browse {

namespace {
    // GD's demon filter takes GJDifficulty's demon values, the way its own
    // DemonFilterSelectLayer hands them over: easy 7, medium 8, hard 6,
    // insane 9, extreme 10 (the server ignores anything else). Indexed by
    // the "Demon" option (0 any, 1-5 easy..extreme).
    constexpr std::array<int, 6> DEMON_FILTER_VALUES {0, 7, 8, 6, 9, 10};
    int demonOptionFor(int gdValue) {
        for (int i = 1; i < static_cast<int>(DEMON_FILTER_VALUES.size()); i++) {
            if (DEMON_FILTER_VALUES[i] == gdValue) return i;
        }
        return 0;
    }

    // The quick searches GD's search screen offers (the search page's sorts).
    bool quickType(SearchType type) {
        switch (type) {
            case SearchType::Search:
            case SearchType::Downloaded:
            case SearchType::MostLiked:
            case SearchType::Trending:
            case SearchType::Recent:
            case SearchType::Magic:
            case SearchType::Awarded:
                return true;
            default:
                return false;
        }
    }

    std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // GJDifficultySprite frame for a list's difficulty (GJDifficulty's
    // numbers), the way levels::Entry numbers them.
    int listDifficultyFrame(int d) {
        if (d == -1) return 0;  // N/A
        if (d == 0) return -1;  // auto
        return std::clamp(d, 1, 10);
    }

    packs::Pack makeListPack(GJLevelList* list) {
        packs::Pack p;
        p.list = list;
        p.id = list->m_listID;
        p.name = list->m_listName;
        p.creator = list->m_creatorName;
        p.difficulty = listDifficultyFrame(list->m_difficulty);
        p.diamonds = list->m_diamonds;
        p.levelsToClaim = list->m_levelsToClaim;
        p.downloads = list->m_downloads;
        p.likes = list->m_likes;
        for (int id : list->m_levels) if (id > 0) p.levelIDs.push_back(id);
        auto colour = levels::difficultyColor(p.difficulty);
        p.textColor = {255, 255, 255};
        p.barColor = colour;
        p.completed = list->completedLevels();
        p.search = lower(p.name);
        return p;
    }

    void refreshListProgress(packs::Pack& p) {
        if (p.list) p.completed = p.list->completedLevels();
        for (auto& e : p.levels) {
            auto level = e.level ? levels::withSavedCopy(e.level) : nullptr;
            if (!level) continue;
            auto fresh = levels::fromLevel(level, false);
            fresh.pack = e.pack;
            e = fresh;
        }
    }

    // The session's results, and GD's delegate for their requests: the
    // request's pages, and any list's levels (by ID, like a map pack's).
    class Store : public LevelManagerDelegate {
    public:
        static Store& get() {
            static Store store;
            return store;
        }

        Results results;
        std::function<void()> listener;
        std::string pageKey;          // the page in flight
        int pendingTotal = -1, pendingEnd = -1; // from the page info, for the page in flight
        std::string listKey;          // the list levels in flight
        size_t listFor = SIZE_MAX;
        std::vector<size_t> listQueue; // lists waiting their turn

        void notify() {
            if (listener) listener();
        }

        void release() {
            auto glm = GameLevelManager::sharedState();
            if (glm->m_levelManagerDelegate == this && pageKey.empty() && listKey.empty()) glm->m_levelManagerDelegate = nullptr;
        }

        void requestPage(int page) {
            auto search = results.request.make(page);
            pageKey = search->getKey();
            pendingTotal = pendingEnd = -1;
            results.state = State::Loading;
            auto glm = GameLevelManager::sharedState();
            glm->m_levelManagerDelegate = this;
            log::debug("Browse: requesting page {} ({})", page, pageKey);
            // GD answers from its cache straight away when it has the page.
            if (results.request.lists) glm->getLevelLists(search);
            else glm->getOnlineLevels(search);
        }

        void pageArrived(CCArray* items) {
            int count = items ? static_cast<int>(items->count()) : 0;
            int fresh = 0;
            if (results.request.lists) {
                std::unordered_set<int> seen;
                for (auto const& l : results.lists) seen.insert(l.id);
                for (auto list : CCArrayExt<GJLevelList*>(items)) {
                    if (!list || !seen.insert(list->m_listID).second) continue; // pages can overlap
                    results.lists.push_back(makeListPack(list));
                    fresh++;
                }
            } else {
                std::unordered_set<int> seen;
                for (auto const& e : results.levels) seen.insert(e.id);
                for (auto level : CCArrayExt<GJGameLevel*>(items)) {
                    if (!level || !seen.insert(level->m_levelID.value()).second) continue;
                    results.levels.push_back(levels::fromLevel(levels::withSavedCopy(level), false));
                    fresh++;
                }
            }
            results.pagesLoaded++;
            if (pendingTotal >= 0) results.total = pendingTotal;
            // Another page? GD says how far this one reached; failing that, a short page is the last.
            if (count == 0 || fresh == 0) results.more = false;
            else if (pendingTotal >= 0 && pendingEnd >= 0) results.more = pendingEnd < pendingTotal;
            else results.more = count >= PER_PAGE;
            results.state = State::Loaded;
            log::info("Browse: page {} gave {} ({} so far, total {})", results.lastPage(), fresh, results.count(), results.total);
            pageKey.clear();
            release();
            notify();
        }

        void requestListLevels(size_t index) {
            auto& p = results.lists[index];
            std::string ids;
            for (int id : p.levelIDs) ids += (ids.empty() ? "" : ",") + std::to_string(id);
            p.state = packs::State::Loading;
            listFor = index;
            // By ID, the way a map pack's levels are fetched (GD's list page
            // has a type of its own, which its level manager turns down here).
            auto search = GJSearchObject::create(SearchType::MapPackOnClick, ids);
            listKey = search->getKey();
            auto glm = GameLevelManager::sharedState();
            glm->m_levelManagerDelegate = this;
            log::debug("Browse: requesting the levels of list {} ({})", p.name, listKey);
            glm->getOnlineLevels(search);
        }

        void nextList() {
            if (listFor != SIZE_MAX || listQueue.empty()) return;
            size_t index = listQueue.front();
            listQueue.erase(listQueue.begin());
            if (index < results.lists.size()) requestListLevels(index);
            else nextList();
        }

        void listDone() {
            listFor = SIZE_MAX;
            listKey.clear();
            release();
            notify();
            nextList();
        }

        void listLevelsArrived(CCArray* items) {
            if (listFor >= results.lists.size()) return listDone();
            auto& p = results.lists[listFor];
            std::vector<levels::Entry> got;
            for (auto level : CCArrayExt<GJGameLevel*>(items)) {
                if (!level) continue;
                auto e = levels::fromLevel(levels::withSavedCopy(level), false);
                e.pack = static_cast<int>(listFor);
                got.push_back(std::move(e));
            }
            // In the list's order.
            p.levels.clear();
            for (int id : p.levelIDs) {
                auto it = std::find_if(got.begin(), got.end(), [id](auto const& e) { return e.id == id; });
                if (it != got.end()) p.levels.push_back(*it);
            }
            for (auto& e : got) {
                if (std::none_of(p.levels.begin(), p.levels.end(), [&](auto const& l) { return l.id == e.id; })) p.levels.push_back(e);
            }
            p.state = p.levels.empty() ? packs::State::Failed : packs::State::Loaded;
            log::info("Browse: list {}: {} of {} levels loaded", p.name, p.levels.size(), p.levelIDs.size());
            listDone();
        }

        // LevelManagerDelegate. GD calls the typed pair; the plain pair is
        // covered in case a build calls those.
        void loadLevelsFinished(CCArray* levels, char const* key) override { loadLevelsFinished(levels, key, 0); }
        void loadLevelsFailed(char const* key) override { loadLevelsFailed(key, 0); }

        void loadLevelsFinished(CCArray* levels, char const* key, int) override {
            std::string k = key ? key : "";
            if (!pageKey.empty() && k == pageKey) pageArrived(levels);
            else if (!listKey.empty() && k == listKey) listLevelsArrived(levels);
            else log::debug("Browse: ignored levels for key {}", k);
        }

        void loadLevelsFailed(char const* key, int) override {
            std::string k = key ? key : "";
            if (!pageKey.empty() && k == pageKey) {
                log::warn("Browse: page {} failed", results.firstPage + results.pagesLoaded);
                results.state = State::Failed;
                pageKey.clear();
                release();
                notify();
            } else if (!listKey.empty() && k == listKey) {
                if (listFor < results.lists.size()) results.lists[listFor].state = packs::State::Failed;
                log::warn("Browse: list levels failed ({})", k);
                listDone();
            }
        }

        void setupPageInfo(gd::string info, char const* key) override {
            std::string k = key ? key : "";
            if (pageKey.empty() || k != pageKey) return;
            // "total:offset:count" (GameLevelManager::createPageInfo). GD sends
            // it with the page; whether before or after it, the numbers land.
            auto parts = utils::string::split(std::string(info), ":");
            if (parts.size() < 3) return;
            int total = utils::numFromString<int>(parts[0]).unwrapOr(-1);
            int start = utils::numFromString<int>(parts[1]).unwrapOr(-1);
            int count = utils::numFromString<int>(parts[2]).unwrapOr(-1);
            if (total < 0 || start < 0 || count < 0) return;
            pendingTotal = total;
            pendingEnd = start + count;
            // The numbers may come after the page: they still count.
            if (results.state == State::Loaded) {
                results.total = total;
                if (results.count() > 0) results.more = pendingEnd < total;
            }
        }
    };
}

int Filters::count() const {
    return std::popcount(difficulty) + std::popcount(length) + std::popcount(general) + (played > 0 ? 1 : 0);
}

GJSearchObject* Request::make(int page) const {
    auto glm = GameLevelManager::sharedState();
    auto diff = [this](int i) { return ((filters.difficulty >> i) & 1) != 0; };
    auto len = [this](int i) { return ((filters.length >> i) & 1) != 0; };
    auto general = [this](General bit) { return ((filters.general >> bit) & 1) != 0; };
    std::string difficulty = glm->getDifficultyStr(diff(0), diff(1), diff(2), diff(3), diff(4), diff(5), diff(6), diff(7));
    std::string length = glm->getLengthStr(len(0), len(1), len(2), len(3), len(4), len(5));
    // A query searches by name (or ID); without one, the sort tab picks GD's quick search.
    SearchType t = searchPage && !query.empty() ? SearchType::Search : type;
    return GJSearchObject::create(t, query, difficulty, length, page,
                                  general(RATED), filters.played == 1, general(FEATURED), 0, general(ORIGINAL),
                                  general(TWO_PLAYER), false, false, general(UNRATED), general(COINS),
                                  general(EPIC), general(LEGENDARY), general(MYTHIC), filters.played == 2,
                                  DEMON_FILTER_VALUES[std::clamp(filters.demon, 0, 5)], 0, lists ? 1 : 0);
}

Request Request::fromSearch(GJSearchObject* search) {
    Request r;
    r.query = utils::string::trim(std::string(search->m_searchQuery));
    r.lists = search->m_searchMode == 1;
    r.searchPage = quickType(search->m_searchType);
    r.type = search->m_searchType == SearchType::Search ? SearchType::Downloaded : search->m_searchType;
    auto& f = r.filters;
    // "1,2,-2": the server's difficulty numbers (-1 N/A, 1-5, -2 demon, -3 auto).
    for (auto const& part : utils::string::split(std::string(search->m_difficulty), ",")) {
        int v = utils::numFromString<int>(part).unwrapOr(99);
        int bit = v == -1 ? 0 : v >= 1 && v <= 5 ? v : v == -2 ? DIFF_DEMON : v == -3 ? 7 : -1;
        if (bit >= 0) f.difficulty |= 1u << bit;
    }
    for (auto const& part : utils::string::split(std::string(search->m_length), ",")) {
        int v = utils::numFromString<int>(part).unwrapOr(-1);
        if (v >= 0 && v <= 5) f.length |= 1u << v;
    }
    f.demon = demonOptionFor(static_cast<int>(search->m_demonFilter));
    if (f.demon > 0) f.difficulty = 1u << DIFF_DEMON;
    auto flag = [&f](bool on, General bit) { if (on) f.general |= 1u << bit; };
    flag(search->m_starFilter, RATED);
    flag(search->m_noStarFilter, UNRATED);
    flag(search->m_featuredFilter, FEATURED);
    flag(search->m_epicFilter, EPIC);
    flag(search->m_legendaryFilter, LEGENDARY);
    flag(search->m_mythicFilter, MYTHIC);
    flag(search->m_originalFilter, ORIGINAL);
    flag(search->m_coinsFilter, COINS);
    flag(search->m_twoPlayerFilter, TWO_PLAYER);
    f.played = search->m_uncompletedFilter ? 1 : search->m_completedFilter ? 2 : 0;
    return r;
}

bool Request::wants(GJSearchObject* search) {
    if (!search || search->m_searchIsOverlay) return false;
    switch (search->m_searchType) {
        case SearchType::Search:
        case SearchType::Downloaded:
        case SearchType::MostLiked:
        case SearchType::Trending:
        case SearchType::Recent:
        case SearchType::Featured:
        case SearchType::Magic:
        case SearchType::Awarded:
        case SearchType::Followed:
        case SearchType::Friends:
        case SearchType::HallOfFame:
        case SearchType::Sent:
        case SearchType::StarAward:
            return true;
        default:
            return false;
    }
}

Results const& results() {
    return Store::get().results;
}

std::vector<packs::Pack>& lists() {
    return Store::get().results.lists;
}

void open(Request const& request, int page, bool force) {
    auto& s = Store::get();
    auto& r = s.results;
    bool same = r.request == request && r.firstPage == page && (r.state == State::Loaded || r.state == State::Loading);
    if (same && !force) return;
    // Whatever was on its way is dropped (its key no longer matches).
    s.pageKey.clear();
    s.listKey.clear();
    s.listFor = SIZE_MAX;
    s.listQueue.clear();
    int generation = r.generation + 1;
    r = {};
    r.request = request;
    r.firstPage = std::max(0, page);
    r.generation = generation;
    s.requestPage(r.firstPage);
}

void loadMore() {
    auto& s = Store::get();
    auto& r = s.results;
    if (r.state == State::Loading || r.state == State::Idle || !r.more) return;
    s.requestPage(r.firstPage + r.pagesLoaded);
}

void refresh() {
    auto& s = Store::get();
    auto& r = s.results;
    if (r.state == State::Idle || r.state == State::Loading) return;
    // GD keeps pages for a while; a refresh wants fresh ones.
    auto glm = GameLevelManager::sharedState();
    for (int page = r.firstPage; page < r.firstPage + std::max(1, r.pagesLoaded); page++) {
        glm->resetTimerForKey(r.request.make(page)->getKey());
    }
    open(r.request, r.firstPage, true);
}

void loadListLevels(size_t index) {
    auto& s = Store::get();
    auto& lists = s.results.lists;
    if (index >= lists.size()) return;
    auto& p = lists[index];
    if (p.state == packs::State::Loading || p.state == packs::State::Loaded) return;
    if (std::find(s.listQueue.begin(), s.listQueue.end(), index) != s.listQueue.end()) return;
    if (p.levelIDs.empty()) {
        p.state = packs::State::Failed;
        return;
    }
    // The list the player is looking at goes first.
    p.state = packs::State::Loading;
    s.listQueue.insert(s.listQueue.begin(), index);
    s.nextList();
}

bool loadingListLevels() {
    auto& s = Store::get();
    return s.listFor != SIZE_MAX || !s.listQueue.empty();
}

bool loading() {
    return Store::get().results.state == State::Loading;
}

void resume() {
    auto& s = Store::get();
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelManagerDelegate == &s) return;
    if (s.results.state == State::Loading && !s.pageKey.empty()) {
        log::debug("Browse: asking for the cut-off page again");
        s.requestPage(s.results.firstPage + s.results.pagesLoaded);
    }
    if (s.listFor != SIZE_MAX && s.listFor < s.results.lists.size()) {
        log::debug("Browse: asking for the cut-off list's levels again");
        s.requestListLevels(s.listFor);
    }
}

int pageOf(size_t index) {
    return Store::get().results.firstPage + static_cast<int>(index / PER_PAGE);
}

void refreshProgress() {
    auto& r = Store::get().results;
    for (auto& e : r.levels) {
        auto level = e.level ? levels::withSavedCopy(e.level) : nullptr;
        if (level) e = levels::fromLevel(level, false);
    }
    for (auto& p : r.lists) refreshListProgress(p);
}

void setListener(std::function<void()> listener) {
    Store::get().listener = std::move(listener);
}

} // namespace lazer::browse
