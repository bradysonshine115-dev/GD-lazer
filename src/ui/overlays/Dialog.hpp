#pragma once

#include "../core/Easing.hpp"
#include "../core/ScrollArea.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

class RoundedBox;
class SettingsRow;

// osu!'s PopupDialog (DialogOverlay): a dark card over a dimmed screen, an icon
// in a ring, a header and body, and full-width sheared buttons. Stands in for
// GD's alert popups in our own screens. Beyond osu!'s: a scrolling list of
// items (settings rows, release notes) under the body, a progress bar, and
// changing its content in place (a download that reports back).
class Dialog : public cocos2d::CCLayer {
public:
    // PopupDialogOkButton (pink), PopupDialogCancelButton (blue),
    // PopupDialogDangerousButton (red, hold to confirm).
    enum class Kind { Ok, Cancel, Dangerous };
    struct Button {
        std::string label;
        Kind kind = Kind::Ok;
        std::function<void()> action;
        // Off: pressing runs the action and the dialog stays (it usually
        // changes its content).
        bool closes = true;
    };

    struct Content {
        char const* icon = nullptr;
        std::string header;
        std::string body;
        std::vector<Button> buttons;
        // Stacked top-down in a scroll area under the body. Settings rows get
        // hover, clicks and drags; their tooltip shows under the list.
        std::vector<cocos2d::CCNode*> items;
        float listHeight = 340;  // at most, osu! pixels
        // A node of its own under the body, laid out by its content size:
        // not scrolled or clipped, and it keeps its own opacities so it can
        // animate. It appears once the dialog has faded in.
        cocos2d::CCNode* panel = nullptr;
        bool progress = false;   // a progress bar under the body (setProgress)
        float width = 500;       // osu! pixels (DialogOverlay's dialogs are 500)
    };

    // Shows over the running scene. Back / Escape presses the last Cancel
    // button (or just closes when there's none).
    static Dialog* show(char const* icon, std::string const& header, std::string const& body, std::vector<Button> buttons);
    static Dialog* show(Content content);

    // One is on screen: screens underneath should ignore keys and the wheel.
    static bool isOpen();

    // Width for rows in `items`, for a dialog `width` osu! pixels wide.
    static float listWidth(float width = 500);

    // Replaces what the dialog shows, in place.
    void setContent(Content content);
    // 0..1, with a line of text under the bar.
    void setProgress(float progress, std::string const& text = "");
    // Re-reads every row (a change in one can change others).
    void refreshRows();

    void close();
    // Closed (or fading out): gone for anything that wants to update it.
    bool closing() const { return m_closing; }

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

    bool init(Content content);
    void build(Content content);
    void layoutItems();
    void showTooltip(std::string const& text);
    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void keyBackClicked() override;
    void onExit() override;

    ButtonNode* buttonAt(cocos2d::CCPoint world);
    SettingsRow* rowAt(cocos2d::CCPoint world);
    void setHovered(ButtonNode& b, bool hovered);
    void press(ButtonNode& b);

    float m_k = 1;
    float m_width = 0;
    float m_fit = 1;                  // scale to fit the screen's height
    cocos2d::CCLayerColor* m_dim = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_column = nullptr;
    RoundedBox* m_ring = nullptr;
    cocos2d::CCNode* m_icon = nullptr;
    float m_iconBaseScale = 1;
    std::vector<ButtonNode> m_buttons;
    ButtonNode* m_pressed = nullptr;
    bool m_holding = false;
    bool m_closing = false;
    float m_ms = 0;

    // Items
    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    std::vector<cocos2d::CCNode*> m_items;
    std::vector<SettingsRow*> m_rows;
    SettingsRow* m_hoveredRow = nullptr;
    SettingsRow* m_pressedRow = nullptr;
    bool m_rowDragging = false;
    bool m_listTouch = false;    // the touch in progress began on the list
    cocos2d::CCNode* m_tooltipHolder = nullptr;
    std::string m_tooltip;
    float m_tooltipWidth = 0;
    cocos2d::CCNode* m_panel = nullptr;

    // Progress
    RoundedBox* m_progressTrack = nullptr;
    RoundedBox* m_progressFill = nullptr;
    cocos2d::CCLabelBMFont* m_progressText = nullptr;
    float m_progressWidth = 0;
    Tweened<float> m_progress {0.f};

    Tweened<float> m_dimAlpha {0.f};
    Tweened<float> m_alpha {0.f};
    Tweened<float> m_scale {0.7f};
    Tweened<float> m_ringSize {20.f};
    Tweened<float> m_iconScale {0.f};
};

} // namespace lazer
