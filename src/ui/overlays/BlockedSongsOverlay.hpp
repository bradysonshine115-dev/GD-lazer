#pragma once

#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

namespace lazer {

// The songs blocked in the music player (its ban button), each with an
// unblock button, and one to unblock them all.
class BlockedSongsOverlay : public WaveOverlay, public cocos2d::CCKeypadDelegate {
public:
    // Opens over the running scene.
    static void present();

    void onEnter() override;
    void onExit() override;
    void keyBackClicked() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

protected:
    bool init();
    void onClosed() override;
    void rebuild();

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    std::vector<SettingsRow*> m_rows;
};

} // namespace lazer
