#include "CommentsOverlay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/InfoLayer.hpp>

using namespace geode::prelude;

// GD's comments page for a level (InfoLayer: the level page's info button,
// and anything else that opens one) shows as our page instead. GD's layer is
// already built by then: the page keeps it, hidden, and falls back to it if
// it can't open.
class $modify(LazerInfoLayer, InfoLayer) {
    void show() {
        // Ours asked for GD's own ("more info"), or one we don't cover.
        if (this->getUserObject("vanilla"_spr) || !lazer::CommentsOverlay::wants(this)) return InfoLayer::show();
        if (!lazer::CommentsOverlay::present(m_level, this)) InfoLayer::show();
    }
};
