#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <vector>

namespace lazer::thumbnails {

// Level screenshots from the Level Thumbnails community server
// (levelthumbs.prevter.me, the same one the Level Thumbnails mod uses).
// Downloads are cached in our save folder; images are WebP, decoded by the
// Image Plus mod's CCImage support.
//
// `callback` runs on the main thread with the texture, or with nullptr when the
// level has no thumbnail or it couldn't be loaded.
//
// Only a few thumbnails download or decode at once, newest request first, so a
// list scrolled past quickly doesn't queue up hundreds of them. `wanted` (optional)
// is asked when a queued request's turn comes: if nobody waiting on that level
// still wants it (its card scrolled away), it's dropped without calling back.
// Decoded thumbnails are kept for the most recently used levels only.
void fetch(int levelID, std::function<void(cocos2d::CCTexture2D*)> callback,
           std::function<bool()> wanted = nullptr);

// RobTop's levels (main levels 1-22, the Tower's 5001-5004): screenshots bundled
// with the mod (from the Geometry Dash Wiki). Same rules as fetch().
void fetchOfficial(int levelID, std::function<void(cocos2d::CCTexture2D*)> callback,
                   std::function<bool()> wanted = nullptr);

// Tries each level in order and returns the first thumbnail found (with its
// level ID), or nullptr / 0 when none of them has one.
void fetchFirst(std::vector<int> levelIDs, std::function<void(cocos2d::CCTexture2D*, int)> callback);

} // namespace lazer::thumbnails
