#include "FriendsOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Blue overlay scheme, the social overlay's.
    constexpr theme::Scheme SCHEME {190};
    constexpr float HORIZONTAL_PADDING = 50.f; // WaveOverlayContainer
    constexpr int COLUMNS = 2;
    // UserGridPanel: a wide card, the avatar on a tile at the left, the name
    // above the stats; 10 px between cards.
    constexpr float CARD_HEIGHT = 66, CARD_GAP = 10, CARD_RADIUS = 8;
    constexpr float AVATAR_TILE = 48, AVATAR = 34;
    constexpr float NAME_TEXT = 17, STAT_TEXT = 13, STAT_ICON = 14;
    constexpr float BUTTON_WIDTH = 130, PAGER_WIDTH = 120;
    constexpr int PAGE_SIZE = 10; // GD's user search pages

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

    CCSprite* gdIcon(char const* frame, float size) {
        auto sprite = CCSprite::createWithSpriteFrameName(frame);
        if (!sprite) return nullptr;
        auto s = sprite->getContentSize();
        sprite->setScale(size / std::max(s.width, s.height));
        return sprite;
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

    int myAccountID() { return GJAccountManager::sharedState()->m_accountID; }

    std::string trimmed(std::string const& s) {
        auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
    }
}

FriendsOverlay* FriendsOverlay::create(float topInset) {
    auto ret = new FriendsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

FriendsOverlay::~FriendsOverlay() { detach(); }

bool FriendsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::USER_PLUS, "friends",
                           "find your people: search for a player or browse your friends")) return false;
    float k = m_k;
    auto size = bodySize();
    m_pad = HORIZONTAL_PADDING * k;
    float width = size.width - 2 * m_pad;
    m_gap = CARD_GAP * k;
    m_cardWidth = (width - (COLUMNS - 1) * m_gap) / COLUMNS;
    m_cardHeight = CARD_HEIGHT * k;

    // The top row: the search box, then the two buttons.
    float rowY = size.height - 28 * k; // its centre
    float buttonW = BUTTON_WIDTH * k;
    float inputW = width - 2 * (buttonW + 12 * k);
    m_search = TextInput::create(inputW / k, "player name or user ID", "outfit-regular.fnt"_spr);
    m_search->setCommonFilter(CommonFilter::Any);
    m_search->setMaxCharCount(40);
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(k);
    m_search->setAnchorPoint({0, 0.5f});
    m_search->setPosition({m_pad, rowY});
    m_search->setDelegate(this);
    body()->addChild(m_search);

    auto button = [&](std::string const& label, float x, std::function<void()> action) {
        auto row = ButtonRow::create(label, buttonW, k, std::move(action));
        row->setPosition({x, rowY - row->getContentSize().height / 2});
        body()->addChild(row);
        addInteractive(row);
        return row;
    };
    button("search", size.width - m_pad - 2 * buttonW - 12 * k, [this] { search(); });
    button("my friends", size.width - m_pad - buttonW, [this] { showFriends(); });

    m_status = makeText("", Weight::Regular, 14 * k);
    m_status->setColor(theme::FOREGROUND1);
    m_status->setPosition({size.width / 2, size.height - 68 * k});
    m_statusScale = m_status->getScale();
    body()->addChild(m_status);

    // The cards, with room under them for the search's previous / next.
    float footer = 52 * k;
    float top = size.height - 84 * k;
    m_scroll = ScrollArea::create({width, std::max(40 * k, top - footer)});
    m_scroll->setPosition({m_pad, footer});
    body()->addChild(m_scroll);

    float pagerW = PAGER_WIDTH * k;
    m_previous = ButtonRow::create("previous", pagerW, k, [this] {
        if (!m_loading && m_page > 0) { --m_page; load(); }
    });
    m_previous->setPosition({m_pad, 5 * k});
    m_next = ButtonRow::create("next", pagerW, k, [this] {
        if (!m_loading) { ++m_page; load(); }
    });
    m_next->setPosition({size.width - m_pad - pagerW, 5 * k});
    for (auto pager : {m_previous, m_next}) {
        pager->setVisible(false);
        body()->addChild(pager);
        addInteractive(pager);
    }

    setStatus("search for a player by name or user ID, or browse your friends");
    return true;
}

void FriendsOverlay::onOpened() {
    m_scroll->claimWheel();
    // Your friends are up first, as in osu!'s social overlay.
    if (!m_shownFriends && myAccountID() > 0) {
        m_shownFriends = true;
        showFriends();
    }
}

void FriendsOverlay::onClosed() {
    m_search->defocus();
    detach();
}

void FriendsOverlay::detach() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
    if (glm->m_userListDelegate == this) glm->m_userListDelegate = nullptr;
    m_loading = false;
}

void FriendsOverlay::setStatus(std::string const& text) {
    m_status->setString(text.c_str());
    m_status->setScale(m_statusScale);
    fitWidth(m_status, bodySize().width - 2 * m_pad);
}

void FriendsOverlay::search() {
    if (m_loading) return;
    auto query = trimmed(m_search->getString());
    if (query.empty()) {
        setStatus("type a player's name or user ID first");
        return;
    }
    m_search->defocus();
    m_query = query;
    m_mode = Mode::Search;
    m_page = 0;
    load();
}

void FriendsOverlay::showFriends() {
    if (m_loading) return;
    m_search->defocus();
    m_mode = Mode::Friends;
    m_page = 0;
    load();
}

void FriendsOverlay::textInputOpened(CCTextInputNode*) { m_typing = true; }
void FriendsOverlay::textInputClosed(CCTextInputNode*) { m_typing = false; }

// GD's text inputs don't report Enter (enterPressed never fires): the menu
// hands its keys here instead. Kept in case a build does.
void FriendsOverlay::enterPressed(CCTextInputNode*) { search(); }

bool FriendsOverlay::handleKey(enumKeyCodes key) {
    if (!isOpen() || !m_typing) return false;
    if (key != KEY_Enter && key != KEY_NumEnter) return false;
    search();
    return true;
}

void FriendsOverlay::load() {
    auto glm = GameLevelManager::sharedState();
    if (m_mode == Mode::Friends && myAccountID() <= 0) {
        clearCards();
        setStatus("sign in to your GD account to see your friends");
        return;
    }
    // GD has one delegate slot for each: a page of ours mid-request keeps it.
    bool busy = m_mode == Mode::Friends ? glm->m_userListDelegate && glm->m_userListDelegate != this
                                        : glm->m_levelManagerDelegate && glm->m_levelManagerDelegate != this;
    if (busy) {
        setStatus("another page is still loading, try again in a moment");
        return;
    }
    clearCards();
    m_loading = true;
    m_previous->setVisible(false);
    m_next->setVisible(false);
    if (m_mode == Mode::Friends) {
        setStatus("loading your friends...");
        glm->m_userListDelegate = this;
        glm->getUserList(UserListType::Friends);
    } else {
        setStatus(fmt::format("searching for \"{}\"...", m_query));
        auto request = GJSearchObject::create(SearchType::Users, m_query);
        request->m_page = m_page;
        m_key = request->getKey();
        glm->m_levelManagerDelegate = this;
        glm->getUsers(request);
    }
}

void FriendsOverlay::showUsers(CCArray* users) {
    detach();
    clearCards();
    int me = myAccountID();
    if (users) {
        for (auto score : CCArrayExt<GJUserScore*>(users)) {
            if (!score) continue;
            m_cards.push_back({score, nullptr, nullptr, score->m_accountID == me});
        }
    }
    int rows = (int(m_cards.size()) + COLUMNS - 1) / COLUMNS;
    m_scroll->setContentHeight(rows > 0 ? rows * m_cardHeight + (rows - 1) * m_gap : 0);
    m_scroll->scrollTo(0, false);
    layoutVisibleCards();

    bool paged = m_mode == Mode::Search;
    m_previous->setVisible(paged && m_page > 0);
    m_next->setVisible(paged && int(m_cards.size()) >= PAGE_SIZE);

    size_t n = m_cards.size();
    auto players = fmt::format("{} player{}", n, n == 1 ? "" : "s");
    if (m_mode == Mode::Friends) {
        setStatus(n == 0 ? "no friends yet: search for a player and add them from their profile"
                         : fmt::format("your friends: {}", players));
    } else if (n == 0) {
        setStatus(m_page > 0 ? "no more players: go back a page"
                             : fmt::format("no players found for \"{}\"", m_query));
    } else {
        setStatus(fmt::format("players matching \"{}\": {}, page {}", m_query, players, m_page + 1));
    }
}

void FriendsOverlay::clearCards() {
    m_hovered = nullptr;
    m_pressed = nullptr;
    m_scroll->content()->removeAllChildren();
    m_cards.clear();
    m_scroll->setContentHeight(0);
    m_scroll->scrollTo(0, false);
}

void FriendsOverlay::layoutVisibleCards() {
    // Only cards within (or near) the view exist and draw.
    float viewTop = m_scroll->scroll();
    float viewBottom = viewTop + m_scroll->getContentSize().height;
    float margin = m_cardHeight * 2;
    for (size_t i = 0; i < m_cards.size(); i++) {
        auto& card = m_cards[i];
        float top = float(i / COLUMNS) * (m_cardHeight + m_gap);
        float left = float(i % COLUMNS) * (m_cardWidth + m_gap);
        bool inView = top + m_cardHeight >= viewTop - margin && top <= viewBottom + margin;
        if (!inView) {
            if (card.node) card.node->setVisible(false);
            continue;
        }
        if (!card.node) card.node = buildCard(card);
        card.node->setVisible(true);
        card.node->setPosition({left, -top - m_cardHeight});
    }
}

CCNode* FriendsOverlay::buildCard(Card& card) {
    float k = m_k;
    auto s = card.score.data();
    float w = m_cardWidth, h = m_cardHeight;
    auto node = CCNode::create();
    node->setContentSize({w, h});
    m_scroll->content()->addChild(node);

    // Background4, Background3 under the mouse; your own card in the scheme's colour.
    card.bg = RoundedBox::create({w, h}, CARD_RADIUS * k, card.mine ? m_scheme.dark3() : m_scheme.background4());
    card.bg->setAnchorPoint({0, 0});
    node->addChild(card.bg);

    // Their icon, in their colours, on a darker tile.
    float tile = AVATAR_TILE * k;
    float inset = (h - tile) / 2;
    auto tileBox = RoundedBox::create({tile, tile}, 10 * k, m_scheme.background6());
    tileBox->setPosition({inset + tile / 2, h / 2});
    node->addChild(tileBox);
    auto icon = playerIcon(s, AVATAR * k);
    icon->setPosition(tileBox->getPosition());
    node->addChild(icon);

    float x = inset + tile + 12 * k;
    float right = w - 12 * k;

    // Their name, GD's moderator badge after it (as on their profile).
    auto name = makeText(s->m_userName, Weight::SemiBold, NAME_TEXT * k);
    name->setAnchorPoint({0, 0.5f});
    name->setPosition({x, h / 2 + 11 * k});
    if (card.mine) name->setColor(theme::rgb(m_scheme.highlight1()));
    bool hasBadge = s->m_modBadge > 0 && s->m_modBadge <= 3;
    fitWidth(name, right - x - (hasBadge ? 20 * k : 0));
    node->addChild(name);
    if (hasBadge) {
        if (auto badge = gdIcon(fmt::format("modBadge_0{}_001.png", s->m_modBadge).c_str(), 14 * k)) {
            badge->setAnchorPoint({0, 0.5f});
            badge->setPosition({x + name->getScaledContentSize().width + 6 * k, name->getPositionY()});
            node->addChild(badge);
        }
    }

    // Stars, moons, demons and user coins, each after GD's icon for it.
    struct Stat { char const* sprite; int value; };
    Stat const stats[] = {
        {"GJ_starsIcon_001.png", s->m_stars},
        {"GJ_moonsIcon_001.png", s->m_moons},
        {"GJ_demonIcon_001.png", s->m_demons},
        {"GJ_coinsIcon2_001.png", s->m_userCoins},
    };
    float sx = x, sy = h / 2 - 11 * k;
    for (auto& stat : stats) {
        if (sx > right - 40 * k) break; // a narrow screen: the first few
        if (auto sprite = gdIcon(stat.sprite, STAT_ICON * k)) {
            sprite->setAnchorPoint({0, 0.5f});
            sprite->setPosition({sx, sy});
            node->addChild(sprite);
            sx += sprite->getScaledContentSize().width + 4 * k;
        }
        auto label = makeText(withCommas(stat.value), Weight::Regular, STAT_TEXT * k);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({sx, sy});
        label->setColor(theme::CONTENT2);
        node->addChild(label);
        sx += label->getScaledContentSize().width + 14 * k;
    }
    return node;
}

void FriendsOverlay::onUpdate(float) {
    layoutVisibleCards();

    auto mouse = geode::cocos::getMousePos();
    Card* hovered = nullptr;
    if (isOpen() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse)) {
        for (auto& card : m_cards) {
            if (card.node && card.node->isVisible() && card.bg && nodeContains(card.bg, mouse)) {
                hovered = &card;
                break;
            }
        }
    }
    if (hovered != m_hovered) {
        if (m_hovered && m_hovered->bg) {
            m_hovered->bg->setFillColor(m_hovered->mine ? m_scheme.dark3() : m_scheme.background4());
        }
        if (hovered && hovered->bg) {
            hovered->bg->setFillColor(hovered->mine ? m_scheme.dark4() : m_scheme.background3());
            sfx::hover(sfx::sound::DEFAULT_HOVER);
        }
        m_hovered = hovered;
    }
}

bool FriendsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    auto loc = touch->getLocation();
    // The search box takes its own touches.
    if (isOpen() && this->isVisible() && m_search->getInputNode() && nodeContains(m_search, loc)) return false;
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    m_pressed = nullptr;
    if (m_scroll->containsWorldPoint(loc)) {
        for (auto& card : m_cards) {
            if (card.node && card.node->isVisible() && card.bg && nodeContains(card.bg, loc)) {
                m_pressed = &card;
                break;
            }
        }
    }
    m_drag.began(m_scroll, loc);
    return true;
}

void FriendsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) {
        m_pressed = nullptr;
        cancelPress();
    }
}

void FriendsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    bool dragged = m_drag.ended();
    auto card = m_pressed;
    m_pressed = nullptr;
    if (dragged || !card || !card->bg || !nodeContains(card->bg, touch->getLocation())) return;
    // Their profile (GD's page, shown as ours).
    auto score = card->score.data();
    if (!score || score->m_accountID <= 0) return;
    sfx::click(sfx::sound::DEFAULT_SELECT);
    ProfilePage::create(score->m_accountID, score->m_accountID == myAccountID())->show();
}

void FriendsOverlay::loadLevelsFinished(CCArray* users, char const* key) {
    if (m_mode == Mode::Search && m_loading && key && m_key == key) showUsers(users);
}

void FriendsOverlay::loadLevelsFinished(CCArray* users, char const* key, int) { loadLevelsFinished(users, key); }

// GD answers a search with nothing in it this way too.
void FriendsOverlay::loadLevelsFailed(char const* key) {
    if (m_mode != Mode::Search || !m_loading || !key || m_key != key) return;
    detach();
    m_previous->setVisible(m_page > 0);
    setStatus(m_page > 0 ? "no more players: go back a page"
                         : fmt::format("no players found for \"{}\" (or the servers didn't answer)", m_query));
}

void FriendsOverlay::loadLevelsFailed(char const* key, int) { loadLevelsFailed(key); }

void FriendsOverlay::getUserListFinished(CCArray* users, UserListType type) {
    if (m_mode == Mode::Friends && m_loading && type == UserListType::Friends) showUsers(users);
}

void FriendsOverlay::getUserListFailed(UserListType type, GJErrorCode error) {
    if (m_mode != Mode::Friends || !m_loading || type != UserListType::Friends) return;
    detach();
    setStatus(error == GJErrorCode::NotFound ? "no friends yet: search for a player and add them from their profile"
                                             : "couldn't load your friends: check your connection and try again");
}

// GD hands the cached list through here when it's fresh enough.
void FriendsOverlay::userListChanged(CCArray* users, UserListType type) { getUserListFinished(users, type); }

} // namespace lazer
