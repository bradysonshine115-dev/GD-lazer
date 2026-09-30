#pragma once

// Private to the SongSelect*.cpp files: the sizes, colours and helpers they share.

#include "SongSelect.hpp"

#include "../../integrations/LevelThumbnails.hpp"
#include "../core/Text.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace lazer {

namespace songselect {
    // osu! sizes (768 px tall screen), scaled by m_k.
    inline constexpr float PANEL_HEIGHT = 72;   // PanelBeatmapStandalone.HEIGHT
    inline constexpr float SCROLLBAR_WIDTH = 10;  // OsuScrollContainer.SCROLL_BAR_WIDTH
    inline constexpr float SCROLLBAR_MARGIN = 3;
    // Touch area around the bar: a 10px bar is too thin for a finger.
    inline constexpr float SCROLLBAR_HIT_WIDTH = 36;
    inline constexpr float SCROLLBAR_HELD_WIDTH = 1.6f; // x the width while held
    // Sideways, the held bar follows the finger almost freely (a little
    // rubbery resistance), at most this far into the carousel.
    inline constexpr float SCROLLBAR_PULL_FOLLOW = 0.9f;
    inline constexpr float SCROLLBAR_PULL_MAX = 300;
    // The label keeps clear of the finger holding the bar.
    inline constexpr float SCROLLBAR_LABEL_GAP = 64;
    inline constexpr float PANEL_SPACING = 3;   // BeatmapCarousel.SPACING
    inline constexpr float ACTIVE_X = 25;       // Panel.active_x_offset
    inline constexpr float CORNER = 10;         // Panel.CORNER_RADIUS
    inline constexpr float FOOTER_HEIGHT = 50;  // ScreenFooter.HEIGHT
    inline constexpr float FILTER_HEIGHT = 96;
    inline constexpr float STRIP_WIDTH = 64;
    // Map packs: a pack's levels are shorter rows under it (PanelBeatmap is
    // smaller than PanelBeatmapSet), and rows rest further right the less
    // they matter (Panel.updateXOffset: collapsed sets sit furthest back).
    inline constexpr float PACK_HEADER_HEIGHT = 80;
    inline constexpr float PACK_LEVEL_HEIGHT = 56;
    inline constexpr float PACK_REST_X = 40;
    inline constexpr float PACK_LEVEL_REST_X = 30;
    inline constexpr float PACK_STRIP_WIDTH = 44;
    inline constexpr float PACK_OPEN_GAP = 18;    // room above an open pack and below its last level
    inline constexpr ccColor4B PACK_LEVEL_BG {26, 25, 33, 235};
    inline constexpr float SHEAR = 0.2f;        // OsuGame.SHEAR
    inline constexpr float PREVIEW_DELAY = 150; // SongSelect.SELECTION_DEBOUNCE
    inline constexpr float THUMB_DELAY = 150;
    // Panels built per frame, nearest the middle of the view first: a fast
    // scroll through thousands of levels builds a few at a time instead of a
    // screenful in one frame. New ones fade in (Panel.PrepareForUse).
    inline constexpr int PANEL_LOADS_PER_FRAME = 3;
    inline constexpr float PANEL_FADE = 400;    // Panel.DURATION
    inline constexpr double SCROLL_DECAY = 0.989;
    inline constexpr float BACKGROUND_DIM = 0.55f;
    inline constexpr float LOADER_DIM = 0.3f;       // the loader shows the background more (osu! un-dims it)
    inline constexpr float PUSH_DELAY = 1800;       // PlayerLoader.PlayerPushDelay
    inline constexpr float CONTENT_OUT = 300;       // PlayerLoader.CONTENT_OUT_DURATION
    // When the level is built behind the loader. osu! starts loading the Player
    // once the card has scaled in (650 ms); GD builds it in one go on this
    // thread, so wait for the details too: the frame that stalls is a still one.
    inline constexpr float LEVEL_LOAD_AT = 1000;

    inline constexpr ccColor4B PANEL_BG {36, 34, 44, 235};
    inline constexpr ccColor4B PANEL_HOVER {58, 54, 72, 245};
    inline constexpr ccColor4B PINK {238, 51, 153, 255};
    inline constexpr ccColor4B PURPLE {102, 68, 204, 255};
    inline constexpr ccColor4B TAB {60, 56, 76, 255};

    // Kept between visits (and across a round trip into gameplay), per kind.
    struct Remembered {
        int group = 0;
        int folder = 0;
        levels::Sort sort = levels::Sort::Default;
        std::string query;
        int selectedId = -1;
        bool selectedOfficial = false;
        bool selectedHeader = false; // a pack's header row (pack mode)
        int expandedPack = -1;       // pack ID open in pack mode
    };
    extern levels::Kind g_lastKind;
    Remembered& remembered();

    // Where a song was left, for the preview to carry on from (osu! keeps the
    // track going across a play): the preview's own position when play was
    // pressed, then the level's when it's quit (paused, or completed and left).
    // The next preview of that song uses it; any other preview drops it.
    struct Resume { std::string path; unsigned ms = 0; };
    extern Resume g_resume;

    // Platformer times, GD style: 1:23.456 (or 23.456 under a minute).
    inline std::string formatTime(int ms) {
        int minutes = ms / 60000;
        int seconds = ms / 1000 % 60;
        if (minutes > 0) return fmt::format("{}:{:02}.{:03}", minutes, seconds, ms % 1000);
        return fmt::format("{}.{:03}", seconds, ms % 1000);
    }

    // Stars for classic levels, moons for platformers.
    inline char const* rewardIcon(levels::Entry const& e) { return e.platformer ? icon::MOON : icon::STAR; }

    inline float skewDegrees() { return CC_RADIANS_TO_DEGREES(std::atan(SHEAR)); }

    inline std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // A button's label changed: keep it inside the button's (fixed-size) background.
    inline void fitLabel(CCLabelBMFont* label, CCNode* button) {
        auto base = static_cast<CCFloat*>(label->getUserObject("base-scale"_spr));
        if (!base) {
            base = CCFloat::create(label->getScale());
            label->setUserObject("base-scale"_spr, base);
        }
        label->setScale(base->getValue());
        float room = button->getContentSize().width - button->getContentSize().height * 0.45f - label->getPositionX();
        float w = label->getScaledContentSize().width;
        if (w > room && w > 0) label->setScale(base->getValue() * room / w);
    }

    // Shrinks a label to fit `maxWidth`, cutting it with an ellipsis if it would get too small.
    inline void fit(CCLabelBMFont* label, float maxWidth) {
        float base = label->getScale();
        float w = label->getScaledContentSize().width;
        if (w <= maxWidth) return;
        if (w * 0.85f <= maxWidth) {
            label->setScale(base * maxWidth / w);
            return;
        }
        std::string text = label->getString();
        while (text.size() > 1 && label->getScaledContentSize().width > maxWidth) {
            text.pop_back();
            label->setString((text + "...").c_str());
        }
    }

    inline int difficultyRank(int frame) {
        switch (frame) {
            case 0: return 0;   // N/A
            case -1: return 1;  // auto
            case 7: return 7;   // easy demon
            case 8: return 8;
            case 6: return 9;   // hard demon
            case 9: return 10;
            case 10: return 11;
            default: return frame + 1; // 1-5 -> 2-6
        }
    }

    // Cocos' colour layers repaint their vertex colours when their own opacity
    // is set, but not when it reaches them through a parent that cascades it:
    // as a child of a fading panel a CCLayerGradient would stay put at full
    // alpha. This one follows.
    class CascadingGradient : public CCLayerGradient {
    public:
        static CascadingGradient* create(ccColor4B const& start, ccColor4B const& end, CCPoint const& v) {
            auto ret = new CascadingGradient();
            if (ret->initWithColor(start, end, v)) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }
        void updateDisplayedOpacity(GLubyte parentOpacity) override {
            CCLayerGradient::updateDisplayedOpacity(parentOpacity);
            this->updateColor();
        }
    };

    // A small horizontal run of icon + text pairs.
    inline CCNode* infoRow(std::vector<std::pair<char const*, std::string>> const& items, float size, ccColor3B color) {
        auto row = CCNodeRGBA::create();
        row->setCascadeOpacityEnabled(true);
        float x = 0;
        for (auto const& [glyph, text] : items) {
            if (glyph) {
                auto icon = makeIcon(glyph, size * 0.85f);
                icon->setColor(color);
                icon->setAnchorPoint({0, 0.5f});
                icon->setPosition({x, 0});
                row->addChild(icon);
                x += icon->getScaledContentSize().width + size * 0.3f;
            }
            auto label = makeText(text, Weight::SemiBold, size);
            label->setColor(color);
            label->setAnchorPoint({0, 0.5f});
            label->setPosition({x, 0});
            row->addChild(label);
            x += label->getScaledContentSize().width + size * 0.9f;
        }
        row->setContentSize({x, size});
        return row;
    }

    inline GJFeatureState featureState(GJGameLevel* level) {
        switch (level->m_isEpic) {
            case 1: return GJFeatureState::Epic;
            case 2: return GJFeatureState::Legendary;
            case 3: return GJFeatureState::Mythic;
            default: return level->m_featured > 0 ? GJFeatureState::Featured : GJFeatureState::None;
        }
    }

    inline CCNode* difficultyFace(levels::Entry const& e, float size) {
        auto face = GJDifficultySprite::create(e.difficulty, GJDifficultyName::Short);
        if (!e.official && e.level) face->updateFeatureState(featureState(e.level));
        face->setCascadeOpacityEnabled(true); // the feature glow fades with it
        auto s = face->getContentSize();
        face->setScale(size / std::max(1.f, std::max(s.width, s.height)));
        return face;
    }

    // RobTop's levels have bundled screenshots (their IDs mean other levels
    // online); saved levels come from the Level Thumbnails server.
    inline void levelThumbnail(levels::Entry const& e, std::function<void(CCTexture2D*)> callback,
                        std::function<bool()> wanted = nullptr) {
        if (e.packHeader) {
            // A pack has no picture of its own: its first level's stands in.
            auto& packs = packs::all();
            if (e.pack < 0 || static_cast<size_t>(e.pack) >= packs.size() || packs[e.pack].levelIDs.empty()) return;
            thumbnails::fetch(packs[e.pack].levelIDs.front(), std::move(callback), std::move(wanted));
            return;
        }
        if (e.official) thumbnails::fetchOfficial(e.id, std::move(callback), std::move(wanted));
        else thumbnails::fetch(e.id, std::move(callback), std::move(wanted));
    }

    // What the page says when a search finds nothing, picked by the search
    // text so it holds still while you type (osu!'s NoResultsPlaceholder is
    // plainer; players asked for some fun here).
    inline constexpr std::array<char const*, 7> NO_RESULTS_LINES {{
        "i tried my best bro...",
        "nothing. make sure you typed it right",
        "no level called that. yet.",
        "checked twice. still nothing.",
        "maybe it's in another game",
        "the search came back empty-handed",
        "not a single one. sorry.",
    }};
    // And what the cursor says about it.
    inline constexpr std::array<char const*, 6> NO_RESULTS_CURSOR {{
        "make sure you typed right",
        "i tried my best bro",
        "nope, nothing",
        "that's not a level",
        "typo? no? ok...",
        "i looked everywhere",
    }};
    inline constexpr std::array<char const*, 3> MAP_PACKS_CURSOR {{
        "not the map packs...",
        "oh no. not the map packs",
        "map packs? brave.",
    }};
    inline constexpr std::array<char const*, 3> RANDOM_SPAM_CURSOR {{
        "just pick one already",
        "the random button is tired",
        "that's not how random works",
    }};
    inline size_t pickLine(std::string const& seed, size_t count) {
        return std::hash<std::string> {}(seed) % count;
    }

    // The pack an entry belongs to (a header, or one of its levels), or null.
    inline packs::Pack* packOf(levels::Entry const& e) {
        auto& packs = packs::all();
        if (e.pack < 0 || static_cast<size_t>(e.pack) >= packs.size()) return nullptr;
        return &packs[e.pack];
    }

    inline bool containsWorld(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }
}

// Shared across the song select files, used as if they were in lazer itself.
using namespace songselect;

} // namespace lazer
