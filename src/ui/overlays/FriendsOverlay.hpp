#pragma once

#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer {

class RoundedBox;

// GD's user search and friends list as osu!'s social overlay: a box for a
// player's name or user ID with "search" and "my friends" beside it, then the
// players as cards two across (osu!'s UserGridPanel), each with their icon in
// their colours, their name (and moderator badge) and their stars, moons,
// demons and user coins. A card opens their profile (GD's page, shown as
// ours). Everything comes from GameLevelManager; a search is paged the way
// GD's is, ten players a page. Cards are built as they scroll into view.
class FriendsOverlay : public WaveOverlay, public LevelManagerDelegate, public UserListDelegate,
                       public TextInputDelegate {
public:
    static FriendsOverlay* create(float topInset);
    ~FriendsOverlay() override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

    // LevelManagerDelegate: the user search.
    void loadLevelsFinished(cocos2d::CCArray* users, char const* key) override;
    void loadLevelsFinished(cocos2d::CCArray* users, char const* key, int) override;
    void loadLevelsFailed(char const* key) override;
    void loadLevelsFailed(char const* key, int) override;
    // UserListDelegate: the friends list.
    void getUserListFinished(cocos2d::CCArray* users, UserListType type) override;
    void getUserListFailed(UserListType type, GJErrorCode error) override;
    void userListChanged(cocos2d::CCArray* users, UserListType type) override;
    // TextInputDelegate: whether the search box has the keyboard.
    void textInputOpened(CCTextInputNode* node) override;
    void textInputClosed(CCTextInputNode* node) override;
    void enterPressed(CCTextInputNode* node) override;
    // The menu's keys while the page is up: Enter in the search box searches.
    // True when the key was used.
    bool handleKey(cocos2d::enumKeyCodes key);

protected:
    // A player; the card is built the first time it scrolls into view.
    struct Card {
        geode::Ref<GJUserScore> score;
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        bool mine = false;
    };
    enum class Mode { Search, Friends };

    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;
    void onClosed() override;

    void search();
    void showFriends();
    void load();
    void showUsers(cocos2d::CCArray* users);
    void clearCards();
    void layoutVisibleCards();
    cocos2d::CCNode* buildCard(Card& card);
    void setStatus(std::string const& text);
    void detach();

    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    float m_statusScale = 1;
    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    ButtonRow* m_previous = nullptr;
    ButtonRow* m_next = nullptr;

    std::vector<Card> m_cards;
    Card* m_hovered = nullptr;
    Card* m_pressed = nullptr;

    Mode m_mode = Mode::Search;
    bool m_loading = false;
    bool m_typing = false;        // the search box has the keyboard
    bool m_shownFriends = false;  // the first opening shows your friends
    int m_page = 0;
    std::string m_query;          // the search as it was sent
    std::string m_key;            // GD's key for the search in flight
    float m_pad = 0, m_cardWidth = 0, m_cardHeight = 0, m_gap = 0;
};

} // namespace lazer
