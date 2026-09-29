#include "LeaderboardsOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../../integrations/ModIntegrations.hpp"
#include "../core/Text.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Green overlay scheme, the rankings overlay's.
    constexpr theme::Scheme SCHEME {125};
    constexpr float HORIZONTAL_PADDING = 50.f; // WaveOverlayContainer
    // RankingsTable: 32 px rows, 3 px apart; its text is 12 px, a touch
    // bigger here to read on a phone.
    constexpr float ROW_HEIGHT = 32, ROW_GAP = 3, ROW_TEXT = 14, HEADER_TEXT = 13;
    constexpr float RANK_WIDTH = 52, PLAYER_MIN_WIDTH = 220, STAT_WIDTH = 96, LEVEL_WIDTH = 64;
    constexpr float PLAYER_ICON = 24;

    struct TabDef { char const* label; LeaderboardType type; };
    constexpr std::array<TabDef, 4> TABS {{
        {"top 100", LeaderboardType::Top100},
        {"friends", LeaderboardType::Friends},
        {"global", LeaderboardType::Global},
        {"creators", LeaderboardType::Creator},
    }};

    struct SortDef { char const* label; char const* sprite; LeaderboardStat stat; };
    constexpr std::array<SortDef, 4> SORTS {{
        {"stars", "GJ_starsIcon_001.png", LeaderboardStat::Stars},
        {"moons", "GJ_moonsIcon_001.png", LeaderboardStat::Moons},
        {"demons", "GJ_demonIcon_001.png", LeaderboardStat::Demons},
        {"user coins", "GJ_coinsIcon2_001.png", LeaderboardStat::UserCoins},
    }};
    constexpr int REFRESH = 100; // the refresh pill's value

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

    // A player's icon in their colours (their main one, whichever kind).
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

    CCSprite* gdIcon(char const* frame, float size) {
        auto sprite = CCSprite::createWithSpriteFrameName(frame);
        if (!sprite) return nullptr;
        auto s = sprite->getContentSize();
        sprite->setScale(size / std::max(s.width, s.height));
        return sprite;
    }

    int myAccountID() { return GJAccountManager::sharedState()->m_accountID; }
}

LeaderboardsOverlay* LeaderboardsOverlay::create(float topInset) {
    auto ret = new LeaderboardsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

LeaderboardsOverlay::~LeaderboardsOverlay() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
}

bool LeaderboardsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::RANKING_STAR, "rankings", "find out how you stack up against everyone")) return false;
    m_pad = HORIZONTAL_PADDING * m_k;
    m_rowHeight = ROW_HEIGHT * m_k;
    m_rowGap = ROW_GAP * m_k;
    auto size = bodySize();

    buildTabs();
    buildPills();
    layoutColumns();
    buildHeader();

    m_scroll = ScrollArea::create({size.width, size.height - m_topHeight});
    body()->addChild(m_scroll, 1);

    m_status = makeText(" ", Weight::Regular, 18 * m_k);
    m_status->setColor(theme::rgb(m_scheme.content2()));
    m_status->setPosition({size.width / 2, (size.height - m_topHeight) / 2});
    body()->addChild(m_status, 2);
    return true;
}

// --- building ---

void LeaderboardsOverlay::buildTabs() {
    auto size = bodySize();
    float y = size.height - 40 * m_k; // baseline row of the tabs
    float x = m_pad;
    for (size_t i = 0; i < TABS.size(); i++) {
        auto holder = CCNode::create();
        auto label = makeText(TABS[i].label, Weight::SemiBold, 17 * m_k);
        label->setAnchorPoint({0, 0});
        holder->addChild(label);
        auto labelSize = label->getScaledContentSize();
        holder->setContentSize({labelSize.width, labelSize.height + 10 * m_k});
        label->setPosition({0, 10 * m_k});
        holder->setPosition({x, y - 10 * m_k});
        body()->addChild(holder, 1);
        m_tabs.push_back({holder, label, nullptr, int(i)});
        x += labelSize.width + 26 * m_k;
    }

    // Line under the whole row, and the accent bar under the selected tab (OverlayTabControl).
    auto line = CCLayerColor::create(m_scheme.background6());
    line->setContentSize({size.width - m_pad * 2, 1 * m_k});
    line->setPosition({m_pad, y - 10 * m_k});
    body()->addChild(line, 1);

    m_tabUnderline = RoundedBox::create({10, 3 * m_k}, 1.5f * m_k, m_scheme.highlight1());
    m_tabUnderline->setAnchorPoint({0, 0.5f});
    m_tabUnderline->setPositionY(y - 10 * m_k);
    body()->addChild(m_tabUnderline, 2);

    auto first = m_tabs.front().node;
    m_underlineX.set(first->getPositionX());
    m_underlineW.set(first->getContentSize().width);
    // Under the tabs: the table's caption row, then the rows.
    m_topHeight = size.height - (y - 10 * m_k) + 14 * m_k + 24 * m_k;
}

void LeaderboardsOverlay::buildPills() {
    // Right of the tabs: "sort by" and the stat pills (OverlaySortTabControl),
    // then a refresh pill.
    auto size = bodySize();
    float k = m_k;
    float y = size.height - 40 * k + 4 * k; // pills centred on the tab text
    float h = 24 * k;
    float x = size.width - m_pad;

    auto pill = [&](std::string const& text, char const* sprite, int value) {
        auto label = makeText(text, Weight::SemiBold, 13 * k);
        float w = label->getScaledContentSize().width + 22 * k;
        CCSprite* icon = sprite ? gdIcon(sprite, 14 * k) : nullptr;
        if (icon) w += 14 * k + 5 * k;
        auto bg = RoundedBox::create({w, h}, h / 2, m_scheme.background6());
        bg->setAnchorPoint({0, 0.5f});
        x -= w;
        bg->setPosition({x, y});
        body()->addChild(bg, 1);
        float tx = 11 * k;
        if (icon) {
            icon->setPosition({tx + 7 * k, h / 2});
            bg->addChild(icon);
            tx += 14 * k + 5 * k;
        }
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({tx, h / 2});
        bg->addChild(label);
        m_pills.push_back({bg, label, bg, value});
        x -= 6 * k;
    };

    pill("refresh", nullptr, REFRESH);
    x -= 14 * k;
    for (int i = int(SORTS.size()) - 1; i >= 0; i--) pill(SORTS[i].label, SORTS[i].sprite, i);

    auto caption = makeText("sort by", Weight::Regular, 13 * k);
    caption->setAnchorPoint({1, 0.5f});
    caption->setColor(theme::rgb(m_scheme.content2()));
    caption->setPosition({x - 4 * k, y});
    body()->addChild(caption, 1);
    m_sortCaption = caption;
}

void LeaderboardsOverlay::layoutColumns() {
    // What fits: rank and player always, then the stats by importance
    // (the sorted one first), then the Better Progression level. Shown in
    // their usual order.
    float k = m_k;
    float available = bodySize().width - m_pad * 2;
    float used = (RANK_WIDTH + PLAYER_MIN_WIDTH) * k;
    std::vector<Column> stats;
    auto want = [&](Column c, float w) {
        if (used + w * k > available) return;
        if (std::find(stats.begin(), stats.end(), c) != stats.end()) return;
        stats.push_back(c);
        used += w * k;
    };
    want(sortedColumn(), STAT_WIDTH);
    for (auto c : {Column::Stars, Column::Moons, Column::Demons, Column::Diamonds, Column::UserCoins, Column::Coins, Column::CreatorPoints}) want(c, STAT_WIDTH);
    if (integrations::progressionBadge(1, 10)) want(Column::Level, LEVEL_WIDTH);

    m_columns.clear();
    m_columns.push_back({Column::Rank, RANK_WIDTH});
    m_columns.push_back({Column::Player, PLAYER_MIN_WIDTH});
    for (auto c : {Column::Stars, Column::Moons, Column::Demons, Column::Diamonds, Column::Coins, Column::UserCoins, Column::CreatorPoints, Column::Level}) {
        if (std::find(stats.begin(), stats.end(), c) != stats.end()) m_columns.push_back({c, c == Column::Level ? LEVEL_WIDTH : STAT_WIDTH});
    }
    // The player column takes what's left.
    float x = m_pad;
    for (auto& col : m_columns) {
        col.x = x;
        x += col.width * k + (col.column == Column::Player ? available - used : 0);
    }
    m_playerWidth = PLAYER_MIN_WIDTH * k + (available - used);
}

void LeaderboardsOverlay::buildHeader() {
    // Column captions (RankingsTable's HeaderText): Foreground1, the sorted
    // one in full colour.
    auto size = bodySize();
    float k = m_k;
    if (m_header) m_header->removeFromParent();
    m_header = CCNode::create();
    m_header->setPosition({0, size.height - m_topHeight + 12 * k});
    body()->addChild(m_header, 1);
    m_headerLabels.clear();

    for (auto& col : m_columns) {
        char const* text = "";
        switch (col.column) {
            case Column::Rank: text = ""; break;
            case Column::Player: text = "player"; break;
            case Column::Stars: text = "stars"; break;
            case Column::Moons: text = "moons"; break;
            case Column::Demons: text = "demons"; break;
            case Column::Diamonds: text = "diamonds"; break;
            case Column::Coins: text = "coins"; break;
            case Column::UserCoins: text = "user coins"; break;
            case Column::CreatorPoints: text = "creator points"; break;
            case Column::Level: text = "level"; break;
        }
        auto label = makeText(text, Weight::Regular, HEADER_TEXT * k);
        bool left = col.column == Column::Player;
        label->setAnchorPoint({left ? 0.f : 0.5f, 0.5f});
        float w = col.column == Column::Player ? m_playerWidth : col.width * k;
        label->setPosition({left ? col.x + 10 * k : col.x + w / 2, 0});
        m_header->addChild(label);
        m_headerLabels.push_back(label);
    }
    updateHeaderColours();
}

void LeaderboardsOverlay::updateHeaderColours() {
    auto sorted = sortedColumn();
    for (size_t i = 0; i < m_columns.size() && i < m_headerLabels.size(); i++) {
        bool highlighted = m_columns[i].column == sorted;
        m_headerLabels[i]->setColor(highlighted ? theme::CONTENT1 : theme::FOREGROUND1);
    }
}

// --- data ---

std::string LeaderboardsOverlay::key() const {
    // GD's own cache key for this board.
    return fmt::format("lb_{}_{}", static_cast<int>(m_type), static_cast<int>(m_stat));
}

LeaderboardsOverlay::Column LeaderboardsOverlay::sortedColumn() const {
    if (m_type == LeaderboardType::Creator) return Column::CreatorPoints;
    switch (m_stat) {
        case LeaderboardStat::Moons: return Column::Moons;
        case LeaderboardStat::Demons: return Column::Demons;
        case LeaderboardStat::UserCoins: return Column::UserCoins;
        default: return Column::Stars;
    }
}

bool LeaderboardsOverlay::needsAccount() const {
    return m_type == LeaderboardType::Friends || m_type == LeaderboardType::Global;
}

void LeaderboardsOverlay::onOpened() {
    // GD's page forgets every board's age when it opens, so each visit is fresh.
    auto glm = GameLevelManager::sharedState();
    for (int type = 1; type <= 4; type++) {
        for (int stat = 0; stat <= 3; stat++) glm->resetTimerForKey(fmt::format("lb_{}_{}", type, stat).c_str());
    }
    m_scroll->claimWheel();
    load(true);
}

void LeaderboardsOverlay::onClosed() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
}

void LeaderboardsOverlay::selectType(LeaderboardType type) {
    if (type == m_type) return;
    m_type = type;
    for (auto& chip : m_tabs) {
        if (TABS[chip.value].type != type) continue;
        m_underlineX.to(chip.node->getPositionX(), 500, Easing::OutQuint);
        m_underlineW.to(chip.node->getContentSize().width, 500, Easing::OutQuint);
    }
    // Creators rank by creator points: the sort doesn't apply.
    bool sortable = type != LeaderboardType::Creator;
    for (auto& chip : m_pills) {
        if (chip.value != REFRESH) chip.node->setVisible(sortable);
    }
    if (m_sortCaption) m_sortCaption->setVisible(sortable);
    layoutColumns();
    buildHeader();
    load(false);
}

void LeaderboardsOverlay::selectStat(LeaderboardStat stat) {
    if (stat == m_stat) return;
    m_stat = stat;
    layoutColumns();
    buildHeader();
    load(false);
}

void LeaderboardsOverlay::load(bool refresh) {
    auto glm = GameLevelManager::sharedState();
    auto k = key();
    m_loadingKey.clear();
    if (needsAccount() && myAccountID() <= 0) {
        showScores(nullptr);
        setStatus("log in to see this");
        return;
    }
    if (refresh) {
        glm->resetTimerForKey(k.c_str());
        if (glm->m_onlineLevels) glm->m_onlineLevels->removeObjectForKey(k);
    }
    // GD keeps each board for a while: no request when it's fresh.
    if (auto cached = glm->getStoredOnlineLevels(k.c_str())) {
        showScores(cached);
        return;
    }
    showScores(nullptr);
    setStatus("loading...");
    m_loadingKey = k;
    glm->m_leaderboardManagerDelegate = this;
    glm->getLeaderboardScores(m_type, m_stat);
}

void LeaderboardsOverlay::loadLeaderboardFinished(CCArray* scores, char const* key) {
    if (!key || m_loadingKey != key) return;
    m_loadingKey.clear();
    showScores(scores);
}

void LeaderboardsOverlay::loadLeaderboardFailed(char const* key) {
    if (!key || m_loadingKey != key) return;
    m_loadingKey.clear();
    showScores(nullptr);
    setStatus(needsAccount() ? "couldn't load: are you logged in?" : "couldn't load, try refreshing");
}

void LeaderboardsOverlay::setStatus(std::string const& text) {
    m_status->setString(text.empty() ? " " : text.c_str());
}

void LeaderboardsOverlay::showScores(CCArray* scores) {
    for (auto& row : m_rows) {
        if (row.node) row.node->removeFromParent();
    }
    m_rows.clear();
    m_hoveredRow = m_pressedRow = nullptr;
    setStatus("");
    int me = myAccountID();
    int index = 0;
    for (auto score : CCArrayExt<GJUserScore*>(scores ? scores : CCArray::create())) {
        Row row;
        row.score = score;
        // The server numbers global and friends boards itself.
        row.rank = score->m_playerRank > 0 ? score->m_playerRank : index + 1;
        row.mine = me > 0 && score->m_accountID == me;
        m_rows.push_back(std::move(row));
        index++;
    }
    float rowH = m_rowHeight + m_rowGap;
    m_scroll->setContentHeight(m_rowGap + m_rows.size() * rowH + 20 * m_k);
    m_scroll->scrollTo(0, false);
    if (scores && m_rows.empty()) setStatus("nobody here yet");
    // Your own row starts in view.
    for (size_t i = 0; i < m_rows.size(); i++) {
        if (!m_rows[i].mine) continue;
        float view = m_scroll->getContentSize().height;
        float top = m_rowGap + i * rowH;
        if (top + m_rowHeight > view) m_scroll->scrollTo(top - view / 2 + m_rowHeight / 2, false);
        break;
    }
    layoutVisibleRows();
}

// --- rows ---

void LeaderboardsOverlay::layoutVisibleRows() {
    // Only rows within (or near) the view exist and draw.
    float viewTop = m_scroll->scroll();
    float viewBottom = viewTop + m_scroll->getContentSize().height;
    float margin = m_rowHeight * 4;
    float rowH = m_rowHeight + m_rowGap;
    for (size_t i = 0; i < m_rows.size(); i++) {
        auto& row = m_rows[i];
        float top = m_rowGap + i * rowH;
        bool inView = top + m_rowHeight >= viewTop - margin && top <= viewBottom + margin;
        if (!inView) {
            if (row.node) row.node->setVisible(false);
            continue;
        }
        if (!row.node) row.node = buildRow(row);
        row.node->setVisible(true);
        row.node->setPosition({0, -top - m_rowHeight});
    }
}

CCNode* LeaderboardsOverlay::buildRow(Row& row) {
    float k = m_k;
    auto s = row.score.data();
    float width = bodySize().width - m_pad * 2;
    auto node = CCNode::create();
    node->setContentSize({bodySize().width, m_rowHeight});
    m_scroll->content()->addChild(node);

    // TableRowBackground: Background4, Background3 under the mouse. Your own
    // row is in the scheme's colour.
    row.bg = RoundedBox::create({width, m_rowHeight}, 4 * k, row.mine ? m_scheme.dark3() : m_scheme.background4());
    row.bg->setAnchorPoint({0, 0});
    row.bg->setPosition({m_pad, 0});
    node->addChild(row.bg);

    auto sorted = sortedColumn();
    float cy = m_rowHeight / 2;
    for (auto& col : m_columns) {
        float w = col.column == Column::Player ? m_playerWidth : col.width * k;
        switch (col.column) {
            case Column::Rank: {
                auto label = makeText(fmt::format("#{}", row.rank), Weight::SemiBold, ROW_TEXT * k);
                label->setPosition({col.x + w / 2, cy});
                fitWidth(label, w - 8 * k);
                node->addChild(label);
                break;
            }
            case Column::Player: {
                float x = col.x + 10 * k;
                auto icon = playerIcon(s, PLAYER_ICON * k);
                icon->setPosition({x + PLAYER_ICON * k / 2, cy});
                node->addChild(icon);
                x += PLAYER_ICON * k + 10 * k;
                auto name = makeText(s->m_userName, Weight::SemiBold, ROW_TEXT * k);
                name->setAnchorPoint({0, 0.5f});
                name->setPosition({x, cy});
                if (row.mine) name->setColor(theme::rgb(m_scheme.highlight1()));
                float maxName = col.x + w - x - 30 * k;
                fitWidth(name, maxName);
                node->addChild(name);
                x += name->getScaledContentSize().width + 6 * k;
                // GD's moderator badge, as on their profile.
                if (s->m_modBadge > 0 && s->m_modBadge <= 3) {
                    if (auto badge = gdIcon(fmt::format("modBadge_0{}_001.png", s->m_modBadge).c_str(), 14 * k)) {
                        badge->setAnchorPoint({0, 0.5f});
                        badge->setPosition({x, cy});
                        node->addChild(badge);
                    }
                }
                break;
            }
            case Column::Level: {
                if (auto progress = integrations::betterProgression(s)) {
                    if (auto badge = integrations::progressionBadge(progress->level, 26 * k)) {
                        badge->setPosition({col.x + w / 2, cy});
                        node->addChild(badge);
                    }
                }
                break;
            }
            default: {
                int value = 0;
                switch (col.column) {
                    case Column::Stars: value = s->m_stars; break;
                    case Column::Moons: value = s->m_moons; break;
                    case Column::Demons: value = s->m_demons; break;
                    case Column::Diamonds: value = s->m_diamonds; break;
                    case Column::Coins: value = s->m_secretCoins; break;
                    case Column::UserCoins: value = s->m_userCoins; break;
                    case Column::CreatorPoints: value = s->m_creatorPoints; break;
                    default: break;
                }
                bool highlighted = col.column == sorted;
                auto label = makeText(withCommas(value), highlighted ? Weight::SemiBold : Weight::Regular, ROW_TEXT * k);
                label->setColor(highlighted ? theme::CONTENT1 : theme::FOREGROUND1);
                label->setPosition({col.x + w / 2, cy});
                fitWidth(label, w - 8 * k);
                node->addChild(label);
                break;
            }
        }
    }
    return node;
}

// --- input ---

void LeaderboardsOverlay::onUpdate(float dt) {
    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !m_drag.dragging();

    for (auto& chip : m_tabs) {
        bool hovered = interactive && nodeContains(chip.node, mouse);
        if (hovered && !chip.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
        chip.hovered = hovered;
        bool active = TABS[chip.value].type == m_type;
        chip.label->setColor(active ? theme::CONTENT1 : hovered ? theme::rgb(m_scheme.content2()) : theme::FOREGROUND1);
    }
    for (auto& chip : m_pills) {
        bool hovered = interactive && chip.node->isVisible() && nodeContains(chip.node, mouse);
        if (hovered && !chip.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
        chip.hovered = hovered;
        bool active = chip.value != REFRESH && SORTS[chip.value].stat == m_stat;
        auto base = active ? m_scheme.colour3() : m_scheme.background6();
        chip.bg->setFillColor(hovered && !active ? theme::lerp(base, {255, 255, 255, 255}, 0.08f) : base);
        chip.label->setColor(active ? theme::CONTENT1 : theme::rgb(m_scheme.content2()));
    }

    m_underlineX.update(dt);
    m_underlineW.update(dt);
    m_tabUnderline->setPositionX(m_underlineX.get());
    m_tabUnderline->setContentSize({m_underlineW.get(), m_tabUnderline->getContentSize().height});

    layoutVisibleRows();

    Row* hovered = nullptr;
    if (interactive && m_scroll->containsWorldPoint(mouse)) {
        for (auto& row : m_rows) {
            if (row.node && row.node->isVisible() && row.bg && nodeContains(row.bg, mouse)) { hovered = &row; break; }
        }
    }
    if (hovered != m_hoveredRow) {
        auto restore = [this](Row* r) {
            if (r && r->bg) r->bg->setFillColor(r->mine ? m_scheme.dark3() : m_scheme.background4());
        };
        restore(m_hoveredRow);
        if (hovered && hovered->bg) hovered->bg->setFillColor(hovered->mine ? m_scheme.dark4() : m_scheme.background3());
        m_hoveredRow = hovered;
    }
}

bool LeaderboardsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    auto loc = touch->getLocation();
    m_pressedChip = nullptr;
    m_pressedRow = nullptr;
    for (auto list : {&m_tabs, &m_pills}) {
        for (auto& chip : *list) {
            if (chip.node->isVisible() && nodeContains(chip.node, loc)) m_pressedChip = &chip;
        }
    }
    if (m_pressedChip) return true;
    if (m_scroll->containsWorldPoint(loc)) {
        for (auto& row : m_rows) {
            if (row.node && row.node->isVisible() && row.bg && nodeContains(row.bg, loc)) { m_pressedRow = &row; break; }
        }
    }
    m_drag.began(m_scroll, loc);
    return true;
}

void LeaderboardsOverlay::ccTouchMoved(CCTouch* touch, CCEvent* e) {
    if (m_pressedChip) return;
    if (m_drag.moved(touch->getLocation())) {
        m_pressedRow = nullptr;
        cancelPress();
    }
}

void LeaderboardsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    bool dragged = m_drag.ended();
    auto loc = touch->getLocation();
    auto chip = m_pressedChip;
    auto row = m_pressedRow;
    m_pressedChip = nullptr;
    m_pressedRow = nullptr;

    if (chip) {
        if (!nodeContains(chip->node, loc)) return;
        sfx::click(sfx::sound::DEFAULT_SELECT);
        bool isTab = chip >= m_tabs.data() && chip < m_tabs.data() + m_tabs.size();
        if (isTab) selectType(TABS[chip->value].type);
        else if (chip->value == REFRESH) load(true);
        else selectStat(SORTS[chip->value].stat);
        return;
    }
    if (dragged || !row || !row->bg || !nodeContains(row->bg, loc)) return;
    // Their profile (GD's page, shown as ours).
    auto score = row->score.data();
    if (!score || score->m_accountID <= 0) return;
    sfx::click(sfx::sound::DEFAULT_SELECT);
    ProfilePage::create(score->m_accountID, score->m_accountID == myAccountID())->show();
}

} // namespace lazer
