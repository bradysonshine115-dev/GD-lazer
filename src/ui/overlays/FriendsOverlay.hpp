#pragma once
#include <Geode/platform/platform.hpp>
#include "WaveOverlay.hpp"
#include "../core/ScrollArea.hpp"
#include <Geode/Geode.hpp>
namespace lazer {
class FriendsOverlay : public WaveOverlay, public LevelManagerDelegate, public UserListDelegate {
public:
    static FriendsOverlay* create(float topInset);
    ~FriendsOverlay() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void loadLevelsFinished(cocos2d::CCArray*, char const*) override;
    void loadLevelsFinished(cocos2d::CCArray*, char const*, int) override;
    void loadLevelsFailed(char const*) override;
    void loadLevelsFailed(char const*, int) override;
    void getUserListFinished(cocos2d::CCArray*, UserListType) override;
    void getUserListFailed(UserListType, GJErrorCode) override;
    void userListChanged(cocos2d::CCArray*, UserListType) override;
protected:
    void onClosed() override;
    void onOpened() override;
private:
    bool init(float);
    void search();
    void load();
    void showUsers(cocos2d::CCArray*);
    void clearRows();
    void detach();
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    std::vector<ButtonRow*> m_rows;
    ButtonRow* m_previous = nullptr;
    ButtonRow* m_next = nullptr;
    bool m_friends = false;
    bool m_loading = false;
    int m_page = 0;
    std::string m_query, m_key;
};
}
