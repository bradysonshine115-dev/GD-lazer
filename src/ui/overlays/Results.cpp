// GD's level complete screen (EndLevelLayer, also shown when a level is
// finished in practice mode) in the pause menu's language: a dimmed screen,
// a big "level complete" title with the level under it, GD's line of praise,
// the run's statistics as osu!'s statistic displays, what the run earned
// (coins, stars, orbs, diamonds), GD's round buttons in the middle (retry
// biggest) and pills in a footer, with an intro of its own. GD's layer still
// drives it: it decides the numbers and runs every button's handler.
// Everything GD draws is hidden (never removed: mods find their nodes), every
// frame, since GD shows its labels and menus again once its own panel has
// dropped in.

#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "GameplayButtons.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

using namespace lazer::gameplay;

namespace {
    constexpr float PRAISE_SIZE = 24, STAT_HEADER = 14, STAT_VALUE = 22, STAT_WIDTH = 120, STAT_GAP = 12;
    constexpr float REWARDS_HEIGHT = 40, REWARD_GAP = 36;
    // The intro: everything fades in as the middle row scales up, the
    // statistics follow one by one (StatisticDisplay.Appear), then the rewards
    // pop.
    constexpr float FADE_IN_MS = 250, ROW_IN_MS = 600;
    constexpr float STAT_DELAY_MS = 350, STAT_STAGGER_MS = 150, STAT_FADE_MS = 120;
    constexpr float REWARD_DELAY_MS = 700, REWARD_STAGGER_MS = 150;

    constexpr ccColor4B HEADER_COLOUR {0, 0, 0, 110}; // StatisticDisplay header, over the dim

    // GD's coin sprites on its own panel: gold for collected, grey for not.
    constexpr std::array<char const*, 4> COIN_FRAMES {
        "GJ_coinsIcon_001.png", "GJ_coinsIcon2_001.png", "GJ_coinsIcon_gray_001.png", "GJ_coinsIcon2_gray_001.png",
    };

    // Set when the level is completed: this run is a first clear (or a
    // better platformer time).
    bool g_newBest = false;

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    CCNodeRGBA* group() {
        auto node = CCNodeRGBA::create();
        node->setCascadeOpacityEnabled(true);
        return node;
    }

    void fitWidth(CCNode* node, float maxWidth) {
        float w = node->getScaledContentSize().width;
        if (w > maxWidth && w > 0) node->setScale(node->getScale() * maxWidth / w);
    }

    std::string upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return s;
    }

    std::string trim(std::string s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    // Every node under `root`, depth first.
    void walk(CCNode* root, std::function<void(CCNode*)> const& fn) {
        for (auto child : CCArrayExt<CCNode*>(root->getChildren())) {
            fn(child);
            walk(child, fn);
        }
    }

    struct Stat {
        std::string header;
        std::string value;
    };

    // GD's order: attempts, jumps, time; anything else after.
    int statRank(std::string const& header) {
        if (header == "ATTEMPTS") return 0;
        if (header == "JUMPS") return 1;
        if (header == "TIME") return 2;
        return 3;
    }

    // What the run earned, as GD's own pictures with a count.
    struct Reward {
        CCNode* node;
        Tweened<float> pop {0.f};
    };

    class ResultsScreen : public CCNodeRGBA {
    public:
        static ResultsScreen* create(EndLevelLayer* layer) {
            auto ret = new ResultsScreen();
            if (ret->init(layer)) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void update(float dt) override;

    private:
        bool init(EndLevelLayer* layer);
        void build();
        void layout();
        void buildStats();
        void buildRewards();
        void hideGD();
        void readGD();
        void toggleHidden();

        EndLevelLayer* m_layer = nullptr;
        CCNode* m_main = nullptr;
        float m_k = 1;
        CCLayerColor* m_dim = nullptr;
        CCNodeRGBA* m_content = nullptr;  // everything but the dim and the eye: faded by the intro
        CCNode* m_titleBlock = nullptr;
        CCNode* m_bestPill = nullptr;
        CCLabelBMFont* m_praise = nullptr;
        CCNode* m_statsBlock = nullptr;
        std::vector<Stat> m_stats;
        bool m_statsBuilt = false;
        std::vector<CCLabelBMFont*> m_statValues;
        CCNode* m_rewardsBlock = nullptr;
        std::vector<CCSprite*> m_coins;   // copies of GD's coin sprites
        std::vector<Reward> m_rewards;
        bool m_rewardsBuilt = false;
        CCMenu* m_menu = nullptr;
        std::vector<AnimatedButtonItem*> m_middle;
        std::vector<CCNode*> m_captions;
        std::vector<AnimatedButtonItem*> m_footer;
        AnimatedButtonItem* m_eye = nullptr;
        bool m_hidden = false;
        Tweened<float> m_alpha {0.f};
        Tweened<float> m_rowScale {0.85f};
        Tweened<float> m_bestPop {0.f};
        float m_ms = 0;
        bool m_scanned = false;
    };

    bool ResultsScreen::init(EndLevelLayer* layer) {
        if (!CCNodeRGBA::init()) return false;
        m_layer = layer;
        m_main = layer->m_mainLayer;
        m_k = unitScale();
        this->setID("results"_spr);
        auto win = CCDirector::get()->getWinSize();
        readGD();

        auto dim = CCLayerColor::create({0, 0, 0, 255});
        dim->setContentSize(win);
        dim->setOpacity(0);
        m_dim = dim;
        this->addChild(dim, -1);

        m_content = group();
        m_content->setOpacity(0);
        this->addChild(m_content);
        build();
        hideGD();
        layout();

        // GD's arrow that hides its layer to show the level: ours hides the screen.
        auto eyeMenu = CCMenu::create();
        eyeMenu->setPosition({0, 0});
        this->addChild(eyeMenu, 2);
        float size = FOOTER_HEIGHT * m_k;
        auto eyeIcon = makeIcon(icon::EYE, size * 0.5f);
        anchorOnGlyph(eyeIcon);
        eyeIcon->setPosition({0, 0});
        m_eye = AnimatedButtonItem::create({size, size}, size / 2, GRAY4, eyeIcon, [this] { this->toggleHidden(); });
        m_eye->setPosition({win.width - 16 * m_k - size / 2, win.height - 16 * m_k - size / 2});
        eyeMenu->addChild(m_eye);

        // The intro.
        m_alpha.to(1, FADE_IN_MS, Easing::OutQuint);
        m_rowScale.to(1, ROW_IN_MS, Easing::OutQuint);
        this->scheduleUpdate();
        return true;
    }

    // GD's coins on its panel: copied, so the screen can show them itself.
    void ResultsScreen::readGD() {
        if (!m_coins.empty()) return;
        std::vector<std::pair<float, CCSprite*>> found;
        walk(m_main, [&](CCNode* node) {
            auto sprite = typeinfo_cast<CCSprite*>(node);
            if (!sprite) return;
            for (auto frame : COIN_FRAMES) {
                if (isSpriteFrameName(sprite, frame)) {
                    found.push_back({m_main->convertToNodeSpace(sprite->getParent()->convertToWorldSpace(sprite->getPosition())).x, sprite});
                    return;
                }
            }
        });
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (auto& [x, sprite] : found) {
            auto copy = CCSprite::createWithSpriteFrame(sprite->displayFrame());
            if (copy) m_coins.push_back(copy);
        }
    }

    // Everything GD draws goes, each frame: it shows its labels and menus
    // again once its panel has landed, and adds its reward effects then. Its
    // statistics (attempts, jumps, time) are read as they come.
    void ResultsScreen::hideGD() {
        for (auto child : CCArrayExt<CCNode*>(m_main->getChildren())) {
            if (child->isVisible()) child->setVisible(false);
        }
        // Mods add to the layer itself too (a hide-the-screen button, a death
        // tracker): everything on it but GD's panel and this screen goes.
        for (auto child : CCArrayExt<CCNode*>(m_layer->getChildren())) {
            if (child != m_main && child != this && child->isVisible()) child->setVisible(false);
        }
        if (m_statsBuilt) return;
        std::vector<Stat> stats;
        walk(m_main, [&](CCNode* node) {
            auto label = typeinfo_cast<CCLabelBMFont*>(node);
            if (!label) return;
            std::string text = trim(label->getString());
            auto colon = text.find(": ");
            if (colon != std::string::npos && colon > 0) stats.push_back({upper(trim(text.substr(0, colon))), trim(text.substr(colon + 2))});
        });
        if (!stats.empty()) {
            std::stable_sort(stats.begin(), stats.end(), [](Stat const& a, Stat const& b) { return statRank(a.header) < statRank(b.header); });
            m_stats = std::move(stats);
            buildStats();
        }
    }

    void ResultsScreen::build() {
        float k = m_k;
        auto win = CCDirector::get()->getWinSize();
        auto play = m_layer->m_playLayer;
        auto level = play ? play->m_level : nullptr;
        bool practice = play && play->m_isPracticeMode;
        EndLevelLayer* layer = m_layer;

        // The title, and the level underneath it.
        m_titleBlock = group();
        auto title = makeText(practice ? "practice complete" : "level complete", Weight::SemiBold, TITLE_SIZE * k);
        spaceLetters(title, TITLE_SPACING * k);
        title->setColor(theme::rgb(YELLOW));
        fitWidth(title, win.width - 80 * k);
        m_titleBlock->addChild(title);
        float titleH = TITLE_SIZE * k;
        float blockH = titleH;
        if (level) {
            std::string text = level->m_levelName;
            std::string creator = level->m_creatorName;
            if (!creator.empty() && level->m_levelType != GJLevelType::Editor) text += " by " + creator;
            auto desc = makeText(text, Weight::Regular, INFO_SIZE * k);
            desc->setColor({200, 200, 200});
            fitWidth(desc, win.width - 80 * k);
            blockH += 4 * k + INFO_SIZE * k;
            desc->setPosition({0, -blockH / 2 + INFO_SIZE * k / 2});
            m_titleBlock->addChild(desc);
        }
        title->setPosition({0, blockH / 2 - titleH / 2});
        m_titleBlock->setContentSize({0, blockH});
        m_content->addChild(m_titleBlock);

        // osu!'s "personal best" flair, as a pill beside the title.
        if (g_newBest && !practice) {
            auto pillText = makeText("new best", Weight::Bold, 13 * k);
            pillText->setColor({0x22, 0x22, 0x22});
            float pillW = pillText->getScaledContentSize().width + 18 * k;
            auto box = RoundedBox::create({pillW, 20 * k}, 10 * k, YELLOW);
            box->setAnchorPoint({0.5f, 0.5f});
            auto p = group();
            p->addChild(box);
            p->addChild(pillText);
            p->setScale(0);
            p->setPosition({title->getScaledContentSize().width / 2 + 14 * k + pillW / 2, blockH / 2 - titleH / 2});
            m_titleBlock->addChild(p);
            m_bestPill = p;
        }

        // GD's line of praise where osu! shows the score.
        char const* endText = layer->getEndText();
        std::string praiseText = endText ? trim(endText) : "";
        if (praiseText.empty()) praiseText = "Complete!";
        m_praise = makeText(praiseText, Weight::Regular, PRAISE_SIZE * k);
        fitWidth(m_praise, win.width - 80 * k);
        m_content->addChild(m_praise);

        // Statistics: GD's attempts, jumps and time, read when GD makes its
        // labels (buildStats).
        m_statsBlock = group();
        m_content->addChild(m_statsBlock);

        // What the run earned (practice earns nothing).
        m_rewardsBlock = group();
        m_content->addChild(m_rewardsBlock);
        if (!practice) buildRewards();

        // Everything clickable is in one menu, like GD's own buttons here.
        m_menu = CCMenu::create();
        m_menu->setID("results-buttons"_spr);
        m_menu->setPosition({0, 0});
        m_menu->setCascadeOpacityEnabled(true);
        m_content->addChild(m_menu, 1);

        // The middle row, GD's order: something on the left (the editor's
        // edit, practice's checkpoint, else the leaderboard), retry, menu.
        auto middle = [&](char const* glyph, float size, ccColor4B colour, std::string const& caption, std::function<void()> action) {
            auto item = roundButton(glyph, size * k, colour, std::move(action));
            m_menu->addChild(item);
            m_middle.push_back(item);
            auto label = makeText(caption, Weight::SemiBold, CAPTION_SIZE * k);
            label->setColor({220, 220, 220});
            m_content->addChild(label);
            m_captions.push_back(label);
        };
        bool editor = level && level->m_levelType == GJLevelType::Editor;
        bool online = level && level->m_levelType != GJLevelType::Editor && level->m_levelType != GJLevelType::Main
            && level->m_levelID.value() > 0;
        bool leaderboardInRow = false;
        if (editor) middle(icon::PEN, SIDE_SIZE, BLUE, "edit", [layer] { layer->onEdit(nullptr); });
        else if (practice) middle(icon::GEM, SIDE_SIZE, BLUE, "checkpoint", [layer] { layer->onRestartCheckpoint(nullptr); });
        else if (online) {
            leaderboardInRow = true;
            middle(icon::RANKING_STAR, SIDE_SIZE, BLUE, "leaderboard", [layer] { layer->onLevelLeaderboard(nullptr); });
        }
        middle(icon::ROTATE, PLAY_SIZE, GREEN, "retry", [layer] { layer->onReplay(nullptr); });
        middle(icon::LIST, SIDE_SIZE, GRAY5, "menu", [layer] { layer->onMenu(nullptr); });

        // The footer: what didn't fit the row, and other mods' buttons (update).
        if (online && !leaderboardInRow) {
            auto item = pillButton(icon::RANKING_STAR, "leaderboard", FOOTER_HEIGHT * k, k, GRAY4, [layer] { layer->onLevelLeaderboard(nullptr); });
            m_menu->addChild(item);
            m_footer.push_back(item);
        }
    }

    void ResultsScreen::layout() {
        float k = m_k;
        auto win = CCDirector::get()->getWinSize();
        float W = win.width, H = win.height;

        float footerTop = 0;
        if (!m_footer.empty()) {
            float y = FOOTER_MARGIN * k + FOOTER_HEIGHT * k / 2;
            layoutRow(m_footer, std::vector<float>(m_footer.size(), FOOTER_GAP * k), y, W / 2, W - 32 * k);
            footerTop = FOOTER_MARGIN * k + FOOTER_HEIGHT * k;
        }

        // From the top: title, praise, statistics, rewards.
        float titleH = m_titleBlock->getContentSize().height;
        m_titleBlock->setPosition({W / 2, H - TOP_MARGIN * k - titleH / 2});
        float top = H - TOP_MARGIN * k - titleH;
        top -= 18 * k;
        m_praise->setPosition({W / 2, top - PRAISE_SIZE * k / 2});
        top -= PRAISE_SIZE * k;
        top -= 18 * k;
        float statsH = STAT_HEADER * k + 4 * k + STAT_VALUE * k;
        m_statsBlock->setPosition({W / 2, top});
        top -= statsH;
        float rewardsH = m_rewardsBlock->getContentSize().height;
        if (rewardsH > 0) {
            top -= 16 * k;
            m_rewardsBlock->setPosition({W / 2, top - rewardsH / 2});
            top -= rewardsH;
        }

        // The middle row and its captions, centred in what's left (short
        // screens shrink it).
        float room = top - footerTop;
        float rowH = PLAY_SIZE * k + CAPTION_GAP * k + CAPTION_SIZE * k;
        float shrink = rowH + 30 * k > room && rowH > 0 ? std::max(0.5f, (room - 30 * k) / rowH) : 1.f;
        rowH *= shrink;
        float blockTop = footerTop + (room + rowH) / 2;
        float centreY = blockTop - PLAY_SIZE * k * shrink / 2;
        float total = 0;
        for (auto item : m_middle) total += item->getContentSize().width * shrink;
        total += MIDDLE_GAP * k * shrink * (m_middle.size() - 1);
        float x = W / 2 - total / 2;
        for (size_t i = 0; i < m_middle.size(); i++) {
            auto item = m_middle[i];
            float w = item->getContentSize().width * shrink;
            item->setUserObject("fit"_spr, CCFloat::create(shrink));
            item->setScale(shrink * m_rowScale.get());
            item->setPosition({x + w / 2, centreY});
            m_captions[i]->setPosition({x + w / 2, blockTop - rowH + CAPTION_SIZE * k / 2});
            x += w + MIDDLE_GAP * k * shrink;
        }
    }

    // StatisticDisplay: a dark header pill, the value under it.
    void ResultsScreen::buildStats() {
        m_statsBuilt = true;
        if (m_stats.empty()) return;
        float k = m_k;
        auto win = CCDirector::get()->getWinSize();
        float colW = STAT_WIDTH * k, gap = STAT_GAP * k;
        float total = colW * m_stats.size() + gap * (m_stats.size() - 1);
        float maxW = win.width - 40 * k;
        if (total > maxW) {
            colW = (maxW - gap * (m_stats.size() - 1)) / m_stats.size();
            total = maxW;
        }
        float x = -total / 2;
        for (auto& stat : m_stats) {
            auto head = RoundedBox::create({colW, STAT_HEADER * k}, STAT_HEADER * k / 2, HEADER_COLOUR);
            head->setAnchorPoint({0.5f, 0.5f});
            head->setPosition({x + colW / 2, -STAT_HEADER * k / 2});
            m_statsBlock->addChild(head);
            auto headText = makeText(stat.header, Weight::SemiBold, 11 * k);
            headText->setColor({220, 220, 220});
            fitWidth(headText, colW - 12 * k);
            headText->setPosition({x + colW / 2, -STAT_HEADER * k / 2});
            m_statsBlock->addChild(headText);
            auto value = makeText(stat.value, Weight::SemiBold, STAT_VALUE * k);
            fitWidth(value, colW);
            value->setPosition({x + colW / 2, -STAT_HEADER * k - 4 * k - STAT_VALUE * k / 2});
            value->setOpacity(0);
            m_statsBlock->addChild(value);
            m_statValues.push_back(value);
            x += colW + gap;
        }
    }

    // The coins, then each currency with its count, in a row.
    void ResultsScreen::buildRewards() {
        m_rewardsBuilt = true;
        auto play = m_layer->m_playLayer;
        float k = m_k;
        float iconH = 26 * k;
        std::vector<CCNode*> items;

        if (!m_coins.empty()) {
            auto row = group();
            float gap = 6 * k, x = 0;
            for (auto coin : m_coins) {
                float s = iconH / std::max(1.f, coin->getContentSize().height);
                coin->setScale(s);
                coin->setAnchorPoint({0, 0.5f});
                coin->setPosition({x, 0});
                row->addChild(coin);
                x += coin->getScaledContentSize().width + gap;
            }
            row->setContentSize({x - gap, iconH});
            items.push_back(row);
        }

        auto currency = [&](char const* frame, int count) {
            if (count <= 0) return;
            auto sprite = CCSprite::createWithSpriteFrameName(frame);
            if (!sprite) return;
            auto row = group();
            float s = iconH / std::max(1.f, sprite->getContentSize().height);
            sprite->setScale(s);
            sprite->setAnchorPoint({0, 0.5f});
            sprite->setPosition({0, 0});
            row->addChild(sprite);
            auto label = makeText(fmt::format("+{}", count), Weight::SemiBold, 18 * k);
            label->setAnchorPoint({0, 0.5f});
            float x = sprite->getScaledContentSize().width + 6 * k;
            label->setPosition({x, 0});
            row->addChild(label);
            row->setContentSize({x + label->getScaledContentSize().width, iconH});
            items.push_back(row);
        };
        bool platformer = play && play->m_level && play->m_level->isPlatformer();
        currency(platformer ? "GJ_bigMoon_001.png" : "GJ_bigStar_001.png", std::max(m_layer->m_stars, m_layer->m_moons));
        currency("currencyOrbIcon_001.png", std::max(m_layer->m_orbs, play ? play->m_orbs : 0));
        currency("GJ_bigDiamond_001.png", std::max(m_layer->m_diamonds, play ? play->m_diamonds : 0));

        if (items.empty()) return;
        float total = 0;
        for (auto item : items) total += item->getContentSize().width;
        total += REWARD_GAP * k * (items.size() - 1);
        float x = -total / 2;
        for (auto item : items) {
            float w = item->getContentSize().width;
            // Growing from its middle when it pops.
            auto holder = group();
            holder->setPosition({x + w / 2, 0});
            item->setPosition({-w / 2, 0});
            holder->addChild(item);
            holder->setScale(0);
            m_rewardsBlock->addChild(holder);
            m_rewards.push_back({holder});
            x += w + REWARD_GAP * k;
        }
        m_rewardsBlock->setContentSize({total, REWARDS_HEIGHT * k});
    }

    // GD's hide-the-layer arrow: the screen out of the way, to see the level
    // behind; again to bring it back.
    void ResultsScreen::toggleHidden() {
        m_hidden = !m_hidden;
        m_alpha.to(m_hidden ? 0.f : 1.f, 250, Easing::OutQuint);
        m_menu->setEnabled(!m_hidden);
    }

    void ResultsScreen::update(float dt) {
        if (!m_scanned) {
            // After every mod's customSetup: their buttons join the footer.
            m_scanned = true;
            float k = m_k;
            float h = FOOTER_HEIGHT * k;
            bool added = false;
            auto found = collectModButtons(m_main);
            for (auto item : collectModButtons(m_layer)) found.push_back(item);
            for (auto item : found) {
                Ref<CCMenuItem> target = item;
                auto button = AnimatedButtonItem::create({h, h}, 10 * k, GRAY4, modButtonImage(item, h * 0.72f),
                                                         [target] { target->activate(); });
                m_menu->addChild(button);
                m_footer.push_back(button);
                added = true;
            }
            if (added) layout();
        }
        hideGD();
        if (!m_rewardsBuilt && m_layer->m_playLayer && !m_layer->m_playLayer->m_isPracticeMode) {
            // GD's coins may come after its setup.
            readGD();
            if (!m_coins.empty()) {
                buildRewards();
                layout();
            }
        }

        m_ms += dt * 1000.f;
        m_alpha.update(dt);
        m_rowScale.update(dt);
        m_content->setOpacity(toByte(m_alpha.get()));
        m_dim->setOpacity(toByte(m_alpha.get() * BACKGROUND_ALPHA));
        for (auto item : m_middle) {
            float fit = static_cast<CCFloat*>(item->getUserObject("fit"_spr))->getValue();
            item->setScale(fit * m_rowScale.get());
        }

        // Statistics appear one after another (StatisticDisplay.Appear).
        for (size_t i = 0; i < m_statValues.size(); i++) {
            float t = (m_ms - STAT_DELAY_MS - STAT_STAGGER_MS * i) / STAT_FADE_MS;
            auto value = m_statValues[i];
            if (value->getOpacity() != 255) value->setOpacity(toByte(t));
        }
        // The "new best" pill and the rewards pop.
        if (m_bestPill && m_ms >= 400 && m_bestPop.target() < 1) m_bestPop.to(1, 600, Easing::OutElastic);
        if (m_bestPill) {
            m_bestPop.update(dt);
            m_bestPill->setScale(m_bestPop.get());
        }
        for (size_t i = 0; i < m_rewards.size(); i++) {
            auto& reward = m_rewards[i];
            if (reward.pop.target() < 1 && m_ms >= REWARD_DELAY_MS + REWARD_STAGGER_MS * i) {
                reward.pop.to(1, 700, Easing::OutElastic);
            }
            reward.pop.update(dt);
            reward.node->setScale(reward.pop.get());
        }

#ifdef GEODE_IS_DESKTOP
        bool blocked = gameplayPopupOnTop() || m_hidden;
        auto mouse = geode::cocos::getMousePos();
        for (auto item : m_middle) item->setHovered(!blocked && item->containsWorldPoint(mouse));
        for (auto item : m_footer) item->setHovered(!blocked && item->containsWorldPoint(mouse));
        m_eye->setHovered(!gameplayPopupOnTop() && m_eye->containsWorldPoint(mouse));
#endif
    }
}

} // namespace lazer

// Remembers, as the level is completed, whether this run beats the best.
class $modify(LazerResultsPlayLayer, PlayLayer) {
    void levelComplete() {
        lazer::g_newBest = false;
        if (m_level && !m_isPracticeMode && !m_isTestMode) {
            if (m_level->isPlatformer()) {
                int best = m_level->m_bestTime;
                lazer::g_newBest = best <= 0 || m_attemptTime * 1000.0 < best;
            } else {
                lazer::g_newBest = m_level->m_normalPercent.value() < 100;
            }
        }
        PlayLayer::levelComplete();
    }
};

class $modify(LazerEndLevelLayer, EndLevelLayer) {
    static void onModify(auto& self) {
        // Straight after GD's own setup, before other mods add theirs.
        (void)self.setHookPriorityPost("EndLevelLayer::customSetup", Priority::VeryEarlyPost);
    }

    void customSetup() {
        EndLevelLayer::customSetup();
        if (!Mod::get()->getSettingValue<bool>("restyle-gameplay")) return;
        if (!m_mainLayer || !m_playLayer) return;
        auto screen = lazer::ResultsScreen::create(this);
        if (!screen) return;
        // On the layer itself, not GD's dropping panel: the screen has its
        // own way in.
        this->addChild(screen, 100);
    }
};
