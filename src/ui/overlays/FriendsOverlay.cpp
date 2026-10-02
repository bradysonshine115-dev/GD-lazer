#include "FriendsOverlay.hpp"
#include "../core/Text.hpp"
using namespace geode::prelude;
namespace lazer {
FriendsOverlay* FriendsOverlay::create(float inset) {
    auto ret = new FriendsOverlay();
    if (ret->init(inset)) { ret->autorelease(); return ret; }
    delete ret; return nullptr;
}
FriendsOverlay::~FriendsOverlay() { detach(); }
void FriendsOverlay::detach() {
    auto manager = GameLevelManager::sharedState();
    if (manager->m_levelManagerDelegate == this) manager->m_levelManagerDelegate = nullptr;
    if (manager->m_userListDelegate == this) manager->m_userListDelegate = nullptr;
    m_loading = false;
}
void FriendsOverlay::onClosed() { m_search->defocus(); detach(); }
void FriendsOverlay::onOpened() { m_scroll->claimWheel(); }
bool FriendsOverlay::init(float inset) {
    if (!WaveOverlay::init(inset, theme::Scheme{190}, icon::USER_PLUS,
        "friends", "find your people · search players or browse your friends")) return false;
    auto size = bodySize();
    float pad = 50 * m_k, width = size.width - 2 * pad;
    m_search = TextInput::create((width * 0.65f) / m_k, "player name or user ID", "outfit-regular.fnt"_spr);
    m_search->setScale(m_k); m_search->setMaxCharCount(40);
    m_search->setCommonFilter(CommonFilter::Any);
    m_search->setPosition({pad + width * 0.325f, size.height - 28 * m_k});
    body()->addChild(m_search);
    auto button = [&](std::string text, float x, float y, float w, std::function<void()> action) {
        auto row = ButtonRow::create(text, w, m_k, std::move(action));
        row->setPosition({x,y}); body()->addChild(row); addInteractive(row); return row;
    };
    button("search", pad + width * 0.68f, size.height - 49 * m_k, width * 0.14f, [this] { search(); });
    button("my friends", pad + width * 0.84f, size.height - 49 * m_k, width * 0.16f, [this] {
        if (m_loading) return;
        m_search->defocus(); m_friends = true; m_page = 0; load();
    });
    m_status = makeText("Search for a player to open their profile and add a friend.", Weight::Regular, 15 * m_k);
    m_status->setPosition({size.width / 2, size.height - 75 * m_k}); body()->addChild(m_status);
    m_scroll = ScrollArea::create({width, std::max(40.f * m_k, size.height - 150 * m_k)});
    m_scroll->setPosition({pad, 55 * m_k}); body()->addChild(m_scroll);
    m_previous = button("previous", pad, 5 * m_k, 120 * m_k, [this] { if (!m_loading && m_page > 0) { --m_page; load(); } });
    m_next = button("next", size.width - pad - 120 * m_k, 5 * m_k, 120 * m_k, [this] { if (!m_loading) { ++m_page; load(); } });
    m_previous->setEnabled(false); m_next->setEnabled(false);
    return true;
}
void FriendsOverlay::clearRows() {
    cancelPress();
    for (auto row : m_rows) removeInteractive(row);
    m_rows.clear(); m_scroll->content()->removeAllChildren();
    m_scroll->setContentHeight(0); m_scroll->scrollTo(0, false);
}
void FriendsOverlay::search() {
    if (m_loading) return;
    std::string query = m_search->getString();
    auto first = query.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { m_status->setString("Enter a player name or user ID first."); return; }
    m_query = query.substr(first, query.find_last_not_of(" \t\r\n") - first + 1);
    m_search->defocus(); m_friends = false; m_page = 0; load();
}
void FriendsOverlay::load() {
    auto manager = GameLevelManager::sharedState();
    if (m_friends && !GJAccountManager::sharedState()->m_accountID) {
        clearRows(); m_previous->setEnabled(false); m_next->setEnabled(false);
        m_status->setString("Sign in to your GD account to view your friends."); return;
    }
    if ((m_friends && manager->m_userListDelegate && manager->m_userListDelegate != this) ||
        (!m_friends && manager->m_levelManagerDelegate && manager->m_levelManagerDelegate != this)) {
        m_status->setString("Another page is loading. Close it and try again."); return;
    }
    clearRows(); m_loading = true; m_previous->setEnabled(false); m_next->setEnabled(false);
    m_status->setString(m_friends ? "Loading your friends…" : "Searching players…");
    if (m_friends) { manager->m_userListDelegate = this; manager->getUserList(UserListType::Friends); }
    else {
        auto request = GJSearchObject::create(SearchType::Users, m_query);
        request->m_page = m_page; m_key = request->getKey();
        manager->m_levelManagerDelegate = this; manager->getUsers(request);
    }
}
void FriendsOverlay::showUsers(CCArray* users) {
    detach(); clearRows();
    float width = m_scroll->getContentSize().width, height = 56 * m_k;
    if (users) for (auto score : CCArrayExt<GJUserScore*>(users)) {
        if (!score) continue;
        Ref<GJUserScore> retained = score;
        auto row = ButtonRow::create("", width, m_k, [retained] {
            if (retained->m_accountID > 0) ProfilePage::create(retained->m_accountID,
                retained->m_accountID == GJAccountManager::sharedState()->m_accountID)->show();
        });
        row->setEnabled(score->m_accountID > 0);
        row->setPosition({0, -height * float(m_rows.size()) - 42 * m_k});
        auto player = SimplePlayer::create(1);
        player->updatePlayerFrame(std::max(1, score->m_iconID), score->m_iconType);
        auto gm = GameManager::get();
        player->setColors(gm->colorForIdx(score->m_color1), gm->colorForIdx(score->m_color2));
        if (score->m_glowEnabled) player->setGlowOutline(gm->colorForIdx(score->m_color3));
        else player->disableGlowOutline();
        player->setScale(24 * m_k / 30.f); player->setPosition({26 * m_k, 21 * m_k}); row->addChild(player);
        auto name = makeText(score->m_userName, Weight::SemiBold, 17 * m_k);
        name->setAnchorPoint({0,0.5f}); name->setPosition({50 * m_k,21 * m_k});
        float maxWidth = width - 240 * m_k;
        if (name->getScaledContentSize().width > maxWidth) name->setScale(name->getScale() * maxWidth / name->getScaledContentSize().width);
        row->addChild(name);
        auto detail = makeText(fmt::format("{} stars  ·  {} demons", score->m_stars, score->m_demons), Weight::Regular, 13 * m_k);
        detail->setAnchorPoint({1,0.5f}); detail->setPosition({width - 14 * m_k,21 * m_k}); row->addChild(detail);
        m_scroll->content()->addChild(row); addInteractive(row); m_rows.push_back(row);
    }
    m_scroll->setContentHeight(height * m_rows.size());
    m_previous->setEnabled(!m_friends && m_page > 0);
    m_next->setEnabled(!m_friends && m_rows.size() >= 10);
    m_status->setString(m_rows.empty() ? (m_friends ? "No friends yet. Search for a player to add one." : "No players found. Try another name or the previous page.") :
        fmt::format("{} · {} players{}", m_friends ? "my friends" : "search results", m_rows.size(), m_friends ? "" : fmt::format(" · page {}", m_page + 1)).c_str());
}
void FriendsOverlay::loadLevelsFinished(CCArray* users, char const* key) {
    if (!m_friends && m_loading && key && m_key == key) showUsers(users);
}
void FriendsOverlay::loadLevelsFinished(CCArray* users, char const* key, int) { loadLevelsFinished(users,key); }
void FriendsOverlay::loadLevelsFailed(char const* key) {
    if (!m_friends && m_loading && key && m_key == key) {
        detach(); m_previous->setEnabled(m_page > 0);
        m_status->setString("No results or search unavailable. Retry, change the name, or go back a page.");
    }
}
void FriendsOverlay::loadLevelsFailed(char const* key, int) { loadLevelsFailed(key); }
void FriendsOverlay::getUserListFinished(CCArray* users, UserListType type) {
    if (m_friends && m_loading && type == UserListType::Friends) showUsers(users);
}
void FriendsOverlay::getUserListFailed(UserListType type, GJErrorCode error) {
    if (m_friends && m_loading && type == UserListType::Friends) {
        detach(); m_status->setString(error == GJErrorCode::NotFound ? "No friends yet. Search for a player to add one." : "Could not load your friends. Check your connection and try again.");
    }
}
void FriendsOverlay::userListChanged(CCArray* users, UserListType type) { getUserListFinished(users,type); }
bool FriendsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (isOpen() && isVisible() && m_search) {
        auto point = m_search->convertToNodeSpace(touch->getLocation()); auto size = m_search->getContentSize();
        if (point.x >= 0 && point.y >= 0 && point.x <= size.width && point.y <= size.height) return false;
    }
    bool claimed = WaveOverlay::ccTouchBegan(touch,event);
    if (claimed) {
        if (!m_scroll->containsWorldPoint(touch->getLocation())) {
            // Offscreen list rows must never intercept fixed controls.
            for (auto row : m_rows) removeInteractive(row);
            cancelPress();
            WaveOverlay::ccTouchBegan(touch,event);
            for (auto row : m_rows) addInteractive(row);
        }
        m_drag.began(m_scroll,touch->getLocation());
    }
    return claimed;
}
void FriendsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) { if (m_drag.moved(touch->getLocation())) cancelPress(); }
void FriendsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_drag.ended()) cancelPress();
    WaveOverlay::ccTouchEnded(touch,event);
}
}
