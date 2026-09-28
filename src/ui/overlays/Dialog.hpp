#pragma once

#include "../core/Easing.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

class RoundedBox;

// osu!'s PopupDialog (DialogOverlay): a dark card over a dimmed screen, an icon
// in a ring, a header and body, and full-width sheared buttons. Stands in for
// GD's alert popups in our own screens.
class Dialog : public cocos2d::CCLayer {
public:
    // PopupDialogOkButton (pink), PopupDialogCancelButton (blue),
    // PopupDialogDangerousButton (red, hold to confirm).
    enum class Kind { Ok, Cancel, Dangerous };
    struct Button {
        std::string label;
        Kind kind = Kind::Ok;
        std::function<void()> action;
    };

    // Shows over the running scene. Back / Escape presses the last Cancel
    // button (or just closes when there's none).
    static Dialog* show(char const* icon, std::string const& header, std::string const& body, std::vector<Button> buttons);

    // One is on screen: screens underneath should ignore keys and the wheel.
    static bool isOpen();

    void close();

private:
    struct ButtonNode {
        Button def;
        cocos2d::CCNode* root = nullptr;
        RoundedBox* bg = nullptr;
        RoundedBox* progress = nullptr; // dangerous: fills while held
        RoundedBox* flash = nullptr;
        cocos2d::CCLabelBMFont* text = nullptr;
        Tweened<float> width {0.8f};   // share of the dialog's width
        Tweened<float> hold {0.f};     // dangerous: 0..1
        Tweened<float> flashAlpha {0.f};
        bool hovered = false;
        float lastTickMs = -1000;
    };

    bool init(char const* icon, std::string const& header, std::string const& body, std::vector<Button> buttons);
    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void keyBackClicked() override;
    void onExit() override;

    ButtonNode* buttonAt(cocos2d::CCPoint world);
    void setHovered(ButtonNode& b, bool hovered);
    void press(ButtonNode& b);

    float m_k = 1;
    float m_width = 0;
    float m_fit = 1;                  // scale to fit the screen's height
    cocos2d::CCLayerColor* m_dim = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    RoundedBox* m_ring = nullptr;
    cocos2d::CCNode* m_icon = nullptr;
    float m_iconBaseScale = 1;
    std::vector<ButtonNode> m_buttons;
    ButtonNode* m_pressed = nullptr;
    bool m_holding = false;
    bool m_closing = false;
    float m_ms = 0;
    Tweened<float> m_dimAlpha {0.f};
    Tweened<float> m_alpha {0.f};
    Tweened<float> m_scale {0.7f};
    Tweened<float> m_ringSize {20.f};
    Tweened<float> m_iconScale {0.f};
};

} // namespace lazer
