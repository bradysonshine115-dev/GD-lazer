#include "MapPacks.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer::packs {

namespace {
    // GD's map pack page asks for ten at a time.
    constexpr int PER_PAGE = 10;
    constexpr int MAX_PAGES = 40;

    std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // GJDifficultySprite frame for a pack's difficulty (GJDifficulty), the
    // way levels::Entry numbers them: -1 auto, 0 N/A, 1-5, 6 hard demon, 7-10
    // the other demons.
    int difficultyFrame(GJDifficulty d) {
        switch (d) {
            case GJDifficulty::Auto: return -1;
            case GJDifficulty::NA: return 0;
            default: return std::clamp(static_cast<int>(d), 0, 10);
        }
    }

    std::vector<int> parseIDs(std::string const& ids) {
        std::vector<int> out;
        for (auto part : utils::string::split(ids, ",")) {
            if (auto id = utils::numFromString<int>(utils::string::trim(part)); id && *id > 0) out.push_back(*id);
        }
        return out;
    }

    // Your saved copy of a level carries its data and your progress. GD also
    // keeps nameless stubs for levels you've played but not saved: those only
    // lend their progress to the fetched copy.
    GJGameLevel* withSavedCopy(GJGameLevel* level) {
        auto saved = GameLevelManager::sharedState()->getSavedLevel(level->m_levelID.value());
        if (!saved || saved == level) return level;
        if (!std::string(saved->m_levelName).empty()) return saved;
        if (saved->m_normalPercent.value() > level->m_normalPercent.value()) level->m_normalPercent = saved->m_normalPercent.value();
        if (saved->m_practicePercent > level->m_practicePercent) level->m_practicePercent = saved->m_practicePercent;
        if (saved->m_attempts.value() > level->m_attempts.value()) level->m_attempts = saved->m_attempts.value();
        if (saved->m_jumps.value() > level->m_jumps.value()) level->m_jumps = saved->m_jumps.value();
        return level;
    }

    Pack makePack(GJMapPack* pack) {
        Pack p;
        p.pack = pack;
        p.id = pack->m_packID;
        p.name = pack->m_packName;
        p.difficulty = difficultyFrame(pack->m_difficulty);
        p.stars = pack->m_stars;
        p.coins = pack->m_coins;
        p.textColor = pack->m_textColour;
        p.barColor = pack->m_barColour;
        p.levelIDs = parseIDs(pack->m_levelStrings);
        p.search = lower(p.name);
        refresh(p);
        return p;
    }

    // The session's store, and GD's delegate for its requests: the pack list
    // page by page, then any pack's levels (SearchType::MapPackOnClick, the
    // request GD's pack cell makes).
    class Store : public LevelManagerDelegate {
    public:
        static Store& get() {
            static Store store;
            return store;
        }

        State state = State::Unloaded;
        std::vector<Pack> packs;
        std::function<void()> listener;
        int page = 0;
        int total = -1;              // from the page info, once known
        std::string listKey;         // the request in flight
        size_t levelsFor = SIZE_MAX; // the pack whose levels are in flight
        std::string levelsKey;

        void notify() {
            if (listener) listener();
        }

        void requestPage() {
            auto glm = GameLevelManager::sharedState();
            auto search = GJSearchObject::create(SearchType::MapPack);
            search->m_page = page;
            listKey = search->getKey();
            glm->m_levelManagerDelegate = this;
            log::debug("Map packs: requesting page {} ({})", page, listKey);
            // GD answers from its cache straight away when it has the page.
            glm->getMapPacks(search);
        }

        void pageArrived(CCArray* items) {
            int before = static_cast<int>(packs.size());
            for (auto pack : CCArrayExt<GJMapPack*>(items)) {
                if (!pack) continue;
                bool dup = std::any_of(packs.begin(), packs.end(), [&](Pack const& p) { return p.id == pack->m_packID; });
                if (!dup) packs.push_back(makePack(pack));
            }
            int got = static_cast<int>(packs.size()) - before;
            log::info("Map packs: page {} gave {} packs ({} so far, total {})", page, got, packs.size(), total);
            bool more = got > 0 && (total < 0 || static_cast<int>(packs.size()) < total) && got >= PER_PAGE && page + 1 < MAX_PAGES;
            if (more) {
                page++;
                notify();
                requestPage();
            } else {
                state = State::Loaded;
                listKey.clear();
                auto glm = GameLevelManager::sharedState();
                if (glm->m_levelManagerDelegate == this && levelsKey.empty()) glm->m_levelManagerDelegate = nullptr;
                notify();
            }
        }

        void levelsArrived(CCArray* items) {
            if (levelsFor >= packs.size()) return;
            auto& p = packs[levelsFor];
            auto glm = GameLevelManager::sharedState();
            std::vector<levels::Entry> found;
            for (auto level : CCArrayExt<GJGameLevel*>(items)) {
                if (!level) continue;
                level = withSavedCopy(level);
                auto e = levels::fromLevel(level, false);
                e.pack = static_cast<int>(levelsFor);
                found.push_back(std::move(e));
            }
            // In the pack's order.
            p.levels.clear();
            for (int id : p.levelIDs) {
                auto it = std::find_if(found.begin(), found.end(), [id](auto const& e) { return e.id == id; });
                if (it != found.end()) p.levels.push_back(*it);
            }
            for (auto& e : found) {
                if (std::none_of(p.levels.begin(), p.levels.end(), [&](auto const& l) { return l.id == e.id; })) p.levels.push_back(e);
            }
            p.state = p.levels.empty() ? State::Failed : State::Loaded;
            log::info("Map pack {}: {} of {} levels loaded", p.name, p.levels.size(), p.levelIDs.size());
            levelsFor = SIZE_MAX;
            levelsKey.clear();
            if (glm->m_levelManagerDelegate == this && listKey.empty()) glm->m_levelManagerDelegate = nullptr;
            notify();
        }

        // LevelManagerDelegate. GD calls the typed pair; the plain pair is
        // covered in case a build calls those.
        void loadLevelsFinished(CCArray* levels, char const* key) override { loadLevelsFinished(levels, key, 0); }
        void loadLevelsFailed(char const* key) override { loadLevelsFailed(key, 0); }

        void loadLevelsFinished(CCArray* levels, char const* key, int) override {
            std::string k = key ? key : "";
            if (!listKey.empty() && (k == listKey || k.empty())) {
                pageArrived(levels);
            } else if (!levelsKey.empty() && k == levelsKey) {
                levelsArrived(levels);
            } else {
                log::debug("Map packs: ignored levels for key {}", k);
            }
        }

        void loadLevelsFailed(char const* key, int) override {
            std::string k = key ? key : "";
            auto glm = GameLevelManager::sharedState();
            if (!listKey.empty() && (k == listKey || k.empty())) {
                log::warn("Map packs: page {} failed", page);
                // Something already loaded is worth showing.
                state = packs.empty() ? State::Failed : State::Loaded;
                listKey.clear();
            } else if (!levelsKey.empty() && k == levelsKey) {
                if (levelsFor < packs.size()) packs[levelsFor].state = State::Failed;
                log::warn("Map pack levels failed ({})", k);
                levelsFor = SIZE_MAX;
                levelsKey.clear();
            } else {
                return;
            }
            if (glm->m_levelManagerDelegate == this && listKey.empty() && levelsKey.empty()) glm->m_levelManagerDelegate = nullptr;
            notify();
        }

        void setupPageInfo(gd::string info, char const* key) override {
            std::string k = key ? key : "";
            if (listKey.empty() || (k != listKey && !k.empty())) return;
            // "total:offset:count"
            auto parts = utils::string::split(std::string(info), ":");
            if (!parts.empty()) {
                if (auto n = utils::numFromString<int>(parts[0])) total = *n;
            }
        }
    };
}

State state() {
    return Store::get().state;
}

std::vector<Pack>& all() {
    return Store::get().packs;
}

void load() {
    auto& s = Store::get();
    if (s.state == State::Loading || s.state == State::Loaded) return;
    s.state = State::Loading;
    s.page = 0;
    s.total = -1;
    s.packs.clear();
    s.requestPage();
}

void loadLevels(size_t index) {
    auto& s = Store::get();
    if (index >= s.packs.size()) return;
    auto& p = s.packs[index];
    if (p.state == State::Loading || p.state == State::Loaded) return;
    // One at a time: the delegate has one key in flight for levels.
    if (s.levelsFor != SIZE_MAX) return;
    if (p.levelIDs.empty()) {
        p.state = State::Failed;
        return;
    }
    p.state = State::Loading;
    s.levelsFor = index;
    std::string ids;
    for (int id : p.levelIDs) ids += (ids.empty() ? "" : ",") + std::to_string(id);
    auto search = GJSearchObject::create(SearchType::MapPackOnClick, ids);
    s.levelsKey = search->getKey();
    auto glm = GameLevelManager::sharedState();
    glm->m_levelManagerDelegate = &s;
    log::debug("Map pack {}: requesting levels ({})", p.name, s.levelsKey);
    glm->getOnlineLevels(search);
}

void refresh(Pack& p) {
    if (!p.pack) return;
    p.completed = p.pack->completedMaps();
    p.claimed = GameStatsManager::sharedState()->hasCompletedMapPack(p.id);
    // A level's saved copy appears once it's been played: pick it up.
    for (auto& e : p.levels) {
        auto level = e.level ? withSavedCopy(e.level) : nullptr;
        if (level && level != e.level.data()) {
            auto fresh = levels::fromLevel(level, false);
            fresh.pack = e.pack;
            e = fresh;
        } else if (e.level) {
            e.normalPercent = e.level->m_normalPercent.value();
            e.practicePercent = e.level->m_practicePercent;
            e.resolved = false;
        }
    }
}

bool canClaim(Pack const& p) {
    if (!p.pack || p.claimed) return false;
    return !p.levelIDs.empty() && p.completed >= static_cast<int>(p.levelIDs.size());
}

void claim(Pack& p) {
    if (!canClaim(p)) return;
    // What GD's pack cell does on its reward button: the stats manager
    // records the pack and hands out its stars and coins.
    GameStatsManager::sharedState()->completedMapPack(p.pack);
    p.claimed = GameStatsManager::sharedState()->hasCompletedMapPack(p.id);
    log::info("Map pack {} claimed ({} stars, {} coins)", p.name, p.stars, p.coins);
}

void setListener(std::function<void()> listener) {
    Store::get().listener = std::move(listener);
}

} // namespace lazer::packs
