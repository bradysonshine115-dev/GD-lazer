#include "LevelFacts.hpp"

#include "../../levels/LevelLibrary.hpp"
#include "../core/Text.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer::facts {

namespace {
    // OsuColour's status colours (ForBeatmapSetOnlineStatus), for GD's ratings.
    constexpr ccColor4B LIME1 {0xb3, 0xd9, 0x44, 255};
    constexpr ccColor4B BLUE1 {0x66, 0xcc, 0xff, 255};
    constexpr ccColor4B ORANGE1 {0xff, 0xcc, 0x22, 255};
    constexpr ccColor4B PURPLE1 {0x88, 0x66, 0xee, 255};
    constexpr ccColor4B PINK1 {0xff, 0x66, 0xaa, 255};
    constexpr ccColor4B GRAY {0x99, 0x99, 0x99, 255};
    // ForBeatmapSetOnlineStatus again, for your own levels: uploaded like
    // Qualified, verified like Pending, not yet verified like WIP.
    constexpr ccColor4B UPLOADED_COLOUR {0x66, 0xcc, 0xff, 255};
    constexpr ccColor4B VERIFIED_COLOUR {0xff, 0xd9, 0x66, 255};
    constexpr ccColor4B UNVERIFIED_COLOUR {0xff, 0x99, 0x66, 255};
}

std::string withCommas(long long v) {
    auto s = fmt::format("{}", v);
    for (int i = int(s.size()) - 3; i > (s[0] == '-' ? 1 : 0); i -= 3) s.insert(size_t(i), ",");
    return s;
}

std::string metric(long long n) {
    std::string sign = n < 0 ? "-" : "";
    if (n < 0) n = -n;
    auto one = [](double v, char suffix) {
        auto s = fmt::format("{:.1f}", v);
        if (s.ends_with(".0")) s.resize(s.size() - 2);
        return s + suffix;
    };
    if (n < 1000) return sign + std::to_string(n);
    if (n < 1000000) return sign + one(n / 1000.0, 'k');
    return sign + one(n / 1000000.0, 'M');
}

std::string formatTime(int ms) {
    int minutes = ms / 60000;
    int seconds = ms / 1000 % 60;
    if (minutes > 0) return fmt::format("{}:{:02}.{:03}", minutes, seconds, ms % 1000);
    return fmt::format("{}.{:03}", seconds, ms % 1000);
}

int difficultyFrame(GJGameLevel* level) {
    if (level->m_autoLevel) return -1;
    if (level->m_demon.value() > 0) {
        switch (level->m_demonDifficulty) {
            case 3: return 7;  // easy demon
            case 4: return 8;  // medium
            case 5: return 9;  // insane
            case 6: return 10; // extreme
            default: return 6; // hard
        }
    }
    return std::clamp(level->getAverageDifficulty(), 0, 5);
}

// GJLevelList::frameForListDifficulty.
int listFrame(int diff) {
    if (diff == 0) return -1; // auto
    switch (diff) {
        case 6: return 7;
        case 7: return 8;
        case 8: return 6;
        default: return diff >= 1 && diff <= 10 ? diff : 0;
    }
}

GJFeatureState featureState(GJGameLevel* level) {
    switch (level->m_isEpic) {
        case 1: return GJFeatureState::Epic;
        case 2: return GJFeatureState::Legendary;
        case 3: return GJFeatureState::Mythic;
        default: return level->m_featured > 0 ? GJFeatureState::Featured : GJFeatureState::None;
    }
}

CCNode* difficultyFace(int frame, GJFeatureState state, float size) {
    auto face = GJDifficultySprite::create(frame, GJDifficultyName::Short);
    face->updateFeatureState(state);
    face->setCascadeOpacityEnabled(true); // the feature glow fades with it
    auto s = face->getContentSize();
    face->setScale(size / std::max(1.f, std::max(s.width, s.height)));
    return face;
}

std::string lengthName(GJGameLevel* level) {
    if (level->isPlatformer()) return "platformer";
    return levels::lengthName(level->m_levelLength);
}

Status levelStatus(GJGameLevel* level) {
    switch (level->m_isEpic) {
        case 3: return {"mythic", PINK1};
        case 2: return {"legendary", PURPLE1};
        case 1: return {"epic", ORANGE1};
        default: break;
    }
    if (level->m_featured > 0) return {"featured", LIME1};
    if (level->m_stars.value() > 0) return {"rated", BLUE1};
    return {nullptr, GRAY};
}

Status listStatus(GJLevelList* list) {
    return list->m_featured ? Status {"featured", LIME1} : Status {nullptr, GRAY};
}

Status localStatus(GJGameLevel* level, GJLevelList* list) {
    if (list) return list->m_uploaded || list->m_listID > 0 ? Status {"uploaded", UPLOADED_COLOUR} : Status {"not uploaded", UNVERIFIED_COLOUR};
    if (level->m_isUploaded || level->m_levelID.value() > 0) return {"uploaded", UPLOADED_COLOUR};
    if (level->m_isVerified.value() != 0) return {"verified", VERIFIED_COLOUR};
    return {"unverified", UNVERIFIED_COLOUR};
}

CCNode* statsRow(std::vector<Stat> const& items, float size, ccColor3B iconColor, ccColor3B textColor) {
    auto row = CCNodeRGBA::create();
    row->setCascadeOpacityEnabled(true);
    float x = 0;
    for (auto const& [glyph, text] : items) {
        if (glyph) {
            auto icon = makeIcon(glyph, size * 0.8f);
            icon->setColor(iconColor);
            icon->setAnchorPoint({0, 0.5f});
            icon->setPosition({x, 0});
            row->addChild(icon);
            x += icon->getScaledContentSize().width + size * 0.4f;
        }
        auto label = makeText(text, Weight::SemiBold, size);
        label->setColor(textColor);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, 0});
        row->addChild(label);
        x += label->getScaledContentSize().width + size * 1.1f;
    }
    row->setContentSize({x, size});
    return row;
}

void fit(CCLabelBMFont* label, float maxWidth) {
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

CCNode* playerIcon(GJUserScore* s, float size) {
    auto gm = GameManager::get();
    auto player = SimplePlayer::create(1);
    player->updatePlayerFrame(std::max(1, s->m_iconID), s->m_iconType);
    player->setColors(gm->colorForIdx(s->m_color1), gm->colorForIdx(s->m_color2));
    if (s->m_glowEnabled) player->setGlowOutline(gm->colorForIdx(s->m_color3));
    else player->disableGlowOutline();
    player->setScale(size / 30.f);
    return player;
}

CCNode* makeSpinner(float size, ccColor3B color) {
    // The glyph doesn't sit in the middle of its label's line box: the holder
    // turns around the glyph's own centre.
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

} // namespace lazer::facts
