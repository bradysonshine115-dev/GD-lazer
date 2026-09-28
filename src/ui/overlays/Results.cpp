// GD's level complete screen (EndLevelLayer, also shown when a level is
// finished in practice mode) as osu!'s results card (Screens/Ranking/
// ScorePanel, expanded): the level and its creator, GD's line of praise, the
// run's statistics as osu!'s statistic displays and the buttons underneath.
// GD's layer still drives it: it drops in, plays its own reward and coin
// animations (moved onto the card) and runs every button's handler. Its
// panel, chains, labels and menus are hidden, never removed.

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

namespace {
    constexpr float PANEL_WIDTH = 360;        // ScorePanel.EXPANDED_WIDTH
    constexpr float TOP_LAYER = 44;           // visible top layer (EXPANDED_TOP_LAYER_HEIGHT is 53)
    constexpr float CORNER = 20;
    constexpr float PADDING = 12;
    constexpr float BUTTON_HEIGHT = 30;       // the results screen's buttons
    constexpr float BUTTON_SPACING = 5;
    constexpr float REWARDS_HEIGHT = 46;
    constexpr float STAT_DELAY_MS = 500;      // after GD's drop-in
    constexpr float STAT_STAGGER_MS = 200, STAT_FADE_MS = 100; // ExpandedPanelMiddleContent

    constexpr ccColor4B TOP_COLOUR {0x3b, 0x3b, 0x3b, 255};    // #444 -> #333
    constexpr ccColor4B MIDDLE_COLOUR {0x44, 0x44, 0x44, 255}; // #555 -> #333
    constexpr ccColor4B HEADER_COLOUR {0x22, 0x22, 0x22, 255}; // StatisticDisplay header
    constexpr ccColor4B GREEN {0x88, 0xb3, 0x00, 255};
    constexpr ccColor4B YELLOW {0xff, 0xcc, 0x22, 255};
    constexpr ccColor4B GRAY5 {0x55, 0x55, 0x55, 255};

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

    // A rectangle of `node` in `space`'s coordinates.
    CCRect rectIn(CCNode* node, CCNode* space) {
        auto box = node->boundingBox();
        auto parent = node->getParent();
        if (!parent || parent == space) return box;
        auto a = space->convertToNodeSpace(parent->convertToWorldSpace(box.origin));
        auto b = space->convertToNodeSpace(parent->convertToWorldSpace({box.getMaxX(), box.getMaxY()}));
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
    }

    struct Stat {
        std::string header;
        std::string value;
    };

    class ResultsCard : public CCNodeRGBA {
    public:
        static ResultsCard* create(EndLevelLayer* layer) {
            auto ret = new ResultsCard();
            if (ret->init(layer)) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void update(float dt) override;
        // Once the card is on GD's layer: GD's coins and reward effects move
        // onto it.
        void placeRewards();

    private:
        bool init(EndLevelLayer* layer);
        void readGD(CCRect panel);
        void build(float u, float width);
        void layoutButtons();

        EndLevelLayer* m_layer = nullptr;
        CCNode* m_main = nullptr;
        CCNode* m_column = nullptr;
        float m_u = 1;
        float m_width = 0;
        std::vector<Stat> m_stats;
        std::vector<std::string> m_texts;
        std::vector<CCNode*> m_art;       // GD's pictures on its panel (coins)
        CCMenu* m_menu = nullptr;
        std::vector<AnimatedButtonItem*> m_buttons;
        float m_buttonsY = 0;
        CCPoint m_rewardsCentre {};       // in the column's space
        bool m_hasRewards = false;
        std::vector<CCLabelBMFont*> m_statValues;
        float m_ms = 0;
        bool m_scanned = false;
    };

    bool ResultsCard::init(EndLevelLayer* layer) {
        if (!CCNodeRGBA::init()) return false;
        m_layer = layer;
        m_main = layer->m_mainLayer;
        this->setID("results"_spr);
        this->setCascadeOpacityEnabled(true);
        auto win = CCDirector::get()->getWinSize();

        // GD's panel is the drop-down's list layer; the card takes its place.
        CCRect panel {win.width / 2 - 178, win.height / 2 - 115, 356, 230};
        if (layer->m_listLayer && layer->m_listLayer->getParent()) panel = rectIn(layer->m_listLayer, m_main);
        readGD(panel);

        // osu!'s card scaled to the width of GD's panel, so GD's own art
        // (coins, rewards) keeps its proportions on it.
        m_u = panel.size.width / PANEL_WIDTH;
        build(m_u, panel.size.width);

        // Short screens: the whole card shrinks to fit.
        float height = this->getContentSize().height;
        float fit = std::min(1.f, (win.height - 16) / std::max(1.f, height));
        this->setScale(fit);
        float centreY = panel.getMidY();
        float half = height * fit / 2;
        centreY = std::clamp(centreY, half + 8, std::max(half + 8, win.height - half - 8));
        this->setAnchorPoint({0.5f, 0.5f});
        this->setPosition({panel.getMidX(), centreY});

        this->scheduleUpdate();
        return true;
    }

    // What GD put on its panel: its labels become our text, its pictures
    // (coins) move onto the card, the rest is hidden.
    void ResultsCard::readGD(CCRect panel) {
        auto layer = m_layer;
        std::vector<CCNode*> children;
        for (auto child : CCArrayExt<CCNode*>(m_main->getChildren())) children.push_back(child);
        for (auto child : children) {
            if (child == this) continue;
            if (child == layer->m_listLayer) {
                child->setVisible(false);
                continue;
            }
            // GD's arrow that hides the layer to show the level stays.
            if (child == layer->m_sideMenu) continue;
            if (typeinfo_cast<CCMenu*>(child)) {
                child->setVisible(false);
                continue;
            }
            if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) {
                if (label->isVisible()) {
                    std::string text = trim(label->getString());
                    auto colon = text.find(": ");
                    if (colon != std::string::npos && colon > 0) {
                        m_stats.push_back({upper(trim(text.substr(0, colon))), trim(text.substr(colon + 2))});
                    } else if (!text.empty()) {
                        m_texts.push_back(text);
                    }
                }
                child->setVisible(false);
                continue;
            }
            if (typeinfo_cast<TextArea*>(child) || typeinfo_cast<MultilineBitmapFont*>(child)) {
                child->setVisible(false);
                continue;
            }
            if (!child->isVisible()) continue;
            auto box = rectIn(child, m_main);
            // The chains hanging the panel are outside it; coins are on it.
            if (panel.containsPoint({box.getMidX(), box.getMidY()})) m_art.push_back(child);
            else child->setVisible(false);
        }
    }

    void ResultsCard::build(float u, float width) {
        auto play = m_layer->m_playLayer;
        auto level = play ? play->m_level : nullptr;
        bool practice = play && play->m_isPracticeMode;
        EndLevelLayer* layer = m_layer;
        m_width = width;
        float inner = width - 2 * PADDING * u;

        // Laid out top-down from y = 0, then moved up by the height.
        auto column = group();
        m_column = column;
        this->addChild(column);
        float y = 0;

        // Top layer: what happened.
        auto status = group();
        auto statusText = makeText(practice ? "practice complete" : "level complete", Weight::SemiBold, 18 * u);
        float statusW = statusText->getScaledContentSize().width;
        CCNode* pill = nullptr;
        float pillW = 0;
        if (g_newBest && !practice) {
            // osu!'s "personal best" flair, as a pill.
            auto pillText = makeText("new best", Weight::Bold, 12 * u);
            pillText->setColor({0x22, 0x22, 0x22});
            pillW = pillText->getScaledContentSize().width + 16 * u;
            auto box = RoundedBox::create({pillW, 18 * u}, 9 * u, YELLOW);
            box->setAnchorPoint({0.5f, 0.5f});
            auto p = group();
            p->addChild(box);
            p->addChild(pillText);
            pill = p;
        }
        float rowW = statusW + (pill ? 8 * u + pillW : 0);
        statusText->setAnchorPoint({0, 0.5f});
        statusText->setPosition({-rowW / 2, 0});
        status->addChild(statusText);
        if (pill) {
            pill->setPosition({rowW / 2 - pillW / 2, 0});
            status->addChild(pill);
        }
        status->setContentSize({rowW, 18 * u});
        fitWidth(status, inner);
        status->setPosition({width / 2, y - TOP_LAYER * u / 2});
        column->addChild(status, 1);
        y -= TOP_LAYER * u;
        float middleTop = y;

        // The level (ClickableMetadata).
        y -= PADDING * u;
        if (level) {
            auto title = makeText(level->m_levelName, Weight::SemiBold, 20 * u);
            fitWidth(title, inner);
            title->setPosition({width / 2, y - 10 * u});
            column->addChild(title, 1);
            y -= 22 * u;
            std::string creator = level->m_creatorName;
            if (!creator.empty() && level->m_levelType != GJLevelType::Editor) {
                auto by = group();
                auto a = makeText("by ", Weight::Regular, 13 * u);
                auto b = makeText(creator, Weight::SemiBold, 13 * u);
                float wa = a->getScaledContentSize().width, wb = b->getScaledContentSize().width;
                a->setAnchorPoint({0, 0.5f});
                a->setPosition({-(wa + wb) / 2, 0});
                b->setAnchorPoint({0, 0.5f});
                b->setPosition({-(wa + wb) / 2 + wa, 0});
                by->addChild(a);
                by->addChild(b);
                by->setContentSize({wa + wb, 13 * u});
                fitWidth(by, inner);
                by->setPosition({width / 2, y - 7 * u});
                column->addChild(by, 1);
                y -= 16 * u;
            }
        }

        // GD's line of praise where osu! shows the score, then any other
        // lines GD had.
        y -= 8 * u;
        auto praise = makeText(m_texts.empty() ? "Complete!" : m_texts.front(), Weight::Regular, 30 * u);
        fitWidth(praise, inner);
        praise->setPosition({width / 2, y - 15 * u});
        column->addChild(praise, 1);
        y -= 32 * u;
        for (size_t i = 1; i < m_texts.size(); i++) {
            auto line = makeText(m_texts[i], Weight::Regular, 13 * u);
            line->setColor({200, 200, 200});
            fitWidth(line, inner);
            line->setPosition({width / 2, y - 7 * u});
            column->addChild(line, 1);
            y -= 16 * u;
        }

        // Statistics (StatisticDisplay: a dark header pill, the value under it).
        if (!m_stats.empty()) {
            y -= 10 * u;
            float gap = 6 * u;
            float colW = (inner - gap * (m_stats.size() - 1)) / m_stats.size();
            float x = PADDING * u;
            for (auto& stat : m_stats) {
                auto head = RoundedBox::create({colW, 14 * u}, 7 * u, HEADER_COLOUR);
                head->setAnchorPoint({0.5f, 0.5f});
                head->setPosition({x + colW / 2, y - 7 * u});
                column->addChild(head, 1);
                auto headText = makeText(stat.header, Weight::SemiBold, 11 * u);
                fitWidth(headText, colW - 8 * u);
                headText->setPosition({x + colW / 2, y - 7 * u});
                column->addChild(headText, 1);
                auto value = makeText(stat.value, Weight::Regular, 20 * u);
                fitWidth(value, colW);
                value->setPosition({x + colW / 2, y - 14 * u - 3 * u - 10 * u});
                value->setOpacity(0);
                column->addChild(value, 1);
                m_statValues.push_back(value);
                x += colW + gap;
            }
            y -= 14 * u + 3 * u + 20 * u;
        }

        // GD's coins and rewards land in a band of their own.
        int stars = 0, orbs = 0, diamonds = 0;
        if (play) {
            stars = std::max(m_layer->m_stars, m_layer->m_moons);
            orbs = std::max(m_layer->m_orbs, play->m_orbs);
            diamonds = std::max(m_layer->m_diamonds, play->m_diamonds);
        }
        m_hasRewards = !m_art.empty() || stars > 0 || orbs > 0 || diamonds > 0;
        if (m_hasRewards) {
            y -= 10 * u;
            auto band = RoundedBox::create({inner, REWARDS_HEIGHT * u}, 10 * u, {0, 0, 0, 40});
            band->setAnchorPoint({0.5f, 0.5f});
            band->setPosition({width / 2, y - REWARDS_HEIGHT * u / 2});
            column->addChild(band, 1);
            m_rewardsCentre = {width / 2, y - REWARDS_HEIGHT * u / 2};
            y -= REWARDS_HEIGHT * u;
        }

        // Buttons.
        y -= 14 * u;
        m_buttonsY = y - BUTTON_HEIGHT * u / 2;
        y -= BUTTON_HEIGHT * u + 16 * u;
        float height = -y;

        m_menu = CCMenu::create();
        m_menu->setID("results-buttons"_spr);
        m_menu->setPosition({0, 0});
        m_menu->setCascadeOpacityEnabled(true);
        column->addChild(m_menu, 2);

        float h = BUTTON_HEIGHT * u, text = 13 * u, radius = 10 * u;
        auto add = [&](char const* glyph, std::string const& label, ccColor4B colour, float minWidth, std::function<void()> action) {
            auto content = iconLabel(glyph, label, text);
            float w = std::max(minWidth, content->getContentSize().width + 24 * u);
            auto item = AnimatedButtonItem::create({w, h}, radius, colour, content, std::move(action));
            m_menu->addChild(item);
            m_buttons.push_back(item);
        };
        add(icon::ROTATE, "retry", GREEN, 110 * u, [layer] { layer->onReplay(nullptr); });
        if (practice) add(icon::GEM, "checkpoint", GRAY5, 0, [layer] { layer->onRestartCheckpoint(nullptr); });
        if (level && level->m_levelType == GJLevelType::Editor) {
            add(icon::PEN, "edit", GRAY5, 0, [layer] { layer->onEdit(nullptr); });
        }
        if (level && level->m_levelType != GJLevelType::Editor && level->m_levelType != GJLevelType::Main
            && level->m_levelID.value() > 0) {
            add(icon::RANKING_STAR, "leaderboard", GRAY5, 0, [layer] { layer->onLevelLeaderboard(nullptr); });
        }
        add(icon::LIST, "menu", GRAY5, 0, [layer] { layer->onMenu(nullptr); });

        // The card: osu!'s top layer peeking out above the middle one.
        auto top = RoundedBox::create({width, TOP_LAYER * u + CORNER * u}, CORNER * u, TOP_COLOUR);
        top->setAnchorPoint({0, 1});
        top->setPosition({0, 0});
        top->setShadow(12 * u, {0, 0, 0, 60});
        column->addChild(top, -1);
        auto middle = RoundedBox::create({width, height + middleTop}, CORNER * u, MIDDLE_COLOUR);
        middle->setShadow(12 * u, {0, 0, 0, 60});
        middle->setAnchorPoint({0, 1});
        middle->setPosition({0, middleTop});
        column->addChild(middle, 0);

        column->setPosition({0, height});
        this->setContentSize({width, height});
        layoutButtons();
    }

    void ResultsCard::layoutButtons() {
        float u = m_u;
        float gap = BUTTON_SPACING * u;
        float maxW = m_width - 2 * PADDING * u;
        float total = 0;
        for (auto item : m_buttons) total += item->getContentSize().width;
        total += gap * (m_buttons.empty() ? 0 : m_buttons.size() - 1);
        float scale = total > maxW && total > 0 ? maxW / total : 1.f;
        float x = m_width / 2 - total * scale / 2;
        for (auto item : m_buttons) {
            float w = item->getContentSize().width * scale;
            item->setScale(scale);
            item->setPosition({x + w / 2, m_buttonsY});
            x += w + gap * scale;
        }
    }

    // GD's coins and reward effects are laid out on its own panel: move them
    // into the card's band, side by side.
    void ResultsCard::placeRewards() {
        if (!m_hasRewards) return;
        auto play = m_layer->m_playLayer;
        int stars = 0, orbs = 0, diamonds = 0;
        if (play) {
            stars = std::max(m_layer->m_stars, m_layer->m_moons);
            orbs = std::max(m_layer->m_orbs, play->m_orbs);
            diamonds = std::max(m_layer->m_diamonds, play->m_diamonds);
        }
        int slots = (m_art.empty() ? 0 : 1) + (stars > 0) + (orbs > 0) + (diamonds > 0);
        if (slots == 0) slots = 1;
        auto column = m_column;
        if (!column || !this->getParent()) return;
        float inner = m_width - 2 * PADDING * m_u;
        auto slotAt = [&](int i) {
            float x = m_width / 2 - inner / 2 + inner * (i + 0.5f) / slots;
            return m_main->convertToNodeSpace(column->convertToWorldSpace({x, m_rewardsCentre.y}));
        };
        int slot = 0;
        if (!m_art.empty()) {
            CCRect box = rectIn(m_art.front(), m_main);
            for (auto node : m_art) {
                auto r = rectIn(node, m_main);
                float minX = std::min(box.getMinX(), r.getMinX()), minY = std::min(box.getMinY(), r.getMinY());
                float maxX = std::max(box.getMaxX(), r.getMaxX()), maxY = std::max(box.getMaxY(), r.getMaxY());
                box = {minX, minY, maxX - minX, maxY - minY};
            }
            auto target = slotAt(slot++);
            CCPoint delta {target.x - box.getMidX(), target.y - box.getMidY()};
            for (auto node : m_art) node->setPosition(node->getPosition() + delta);
        }
        // Where GD plays its star / orb / diamond effects.
        auto centre = slotAt(std::min(slot, slots - 1));
        m_layer->m_starsPosition = stars > 0 ? slotAt(slot++) : centre;
        m_layer->m_orbsPosition = orbs > 0 ? slotAt(slot++) : centre;
        m_layer->m_diamondsPosition = diamonds > 0 ? slotAt(slot++) : centre;
    }

    void ResultsCard::update(float dt) {
        if (!m_scanned) {
            // After every mod's customSetup: their buttons join ours (the
            // ones in GD's side menu stay there, next to GD's arrow).
            m_scanned = true;
            float h = BUTTON_HEIGHT * m_u;
            for (auto item : collectModButtons(m_main)) {
                if (item->getParent() == m_layer->m_sideMenu) continue;
                Ref<CCMenuItem> target = item;
                auto button = AnimatedButtonItem::create({h, h}, 10 * m_u, GRAY5, modButtonImage(item, h * 0.72f),
                                                         [target] { target->activate(); });
                m_menu->addChild(button);
                m_buttons.push_back(button);
                if (auto parent = item->getParent()) parent->setVisible(false);
            }
            layoutButtons();
        }

        // Statistics appear one after another (StatisticDisplay.Appear).
        m_ms += dt * 1000.f;
        for (size_t i = 0; i < m_statValues.size(); i++) {
            float t = (m_ms - STAT_DELAY_MS - STAT_STAGGER_MS * i) / STAT_FADE_MS;
            auto value = m_statValues[i];
            if (value->getOpacity() != 255) value->setOpacity(toByte(t));
        }

#ifdef GEODE_IS_DESKTOP
        bool blocked = gameplayPopupOnTop();
        auto mouse = geode::cocos::getMousePos();
        for (auto item : m_buttons) item->setHovered(!blocked && item->containsWorldPoint(mouse));
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
        auto card = lazer::ResultsCard::create(this);
        if (!card) return;
        // Under everything GD puts on its layer: its coins and reward effects
        // play on top of the card.
        m_mainLayer->addChild(card, -10);
        card->placeRewards();
    }
};
