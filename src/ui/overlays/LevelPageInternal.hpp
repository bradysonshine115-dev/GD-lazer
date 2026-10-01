#pragma once

#include "../core/Theme.hpp"

#include <Geode/Geode.hpp>

// Shared between the level page's files: its sizes (osu! pixels, scaled by
// m_k) and the small checks its input needs.
namespace lazer::levelpage {

// osu!'s Blue overlay scheme, like the listing the page opens from.
constexpr theme::Scheme SCHEME {200};

constexpr float HORIZONTAL_PADDING = 50.f;  // WaveOverlayContainer
constexpr float HERO_PADDING = 24.f;        // above and below the top band's content
constexpr float HERO_GAP = 24.f;            // between the picture and the facts
constexpr float THUMB_HEIGHT = 150.f;       // the picture (16:9)
constexpr float THUMB_RADIUS = 10.f;
constexpr float SECTION_GAP = 28.f;         // between sections
constexpr float COLUMN_GAP = 36.f;          // between the two columns
constexpr float TAB_HEIGHT = 26.f;

inline bool nodeContains(cocos2d::CCNode* node, cocos2d::CCPoint world) {
    auto local = node->convertToNodeSpace(world);
    auto size = node->getContentSize();
    return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
}

inline bool nodeShown(cocos2d::CCNode* node) {
    for (auto n = node; n; n = n->getParent()) {
        if (!n->isVisible()) return false;
    }
    return true;
}

} // namespace lazer::levelpage
