#include "PathsOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Text.hpp"
#include "Dialog.hpp"
#include "SettingsRows.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Orange overlay scheme: paths are about treasure.
    constexpr theme::Scheme SCHEME {45};
    constexpr float HORIZONTAL_PADDING = 50.f; // WaveOverlayContainer
    constexpr float LIST_WIDTH = 300, LIST_GAP = 30;
    constexpr float ROW_HEIGHT = 58, ROW_GAP = 6;
    constexpr float CARD_W = 84, CARD_H = 104, CARD_GAP = 8;
    constexpr int PATH_ITEM = 12;               // UnlockType::GJItem: a path is one of GD's "items"
    constexpr int PATH_STAT_BASE = 29;          // StatKey::FirePath is 30: path 1

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    void fitWidth(CCLabelBMFont* label, float maxWidth) {
        float w = label->getScaledContentSize().width;
        if (w > maxWidth && w > 0) label->setScale(label->getScale() * maxWidth / w);
    }

    std::string withCommas(long long v) {
        auto s = fmt::format("{}", v);
        for (int i = int(s.size()) - 3; i > (s[0] == '-' ? 1 : 0); i -= 3) s.insert(size_t(i), ",");
        return s;
    }

    std::string lower(std::string s) {
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    CCSprite* pathArt(int path, float size) {
        auto sprite = CCSprite::createWithSpriteFrameName(fmt::format("pathIcon_{:02}_001.png", path).c_str());
        if (!sprite) return nullptr;
        auto s = sprite->getContentSize();
        sprite->setScale(size / std::max(s.width, s.height));
        return sprite;
    }

    // What paying for a path takes: GD's store items pay in mana orbs, the
    // diamond shop's in diamond shards.
    char const* currencyName(std::string const& key) { return key == "29" ? "diamond shards" : "mana orbs"; }

    // What kind of thing an unlock is, as GD's item info calls it.
    char const* unlockName(UnlockType type) {
        switch (type) {
            case UnlockType::Cube: return "cube";
            case UnlockType::Col1: return "main color";
            case UnlockType::Col2: return "secondary color";
            case UnlockType::Ship: return "ship";
            case UnlockType::Ball: return "ball";
            case UnlockType::Bird: return "ufo";
            case UnlockType::Dart: return "wave";
            case UnlockType::Robot: return "robot";
            case UnlockType::Spider: return "spider";
            case UnlockType::Streak: return "trail";
            case UnlockType::Death: return "death effect";
            case UnlockType::Swing: return "swing";
            case UnlockType::Jetpack: return "jetpack";
            case UnlockType::ShipFire: return "ship fire";
            default: return "item";
        }
    }

    std::string dictString(CCDictionary* dict, char const* key) {
        if (!dict) return "";
        auto value = dict->valueForKey(key);
        return value ? value->getCString() : "";
    }
}

PathsOverlay* PathsOverlay::create(float topInset) {
    auto ret = new PathsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PathsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::ROUTE, "paths", "the orbs you collect go into a path, and each rank of it unlocks something")) return false;
    // GD's path art lives in its own sheet, loaded when its page opens.
    CCSpriteFrameCache::get()->addSpriteFramesWithFile("GJ_PathSheet.plist");
    m_pad = HORIZONTAL_PADDING * m_k;
    auto size = bodySize();
    m_listWidth = std::min(LIST_WIDTH * m_k, size.width * 0.32f);
    m_detailX = m_pad + m_listWidth + LIST_GAP * m_k;
    m_detailWidth = size.width - m_pad - m_detailX;

    buildList();
    buildDetail();

    m_tooltip = CCNode::create();
    m_tooltip->setVisible(false);
    body()->addChild(m_tooltip, 10);

    refresh(true);
    return true;
}

// --- GD's numbers ---

int PathsOverlay::points(int path) const {
    int value = GameStatsManager::sharedState()->getStat(fmt::format("{}", PATH_STAT_BASE + path).c_str());
    return std::clamp(value, 0, RANKS * POINTS_PER_RANK);
}

bool PathsOverlay::unlocked(int path) const {
    return GameStatsManager::sharedState()->isItemUnlocked(static_cast<UnlockType>(PATH_ITEM), path);
}

bool PathsOverlay::active(int path) const {
    return GameStatsManager::sharedState()->m_activePath == PATH_STAT_BASE + path;
}

bool PathsOverlay::chestClaimed(int path) const {
    return GameStatsManager::sharedState()->isPathChestUnlocked(path);
}

GJStoreItem* PathsOverlay::storeItem(int path) const {
    return GameStatsManager::sharedState()->getStoreItem(path, PATH_ITEM);
}

std::string PathsOverlay::pathName(int path) const {
    std::string name = lower(GJPathsLayer::nameForPath(path));
    if (name.empty()) return fmt::format("path {}", path);
    return name.find("path") == std::string::npos ? "path of " + name : name;
}

std::string PathsOverlay::signature() const {
    auto gsm = GameStatsManager::sharedState();
    std::string s = fmt::format("{}|{}|{}|", gsm->m_activePath, gsm->getStat("14"), gsm->getStat("29"));
    for (int p = 1; p <= PATHS; p++) s += fmt::format("{}:{}:{}:{},", points(p), unlocked(p), chestClaimed(p), p == m_selected);
    return s;
}

// --- building ---

void PathsOverlay::buildList() {
    float k = m_k;
    auto size = bodySize();
    float h = ROW_HEIGHT * k, w = m_listWidth;
    // The rows scroll inside the body's height (a phone's is short).
    float margin = 8 * k;
    float listTop = size.height - 24 * k;
    m_list = ScrollArea::create({w + margin * 2, listTop - 16 * k});
    m_list->setPosition({m_pad - margin, 16 * k});
    body()->addChild(m_list, 1);
    float y = 0; // from the top of the content, downwards
    for (int p = 1; p <= PATHS; p++) {
        Row row;
        row.path = p;
        row.node = CCNode::create();
        row.node->setContentSize({w, h});
        row.node->setPosition({margin, -(y + h)});
        m_list->content()->addChild(row.node, 1);

        row.bg = RoundedBox::create({w, h}, 8 * k, m_scheme.background4());
        row.bg->setAnchorPoint({0, 0});
        row.node->addChild(row.bg);
        row.accent = RoundedBox::create({4 * k, h - 16 * k}, 2 * k, m_scheme.highlight1());
        row.accent->setAnchorPoint({0, 0.5f});
        row.accent->setPosition({6 * k, h / 2});
        row.node->addChild(row.accent, 1);

        row.icon = pathArt(p, 40 * k);
        if (row.icon) {
            row.icon->setPosition({18 * k + 20 * k, h / 2});
            row.node->addChild(row.icon, 1);
        }
        float textX = 66 * k;
        row.name = makeText(pathName(p), Weight::SemiBold, 16 * k);
        row.name->setAnchorPoint({0, 0.5f});
        row.name->setPosition({textX, h - 18 * k});
        fitWidth(row.name, w - textX - 64 * k);
        row.node->addChild(row.name, 1);
        row.sub = makeText(" ", Weight::Regular, 12 * k);
        row.sub->setAnchorPoint({0, 0.5f});
        row.sub->setColor(theme::rgb(m_scheme.content2()));
        row.sub->setPosition({textX, h - 36 * k});
        row.node->addChild(row.sub, 1);

        // A thin bar of its progress along the bottom of the text.
        row.barWidth = w - textX - 14 * k;
        auto track = RoundedBox::create({row.barWidth, 4 * k}, 2 * k, m_scheme.background6());
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({textX, 10 * k});
        row.node->addChild(track, 1);
        row.barFill = RoundedBox::create({4 * k, 4 * k}, 2 * k, m_scheme.highlight1());
        row.barFill->setAnchorPoint({0, 0.5f});
        row.barFill->setPosition({textX, 10 * k});
        row.node->addChild(row.barFill, 2);

        // "active" in the corner of the path collecting your orbs.
        auto tagText = makeText("active", Weight::Bold, 10 * k);
        auto tagSize = tagText->getScaledContentSize();
        row.activeTag = RoundedBox::create({tagSize.width + 12 * k, 16 * k}, 8 * k, m_scheme.colour3());
        row.activeTag->setAnchorPoint({1, 0.5f});
        row.activeTag->setPosition({w - 10 * k, h - 18 * k});
        tagText->setPosition({(tagSize.width + 12 * k) / 2, 8 * k});
        row.activeTag->addChild(tagText);
        row.node->addChild(row.activeTag, 2);

        m_rows.push_back(row);
        y += h + ROW_GAP * k;
    }
    m_list->setContentHeight(y - ROW_GAP * k + 12 * k);
}

void PathsOverlay::buildDetail() {
    float k = m_k;
    auto size = bodySize();
    m_detail = CCNode::create();
    m_detail->setPosition({m_detailX, 0});
    body()->addChild(m_detail, 1);
    float W = m_detailWidth;
    float top = size.height - 24 * k;

    // Its art, name and where you are on it.
    float art = 96 * k;
    m_bigIcon = CCSprite::create();
    m_bigIcon->setPosition({art / 2, top - art / 2});
    m_detail->addChild(m_bigIcon, 1);
    float textX = art + 20 * k;
    m_title = makeText(" ", Weight::Bold, 32 * k);
    m_title->setAnchorPoint({0, 0.5f});
    m_title->setPosition({textX, top - 24 * k});
    m_detail->addChild(m_title, 1);
    m_status = makeText(" ", Weight::Regular, 16 * k);
    m_status->setAnchorPoint({0, 0.5f});
    m_status->setColor(theme::rgb(m_scheme.content2()));
    m_status->setPosition({textX, top - 54 * k});
    m_detail->addChild(m_status, 1);
    m_pointsLabel = makeText(" ", Weight::SemiBold, 15 * k);
    m_pointsLabel->setAnchorPoint({1, 0.5f});
    m_pointsLabel->setPosition({W, top - 54 * k});
    m_detail->addChild(m_pointsLabel, 1);

    // Ten segments, one per rank, filling left to right.
    float barY = top - art - 16 * k;
    float gap = 4 * k;
    m_segmentWidth = (W - gap * (RANKS - 1)) / RANKS;
    for (int i = 0; i < RANKS; i++) {
        float x = i * (m_segmentWidth + gap);
        auto track = RoundedBox::create({m_segmentWidth, 10 * k}, 5 * k, m_scheme.background6());
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({x, barY});
        m_detail->addChild(track, 1);
        auto fill = RoundedBox::create({10 * k, 10 * k}, 5 * k, m_scheme.highlight1());
        fill->setAnchorPoint({0, 0.5f});
        fill->setPosition({x, barY});
        fill->setVisible(false);
        m_detail->addChild(fill, 2);
        m_segments.push_back(track);
        m_segmentFills.push_back(fill);
    }

    // The one thing to do about it, and a word on what that means.
    m_actionY = barY - 24 * k - 40 * k;
    m_hint = makeText(" ", Weight::Regular, 13 * k);
    m_hint->setAnchorPoint({0, 0.5f});
    m_hint->setColor(theme::rgb(m_scheme.content2()));
    m_hint->setPosition({250 * k, m_actionY + 20 * k});
    m_detail->addChild(m_hint, 1);

    auto caption = makeText("rewards", Weight::SemiBold, 15 * k);
    caption->setAnchorPoint({0, 0.5f});
    caption->setPosition({0, m_actionY - 28 * k});
    m_detail->addChild(caption, 1);
    m_cardsTop = m_actionY - 44 * k;
}

void PathsOverlay::rebuildCards() {
    for (auto& card : m_cards) card.node->removeFromParent();
    m_cards.clear();
    m_tooltipCard = nullptr;
    m_tooltip->setVisible(false);
    auto am = AchievementManager::sharedState();

    float k = m_k;
    int path = m_selected;
    int reached = rank(path);
    bool open = unlocked(path);
    float cw = CARD_W * k, ch = CARD_H * k, gap = CARD_GAP * k;
    int columns = std::max(1, int((m_detailWidth + gap) / (cw + gap)));
    // Every reward, then the chest at the end of the path.
    int count = RANKS + 2;
    for (int i = 0; i < count; i++) {
        Card card;
        card.rank = i;
        bool chest = i == RANKS + 1;
        bool got = chest ? chestClaimed(path) : (i == 0 ? open : reached >= i);
        bool next = !got && (chest ? reached >= RANKS : (i == 0 ? true : open && reached == i - 1));

        card.node = CCNode::create();
        card.node->setContentSize({cw, ch});
        int col = i % columns, rowIndex = i / columns;
        card.node->setPosition({col * (cw + gap), m_cardsTop - ch - rowIndex * (ch + gap)});
        m_detail->addChild(card.node, 1);
        card.bg = RoundedBox::create({cw, ch}, 8 * k, got ? m_scheme.background3() : m_scheme.background5());
        card.bg->setAnchorPoint({0, 0});
        if (next) card.bg->setBorder(2 * k, m_scheme.highlight1());
        card.node->addChild(card.bg);

        CCNode* art = nullptr;
        if (chest) {
            auto sprite = CCSprite::createWithSpriteFrameName(got ? "chest_02_04_001.png" : "chest_02_02_001.png");
            if (sprite) {
                auto s = sprite->getContentSize();
                sprite->setScale(52 * k / std::max(s.width, s.height));
                art = sprite;
            }
            card.title = "the path's chest";
            card.description = got ? "claimed" : "waiting at rank 10";
        } else {
            int id = 0;
            UnlockType type = UnlockType::Cube;
            auto key = fmt::format("geometry.ach.path{:02}.{:02}", path, i);
            GameManager::get()->getUnlockForAchievement(key, id, type);
            // GD's achievement for the rank names the reward and says what it takes.
            auto ach = am->getAchievementsWithID(key.c_str());
            std::string title = dictString(ach, "title");
            card.description = dictString(ach, got ? "achievedDescription" : "unachievedDescription");
            if (id > 0) {
                card.itemID = id;
                card.itemType = type;
                card.title = fmt::format("{} {}", unlockName(type), id);
                if (!title.empty()) card.title += ": " + title;
                if (auto icon = GJItemIcon::createBrowserItem(type, id)) {
                    auto s = icon->getContentSize();
                    icon->setScale(std::min(52 * k / std::max(s.width, s.height), icon->getScale() * 1.6f));
                    art = icon;
                }
            } else {
                card.title = title;
            }
        }
        if (art) {
            art->setPosition({cw / 2, ch - 12 * k - 26 * k});
            if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(art)) rgba->setOpacity(got || next ? 255 : 90);
            card.node->addChild(art, 1);
        }
        if (!got) {
            auto lock = makeIcon(icon::LOCK, 11 * k);
            lock->setColor(next ? theme::rgb(m_scheme.highlight1()) : theme::FOREGROUND1);
            lock->setPosition({cw - 12 * k, ch - 12 * k});
            card.node->addChild(lock, 2);
        }
        std::string caption = chest ? "chest" : i == 0 ? "unlock" : fmt::format("rank {}", i);
        auto label = makeText(caption, Weight::SemiBold, 12 * k);
        label->setPosition({cw / 2, 26 * k});
        label->setColor(got || next ? theme::CONTENT1 : theme::FOREGROUND1);
        card.node->addChild(label, 2);
        std::string need = chest ? "rank 10" : i == 0 ? "" : fmt::format("{} orbs", withCommas(i * POINTS_PER_RANK));
        auto needLabel = makeText(need.empty() ? " " : need, Weight::Regular, 10 * k);
        needLabel->setPosition({cw / 2, 12 * k});
        needLabel->setColor(theme::FOREGROUND1);
        card.node->addChild(needLabel, 2);
        m_cards.push_back(card);
    }
}

// --- state ---

void PathsOverlay::refresh(bool force) {
    auto sig = signature();
    if (!force && sig == m_signature) return;
    m_signature = sig;
    for (auto& row : m_rows) refreshRow(row);
    refreshDetail();
    rebuildCards();
}

void PathsOverlay::refreshRow(Row& row) {
    int p = row.path;
    bool open = unlocked(p), isActive = active(p);
    int pts = points(p), r = rank(p);
    bool selected = p == m_selected;
    std::string sub;
    if (!open) sub = "locked";
    else if (r >= RANKS) sub = chestClaimed(p) ? "complete" : "complete: a chest is waiting";
    else sub = fmt::format("rank {} of {}  ·  {} / {}", r, RANKS, withCommas(pts), withCommas(RANKS * POINTS_PER_RANK));
    row.sub->setString(sub.c_str());
    row.sub->setColor(!open ? theme::FOREGROUND1 : r >= RANKS && !chestClaimed(p) ? theme::rgb(m_scheme.highlight1()) : theme::rgb(m_scheme.content2()));
    row.name->setColor(open ? theme::CONTENT1 : theme::rgb(m_scheme.content2()));
    if (row.icon) row.icon->setOpacity(open ? 255 : 110);
    float fraction = float(pts) / (RANKS * POINTS_PER_RANK);
    row.barFill->setVisible(open && fraction > 0);
    row.barFill->setContentSize({std::max(4 * m_k, row.barWidth * fraction), 4 * m_k});
    row.activeTag->setVisible(isActive);
    row.accent->setVisible(selected);
    row.bg->setFillColor(selected ? m_scheme.background3() : row.hovered ? m_scheme.dark4() : m_scheme.background4());
}

void PathsOverlay::refreshDetail() {
    float k = m_k;
    int p = m_selected;
    bool open = unlocked(p), isActive = active(p);
    int pts = points(p), r = rank(p);
    bool complete = r >= RANKS, claimed = chestClaimed(p);

    if (auto frame = CCSpriteFrameCache::get()->spriteFrameByName(fmt::format("pathIcon_{:02}_001.png", p).c_str())) {
        m_bigIcon->setDisplayFrame(frame);
        auto s = m_bigIcon->getContentSize();
        m_bigIcon->setScale(96 * k / std::max(s.width, s.height));
        m_bigIcon->setOpacity(open ? 255 : 120);
    }
    m_title->setString(pathName(p).c_str());
    fitWidth(m_title, m_detailWidth - 96 * k - 20 * k - 160 * k);

    std::string status, hint;
    auto item = storeItem(p);
    std::string currency = item ? item->getCurrencyKey() : "14";
    if (!open) {
        status = item ? fmt::format("locked  ·  {} {} to unlock", withCommas(item->m_price.value()), currencyName(currency)) : "locked";
        hint = fmt::format("you have {} {}", withCommas(GameStatsManager::sharedState()->getStat(currency.c_str())), currencyName(currency));
    } else if (complete) {
        status = claimed ? "complete, chest claimed" : "complete: claim your chest";
        hint = "every rank of this path is yours";
    } else if (isActive) {
        status = fmt::format("active  ·  rank {} of {}", r, RANKS);
        hint = "orbs you collect in levels go into this path";
    } else {
        status = fmt::format("rank {} of {}", r, RANKS);
        hint = "make it active and the orbs you collect count towards it";
    }
    m_status->setString(status.c_str());
    m_hint->setString(hint.c_str());
    fitWidth(m_hint, m_detailWidth - 250 * k);
    m_pointsLabel->setString(open ? fmt::format("{} / {} orbs", withCommas(pts), withCommas(RANKS * POINTS_PER_RANK)).c_str() : " ");
    m_pointsLabel->setColor(isActive ? theme::rgb(m_scheme.highlight1()) : theme::CONTENT1);

    for (int i = 0; i < RANKS; i++) {
        float part = std::clamp(float(pts - i * POINTS_PER_RANK) / POINTS_PER_RANK, 0.f, 1.f);
        m_segmentFills[i]->setVisible(open && part > 0);
        m_segmentFills[i]->setContentSize({std::max(10 * k, m_segmentWidth * part), 10 * k});
        m_segmentFills[i]->setFillColor(isActive ? m_scheme.highlight1() : m_scheme.light4());
    }

    // The action: built afresh, since its label changes.
    if (m_action) {
        removeInteractive(m_action);
        m_action->removeFromParent();
        m_action = nullptr;
    }
    std::string label;
    std::function<void()> action;
    bool enabled = true;
    if (!open) {
        label = item ? fmt::format("unlock  ·  {} {}", withCommas(item->m_price.value()), currencyName(currency)) : "unlock";
        action = [this] { this->unlock(); };
        enabled = item != nullptr;
    } else if (complete) {
        label = claimed ? "chest claimed" : "claim the chest";
        action = [this] { this->claim(); };
        enabled = !claimed;
    } else if (isActive) {
        label = "active";
        enabled = false;
    } else {
        label = "make it active";
        action = [this] { this->activate(); };
    }
    m_action = ButtonRow::create(label, 230 * k, k, action);
    m_action->setColor(enabled ? m_scheme.colour3() : m_scheme.background6());
    m_action->setEnabled(enabled);
    m_action->setPosition({0, m_actionY});
    m_detail->addChild(m_action, 2);
    addInteractive(m_action);
}

void PathsOverlay::select(int path) {
    if (path == m_selected) return;
    m_selected = path;
    m_iconPop.set(0.85f);
    m_iconPop.to(1, 500, Easing::OutElasticHalf);
    refresh(true);
}

void PathsOverlay::onOpened() {
    // GD picks the active path the same way when its page opens (a finished
    // one hands over to the next unlocked one).
    GameStatsManager::sharedState()->trySelectActivePath();
    m_openedAt = m_time;
    refresh(true);
}

// --- actions ---

void PathsOverlay::unlock() {
    auto item = storeItem(m_selected);
    if (!item || unlocked(m_selected)) return;
    auto gsm = GameStatsManager::sharedState();
    std::string currency = item->getCurrencyKey();
    int have = gsm->getStat(currency.c_str()), price = item->m_price.value();
    if (have < price) {
        if (!Dialog::isOpen()) {
            Dialog::show(icon::LOCK, fmt::format("you need {} {} for the {}", withCommas(price), currencyName(currency), pathName(m_selected)),
                         fmt::format("you have {} so far", withCommas(have)), {{"ok", Dialog::Kind::Cancel, nullptr}});
        }
        return;
    }
    // GD's own purchase popup: it asks, pays and saves, then tells us.
    if (auto popup = PurchaseItemPopup::create(item)) {
        popup->m_delegate = this;
        popup->show();
    }
}

void PathsOverlay::didPurchaseItem(GJStoreItem* item) {
    refresh(true);
    if (unlocked(m_selected)) playUnlocked();
}

void PathsOverlay::activate() {
    if (!unlocked(m_selected) || rank(m_selected) >= RANKS) return;
    GameStatsManager::sharedState()->updateActivePath(static_cast<StatKey>(PATH_STAT_BASE + m_selected));
    sfx::play(sfx::sound::CHECK_ON);
    refresh(true);
}

void PathsOverlay::claim() {
    if (rank(m_selected) < RANKS || chestClaimed(m_selected)) return;
    // GD's reward popup: it hands the chest over and plays its unlocking.
    if (auto popup = GJPathRewardPopup::create(m_selected)) popup->show();
}

void PathsOverlay::playUnlocked() {
    FMODAudioEngine::sharedEngine()->playEffect(std::string("unlockPath.ogg"));
    m_iconPop.set(0.6f);
    m_iconPop.to(1, 800, Easing::OutElastic);
}

// --- input ---

void PathsOverlay::onUpdate(float dt) {
    m_time += dt;
    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !Dialog::isOpen();

    m_iconPop.update(dt);
    if (m_bigIcon) {
        auto s = m_bigIcon->getContentSize();
        if (s.width > 0) m_bigIcon->setScale(96 * m_k / std::max(s.width, s.height) * m_iconPop.get());
    }

    bool overList = m_list->containsWorldPoint(mouse) && !m_drag.dragging();
    for (auto& row : m_rows) {
        bool hovered = interactive && overList && nodeContains(row.node, mouse);
        if (hovered != row.hovered) {
            if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            row.hovered = hovered;
            bool selected = row.path == m_selected;
            row.bg->setFillColor(selected ? m_scheme.background3() : hovered ? m_scheme.dark4() : m_scheme.background4());
        }
    }
    Card* hoveredCard = nullptr;
    for (auto& card : m_cards) {
        bool hovered = interactive && nodeContains(card.node, mouse);
        if (hovered) hoveredCard = &card;
        if (hovered != card.hovered) {
            card.hovered = hovered;
            bool got = card.rank == RANKS + 1 ? chestClaimed(m_selected) : card.rank == 0 ? unlocked(m_selected) : rank(m_selected) >= card.rank;
            auto base = got ? m_scheme.background3() : m_scheme.background5();
            card.bg->setFillColor(hovered ? theme::lerp(base, {255, 255, 255, 255}, 0.08f) : base);
        }
    }
    updateTooltip(hoveredCard);

    // GD's popups (purchase, reward) change things underneath: keep up.
    m_sinceCheck += dt;
    if (m_sinceCheck >= 0.25f) {
        m_sinceCheck = 0;
        if (isOpen()) refresh(false);
    }
}

void PathsOverlay::updateTooltip(Card* hovered) {
    if (!hovered || hovered->title.empty()) {
        m_tooltipCard = nullptr;
        m_tooltip->setVisible(false);
        return;
    }
    float k = m_k;
    if (hovered != m_tooltipCard) {
        m_tooltipCard = hovered;
        m_tooltip->removeAllChildren();
        // What it is on top, what it takes under it, smaller (the settings' tooltips).
        float pad = 8 * k, maxW = 300 * k;
        auto title = makeWrappedText(hovered->title, 15 * k, maxW, theme::CONTENT1);
        auto titleSize = title->getContentSize();
        CCNode* body = nullptr;
        CCSize bodySize {0, 0};
        if (!hovered->description.empty()) {
            body = makeWrappedText(hovered->description, 12 * k, maxW, theme::rgb(m_scheme.content2()));
            bodySize = body->getContentSize();
        }
        float w = std::max(titleSize.width, bodySize.width) + pad * 2;
        float h = titleSize.height + (body ? bodySize.height + 3 * k : 0) + pad * 2;
        auto box = RoundedBox::create({w, h}, 5 * k, theme::BACKGROUND6);
        box->setAnchorPoint({0, 1});
        box->setShadow(6 * k, {0, 0, 0, 90});
        title->setPosition({pad, h - pad});
        box->addChild(title);
        if (body) {
            body->setPosition({pad, h - pad - titleSize.height - 3 * k});
            box->addChild(body);
        }
        m_tooltip->addChild(box);
        m_tooltip->setContentSize({w, h});
    }
    // Beside the mouse, kept on screen. At once: no delay.
    auto mouse = body()->convertToNodeSpace(geode::cocos::getMousePos());
    auto size = bodySize();
    auto tip = m_tooltip->getContentSize();
    float x = std::min(mouse.x + 12 * k, size.width - tip.width - 4 * k);
    float y = std::max(mouse.y - 12 * k, tip.height + 4 * k);
    m_tooltip->setPosition({x, y});
    m_tooltip->setVisible(true);
}

bool PathsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    auto loc = touch->getLocation();
    m_pressedRow = nullptr;
    if (m_list->containsWorldPoint(loc)) {
        for (auto& row : m_rows) {
            if (nodeContains(row.node, loc)) m_pressedRow = &row;
        }
        m_drag.began(m_list, loc);
    }
    return true;
}

void PathsOverlay::ccTouchMoved(CCTouch* touch, CCEvent* e) {
    // A drag on the list scrolls it, and isn't a pick.
    if (m_drag.moved(touch->getLocation())) {
        m_pressedRow = nullptr;
        cancelPress();
    }
}

void PathsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    bool dragged = m_drag.ended();
    auto loc = touch->getLocation();
    auto row = m_pressedRow;
    m_pressedRow = nullptr;
    if (dragged || !row || !nodeContains(row->node, loc)) return;
    sfx::click(sfx::sound::DEFAULT_SELECT);
    select(row->path);
}

} // namespace lazer
