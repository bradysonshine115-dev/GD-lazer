#pragma once

// Private to the CommentsOverlay*.cpp files: the sizes, colours and helpers they share.

#include "CommentsOverlay.hpp"

#include "../core/Text.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <string>

using namespace geode::prelude;

namespace lazer {

namespace commentsoverlay {
    // osu!'s Blue overlay scheme: the beatmap overlay's, where its comments live.
    inline constexpr theme::Scheme SCHEME {200};

    // osu! sizes (768 px tall screen), scaled by m_k.
    inline constexpr float HORIZONTAL_PADDING = 50.f;  // WaveOverlayContainer
    inline constexpr float COUNTER_HEIGHT = 50.f;      // TotalCommentsCounter
    inline constexpr float EDITOR_PADDING = 20.f;      // around the editor
    inline constexpr float EDITOR_AVATAR = 50.f;
    inline constexpr float EDITOR_BORDER = 3.f;        // CommentEditor.BorderThickness
    inline constexpr float EDITOR_SIDE = 8.f;          // CommentEditor.side_padding
    inline constexpr float TEXTBOX_HEIGHT = 40.f;      // CommentEditor's text box
    inline constexpr float EDITOR_FOOTER = 35.f;
    inline constexpr float BUTTON_WIDTH = 80.f;        // EditorButton
    inline constexpr float BUTTON_HEIGHT = 25.f;
    inline constexpr float HEADER_HEIGHT = 40.f;       // CommentsHeader
    inline constexpr float TAB_HEIGHT = 20.f;          // HeaderButton
    inline constexpr float AVATAR = 40.f;              // DrawableComment.avatar_size
    inline constexpr float COMMENT_PADDING = 15.f;     // DrawableComment's vertical padding
    inline constexpr float VOTE_HEIGHT = 20.f;         // VotePill
    inline constexpr float PLACEHOLDER_HEIGHT = 80.f;  // NoCommentsPlaceholder
    inline constexpr float INFO_PADDING = 15.f;        // around the description and its chips
    inline constexpr float COPIED_MS = 1500.f;         // "copied" stays this long
    // GD's limit for a level comment (ShareCommentLayer's charLimit).
    inline constexpr int COMMENT_LIMIT = 100;
    // GD reports its own request failures; this catches one that never reports.
    inline constexpr float LOAD_TIMEOUT_MS = 30000.f;
    inline constexpr float SPIN_SPEED = 300.f;         // spinner, degrees per second

    inline constexpr ccColor4B GREEN_LIGHT {0xb3, 0xd9, 0x44, 255}; // OsuColour.GreenLight: VotePill's accent
    inline constexpr ccColor4B DANGER {204, 51, 85, 255};
    inline constexpr ccColor4B SEPARATOR {26, 26, 26, 255};        // OsuColour.Gray(0.1f)
    inline constexpr ccColor3B MUTED {128, 128, 128};              // OsuColour.Gray(0.5f): deleted comments
    inline constexpr ccColor4B CLEAR {0, 0, 0, 0};

    // Pill tags: the sort tabs are TAG_TAB + the sort they pick.
    inline constexpr int TAG_TAB = 1;

    inline std::string trim(std::string s) {
        auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (!s.empty() && space(s.back())) s.pop_back();
        size_t start = 0;
        while (start < s.size() && space(s[start])) start++;
        return s.substr(start);
    }

    // A player's icon (the one they show, like GD's comment cells) in their colours.
    inline CCNode* playerIcon(int id, IconType type, int color1, int color2, int glowColor, bool glow, float size) {
        auto gm = GameManager::get();
        auto player = SimplePlayer::create(1);
        player->updatePlayerFrame(std::max(1, id), type);
        player->setColors(gm->colorForIdx(color1), gm->colorForIdx(color2));
        if (glow) player->setGlowOutline(gm->colorForIdx(glowColor));
        else player->disableGlowOutline();
        player->setScale(size / 30.f);
        return player;
    }

    // osu!'s LoadingSpinner glyph. The glyph doesn't sit in the middle of its
    // label's line box: the holder turns around the glyph's own centre.
    inline CCNode* makeSpinner(float size, ccColor3B color) {
        auto holder = CCNode::create();
        auto glyph = makeIcon(icon::CIRCLE_NOTCH, size);
        glyph->setColor(color);
        glyph->setAnchorPoint({0, 0});
        if (auto letter = glyph->getChildByType<CCSprite>(0)) {
            glyph->setPosition(-letter->getPosition() * glyph->getScale());
        }
        holder->addChild(glyph);
        return holder;
    }
}

// Shared across the comments page's files, used as if they were in lazer itself.
using namespace commentsoverlay;

} // namespace lazer
