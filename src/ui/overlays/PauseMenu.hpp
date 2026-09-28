#pragma once

#include "../core/Easing.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <vector>

class PauseLayer;
class Slider;

namespace lazer {

class DialogButtonItem;
class AnimatedButtonItem;

// osu!'s pause overlay (Screens/Play/PauseOverlay + GameplayMenuOverlay) drawn
// inside GD's PauseLayer: a dimmed screen, a big "paused" title, continue /
// retry / quit as osu!'s wide buttons and the retry count underneath. GD's
// extras (practice mode, restart from the start, edit, level options,
// volume) and other mods' pause buttons sit in a footer row. GD's own nodes
// stay alive but hidden; every button runs GD's handler.
class PauseMenu : public cocos2d::CCNodeRGBA {
public:
    // Built at the end of GD's PauseLayer::customSetup.
    static PauseMenu* create(PauseLayer* layer);

    // osu!'s keyboard selection: up / down pick a button, enter presses it.
    // True when the key was used.
    bool handleKey(cocos2d::enumKeyCodes key);

    void update(float dt) override;

protected:
    bool init(PauseLayer* layer);
    void build();
    void layout();
    void takeModButtons();
    void select(int index);
    void openVolume();

    PauseLayer* m_layer = nullptr;
    float m_k = 1;
    cocos2d::CCNode* m_titleBlock = nullptr;
    cocos2d::CCNode* m_infoBlock = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    std::vector<DialogButtonItem*> m_buttons;
    std::vector<AnimatedButtonItem*> m_extras;
    std::vector<AnimatedButtonItem*> m_mods;
    std::vector<AnimatedButtonItem*> m_footer; // extras and mods, for hover
    Slider* m_musicSlider = nullptr;
    Slider* m_sfxSlider = nullptr;
    int m_selected = -1;
    bool m_scanned = false;
    cocos2d::CCPoint m_lastMouse {-1, -1};
    Tweened<float> m_alpha {0.f};
};

} // namespace lazer
