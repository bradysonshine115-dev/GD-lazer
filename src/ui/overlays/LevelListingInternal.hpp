#pragma once

// Private to the LevelListingOverlay*.cpp files: the sizes, colours and helpers they share.

#include "LevelListingOverlay.hpp"

#include "../core/Text.hpp"

#include <Geode/Geode.hpp>
#include <cctype>
#include <string>

using namespace geode::prelude;

namespace lazer {

namespace levellisting {
    // osu!'s Orange (hue 45): your levels and lists, in the create button's colour.
    inline constexpr theme::Scheme CREATE_SCHEME {45};

    // osu! sizes (768 px tall screen), scaled by m_k.
    inline constexpr float HORIZONTAL_PADDING = 50.f;  // WaveOverlayContainer
    inline constexpr float CONTROL_PADDING = 16.f;     // BeatmapListingSearchControl, top and bottom
    inline constexpr float CONTROL_SPACING = 14.f;     // between the search box and the filter rows
    inline constexpr float TEXTBOX_HEIGHT = 40.f;      // SearchTextBox (osu!'s 35, with more room to tap)
    inline constexpr float TEXTBOX_RADIUS = 10.f;
    inline constexpr float TEXTBOX_SIDE = 14.f;        // OsuTextBox.LeftRightPadding
    inline constexpr float SEARCH_ICON = 15.f;         // BasicSearchTextBox's magnifier
    inline constexpr float ROW_LABEL_WIDTH = 90.f;     // BeatmapSearchFilterRow's label column
    inline constexpr float CHIP_HEIGHT = 26.f;         // a filter option
    inline constexpr float CHIP_PAD = 12.f;            // inside a chip, each side of its text
    inline constexpr float CHIP_SPACING = 8.f;         // between chips
    inline constexpr float LINE_SPACING = 6.f;         // between a row's lines of chips
    inline constexpr float ROW_SPACING = 8.f;          // between rows
    inline constexpr float STRIP_HEIGHT = 44.f;        // the strip under the search box
    inline constexpr float STRIP_MARGIN = 20.f;
    inline constexpr float TAB_HEIGHT = 26.f;          // the strip's tabs and buttons
    inline constexpr float TAB_PAD = 12.f;
    inline constexpr float PROGRESS_HEIGHT = 3.f;      // the loading bar under the strip
    inline constexpr float CARD_MIN_WIDTH = 400.f;     // as many columns of at least this as fit
    inline constexpr float CARD_HEIGHT = 96.f;
    inline constexpr float CARD_RADIUS = 10.f;
    inline constexpr float CARD_SPACING = 12.f;
    inline constexpr float CARD_THUMB_RATIO = 1.5f;    // the thumbnail's width over the card's height
    inline constexpr float CARD_TEXT_GAP = 14.f;       // between the thumbnail and the text, and at the right
    inline constexpr float CARDS_PADDING = 20.f;       // panelTarget's horizontal padding
    inline constexpr float CARDS_TOP = 16.f;
    inline constexpr float CARDS_BOTTOM = 24.f;
    inline constexpr float NOT_FOUND_HEIGHT = 160.f;   // NotFoundDrawable
    inline constexpr float CARD_TRANSITION = 360.f;    // BeatmapCard.TRANSITION_DURATION
    inline constexpr float CHIP_TRANSITION = 150.f;
    inline constexpr float CARD_FADE = 320.f;          // a card comes in over this
    inline constexpr float CARD_STAGGER = 35.f;        // ms between one card and the next
    inline constexpr float CARD_STAGGER_MAX = 350.f;
    inline constexpr float CARD_RISE = 14.f;           // a card rises this far as it comes in
    inline constexpr float QUERY_DEBOUNCE = 350.f;     // typing waits this long before searching
    inline constexpr float FILTER_DEBOUNCE = 100.f;
    // GD reports its own request failures; this catches one that never reports.
    inline constexpr float LOAD_TIMEOUT_MS = 30000.f;
    inline constexpr float SHIMMER_SPEED = 2.6f;       // placeholders' gradient, radians per second
    inline constexpr float PROGRESS_PERIOD = 1.4f;     // the loading bar's sweep, seconds
    inline constexpr float STALE_ALPHA = 0.35f;        // the old cards while a new search loads
    inline constexpr int LEVELS_PER_PAGE = 10;         // levels per GD page
    inline constexpr int QUERY_LIMIT = 20;             // GD's search box
    inline constexpr size_t CARDS_PER_FRAME = 12;      // your levels: cards built per frame

    inline constexpr ccColor4B CLEAR {0, 0, 0, 0};
    inline constexpr ccColor4B WHITE {255, 255, 255, 255};
    inline constexpr ccColor4B BLACK {0, 0, 0, 255};
    // A card's body once its thumbnail is behind the text: the picture, dimmed
    // this much (a little less while hovered).
    inline constexpr ccColor4B ART_DIM {58, 60, 68, 255};
    inline constexpr ccColor4B ART_DIM_HOVER {96, 100, 110, 255};

    inline std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // A folder of your levels and lists ("folder 3" if you haven't named it).
    inline std::string createdFolderName(int folder) {
        std::string name = GameLevelManager::sharedState()->getFolderName(folder, true);
        return name.empty() ? fmt::format("folder {}", folder) : name;
    }
}

} // namespace lazer
