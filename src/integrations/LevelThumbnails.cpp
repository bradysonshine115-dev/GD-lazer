#include "LevelThumbnails.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace lazer::thumbnails {

namespace {
    // "medium" is plenty for a blurred, dimmed backdrop and ~100 KB per level.
    constexpr auto URL = "https://levelthumbs.prevter.me/thumbnail/{}/medium";
    // Downloads and decodes running at once; the rest wait their turn.
    constexpr size_t MAX_ACTIVE = 4;
    // Decoded thumbnails kept around (~a screenful or two of song select).
    // Older ones are decoded again from the disk cache when needed.
    constexpr size_t MAX_CACHED = 48;

    using Callback = std::function<void(CCTexture2D*)>;
    using Wanted = std::function<bool()>;

    struct Waiter {
        Callback callback;
        Wanted wanted; // empty = always wanted
    };

    struct State {
        std::unordered_map<int, Ref<CCTexture2D>> textures;
        std::unordered_map<int, uint64_t> lastUse; // for evicting the least recently used
        uint64_t useClock = 0;
        std::unordered_set<int> missing; // 404s: this session won't ask again
        std::unordered_map<int, std::vector<Waiter>> waiting;
        std::unordered_map<int, Ref<LazySprite>> decoding;
        // Queued loads (key, how to start it), newest last; started newest first.
        std::vector<std::pair<int, std::function<void()>>> queue;
        size_t active = 0;
        // Bumped by a graphics reload: loads from before it don't count any more.
        int generation = 0;
    };

    State& state() {
        static State s;
        return s;
    }

    std::filesystem::path cachePath(int id) {
        return Mod::get()->getSaveDir() / "thumbnails" / fmt::format("{}.webp", id);
    }

    void touch(int id) {
        auto& s = state();
        s.lastUse[id] = ++s.useClock;
    }

    void remember(int id, CCTexture2D* texture) {
        auto& s = state();
        s.textures[id] = texture;
        touch(id);
        while (s.textures.size() > MAX_CACHED) {
            auto oldest = s.textures.end();
            uint64_t oldestUse = UINT64_MAX;
            for (auto it = s.textures.begin(); it != s.textures.end(); ++it) {
                uint64_t use = s.lastUse[it->first];
                if (use < oldestUse) {
                    oldestUse = use;
                    oldest = it;
                }
            }
            if (oldest == s.textures.end()) break;
            // Nodes showing it keep their own reference.
            s.lastUse.erase(oldest->first);
            s.textures.erase(oldest);
        }
    }

    void finish(int id, CCTexture2D* texture, bool missing) {
        auto& s = state();
        if (texture) remember(id, texture);
        if (missing) s.missing.insert(id);
        auto node = s.waiting.extract(id);
        if (node.empty()) return;
        for (auto& waiter : node.mapped()) waiter.callback(texture);
    }

    void pump();

    // A load ended (or failed): the next one in the queue can start.
    void release(int generation) {
        auto& s = state();
        if (generation != s.generation) return;
        if (s.active > 0) s.active--;
        pump();
    }

    // Anyone waiting on this level who still wants it?
    bool stillWanted(int id) {
        auto& s = state();
        auto it = s.waiting.find(id);
        if (it == s.waiting.end()) return false;
        for (auto& waiter : it->second) {
            if (!waiter.wanted || waiter.wanted()) return true;
        }
        return false;
    }

    void pump() {
        auto& s = state();
        while (s.active < MAX_ACTIVE && !s.queue.empty()) {
            // Newest first: what's on screen now matters more than what was.
            auto [id, start] = std::move(s.queue.back());
            s.queue.pop_back();
            if (!stillWanted(id)) {
                s.waiting.erase(id);
                continue;
            }
            s.active++;
            start();
        }
    }

    void enqueue(int id, std::function<void()> start) {
        state().queue.emplace_back(id, std::move(start));
        pump();
    }

    // `id` keys the cache: level IDs for online levels, negated for RobTop's
    // (their IDs overlap with online ones). Bundled images are never deleted.
    // Runs as a queued load: releases its slot when done.
    void decode(int id, std::filesystem::path const& path, int generation, bool bundled = false) {
        auto sprite = LazySprite::create({1, 1}, false);
        state().decoding[id] = sprite;
        sprite->setLoadCallback([id, path, bundled, generation](Result<> res) {
            auto& s = state();
            auto it = s.decoding.find(id);
            if (it == s.decoding.end()) return; // dropped by a graphics reload
            Ref<LazySprite> sprite = it->second;
            s.decoding.erase(it);
            CCTexture2D* texture = res && sprite ? sprite->getTexture() : nullptr;
            if (!res) {
                log::warn("Couldn't decode thumbnail for level {}: {}", id, res.unwrapErr());
                std::error_code ec;
                if (!bundled) std::filesystem::remove(path, ec); // corrupt or unsupported: fetch again next time
            }
            finish(id, texture, bundled && !texture);
            release(generation);
            // Don't destroy the sprite from inside its own callback.
            Loader::get()->queueInMainThread([sprite] {});
        });
        sprite->loadFromFile(path);
    }

    // Queues a callback for `key`; returns false if a load for it is already
    // on its way. Asked for again while still queued, it moves to the front.
    bool wait(int key, Callback callback, Wanted wanted) {
        auto& s = state();
        auto& waiting = s.waiting[key];
        waiting.push_back({std::move(callback), std::move(wanted)});
        if (waiting.size() == 1) return true;
        auto queued = std::find_if(s.queue.begin(), s.queue.end(), [key](auto const& job) { return job.first == key; });
        if (queued != s.queue.end() && queued + 1 != s.queue.end()) {
            auto job = std::move(*queued);
            s.queue.erase(queued);
            s.queue.push_back(std::move(job));
        }
        return false;
    }
}

void fetch(int levelID, Callback callback, Wanted wanted) {
    auto& s = state();
    if (levelID <= 0 || s.missing.contains(levelID)) return callback(nullptr);
    if (auto it = s.textures.find(levelID); it != s.textures.end()) {
        touch(levelID);
        return callback(it->second);
    }
    if (!wait(levelID, std::move(callback), std::move(wanted))) return; // already on its way

    enqueue(levelID, [levelID] {
        int generation = state().generation;
        auto path = cachePath(levelID);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            decode(levelID, path, generation);
            return;
        }

        // Download on a worker thread. (Geode's coroutine-based web API crashes
        // this MSVC version's code generator, so use the blocking call instead.)
        std::thread([levelID, path, generation] {
            auto res = web::WebRequest().timeout(std::chrono::seconds(15)).getSync(fmt::format(URL, levelID));
            bool saved = false;
            if (res.ok()) {
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                auto written = file::writeBinary(path, res.data());
                if (!written) log::warn("Couldn't cache thumbnail for level {}: {}", levelID, written.unwrapErr());
                saved = bool(written);
            }
            // 404 = nobody made a thumbnail for this level yet. Anything else
            // (offline, server hiccup) may work next time.
            bool missing = res.code() == 404;
            Loader::get()->queueInMainThread([levelID, path, saved, missing, generation] {
                // Downloaded before a graphics reload: it's on disk for next time.
                if (generation != state().generation) return;
                if (saved) {
                    // Keeps the slot for decoding.
                    decode(levelID, path, generation);
                    return;
                }
                finish(levelID, nullptr, missing);
                release(generation);
            });
        }).detach();
    });
}

void fetchOfficial(int levelID, Callback callback, Wanted wanted) {
    auto& s = state();
    int key = -levelID;
    if (levelID <= 0 || s.missing.contains(key)) return callback(nullptr);
    if (auto it = s.textures.find(key); it != s.textures.end()) {
        touch(key);
        return callback(it->second);
    }
    if (!wait(key, std::move(callback), std::move(wanted))) return;

    auto path = Mod::get()->getResourcesDir() / fmt::format("level-{}.webp", levelID);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        finish(key, nullptr, true);
        return;
    }
    enqueue(key, [key, path] { decode(key, path, state().generation, true); });
}

void fetchFirst(std::vector<int> levelIDs, std::function<void(CCTexture2D*, int)> callback) {
    if (levelIDs.empty()) return callback(nullptr, 0);
    int id = levelIDs.front();
    fetch(id, [id, rest = std::vector<int>(levelIDs.begin() + 1, levelIDs.end()), callback](CCTexture2D* texture) mutable {
        if (texture) return callback(texture, id);
        fetchFirst(std::move(rest), std::move(callback));
    });
}

} // namespace lazer::thumbnails

// Switching between fullscreen and windowed makes a new GL context: cached
// textures from the old one are dead (drawn blank). Drop them while that
// context is still there; they're decoded again from the disk cache. Waiting
// callbacks belong to screens the reload tears down.
class $modify(LazerThumbnailsReload, GameManager) {
    void reloadAll(bool switchingModes, bool toFullscreen, bool borderless, bool fix, bool unused) {
        auto& s = lazer::thumbnails::state();
        s.textures.clear();
        s.lastUse.clear();
        s.waiting.clear();
        s.decoding.clear();
        s.queue.clear();
        s.active = 0;
        s.generation++;
        GameManager::reloadAll(switchingModes, toFullscreen, borderless, fix, unused);
    }
};
