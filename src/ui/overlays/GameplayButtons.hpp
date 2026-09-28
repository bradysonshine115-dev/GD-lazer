#pragma once

#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <vector>

// Buttons for the screens we draw inside GD's own gameplay layers (pause
// menu, level complete). They're menu items in a CCMenu, like GD's buttons
// there, so they take touches the same way GD's do over PlayLayer.
namespace lazer {

// osu!'s DialogButton as the gameplay menus use it: a full-width row with a
// sheared colour bar in the middle that widens on hover, a soft glow of the
// same colour behind it and a bold label.
class DialogButtonItem : public cocos2d::CCMenuItem {
public:
    static DialogButtonItem* create(std::string const& text, cocos2d::ccColor4B colour,
                                    float width, float height, float k, std::function<void()> action);

    // osu!'s SelectionState: hovered, or picked with the keyboard.
    void setSelectedState(bool selected);
    bool selectedState() const { return m_selectedState; }
    // Short screens get shorter buttons.
    void setHeight(float height);
    bool containsWorldPoint(cocos2d::CCPoint world);

    void selected() override;   // pressed
    void unselected() override; // released or slid off
    void activate() override;   // clicked
    void update(float dt) override;

    bool init(std::string const& text, cocos2d::ccColor4B colour, float width, float height, float k,
              std::function<void()> action);

protected:
    void layout();

    std::function<void()> m_action;
    float m_k = 1;
    cocos2d::CCNodeRGBA* m_glow = nullptr;
    cocos2d::CCLayerGradient* m_glowLeft = nullptr;
    cocos2d::CCLayerColor* m_glowCentre = nullptr;
    cocos2d::CCLayerGradient* m_glowRight = nullptr;
    RoundedBox* m_bar = nullptr;
    RoundedBox* m_flash = nullptr;
    cocos2d::CCLabelBMFont* m_text = nullptr;
    float m_textBase = 1;
    bool m_selectedState = false;
    bool m_settling = true;       // lay out once more after the tweens stop
    float m_clickMs = 0;          // the click's widen is playing
    Tweened<float> m_width {0.8f};
    Tweened<float> m_glowWidth {0.8f * 1.08f};
    Tweened<float> m_glowAlpha {0.f};
    Tweened<float> m_textScale {1.f};
    Tweened<float> m_flashAlpha {0.f};
};

// osu!'s OsuAnimatedButton: a rounded button that shrinks while held, springs
// back on release and flashes when clicked (the results screen's buttons).
// `content` (an icon, a label, another mod's button image) sits centred on it.
class AnimatedButtonItem : public cocos2d::CCMenuItem {
public:
    static AnimatedButtonItem* create(cocos2d::CCSize size, float radius, cocos2d::ccColor4B colour,
                                      cocos2d::CCNode* content, std::function<void()> action);

    void setHovered(bool hovered);
    bool containsWorldPoint(cocos2d::CCPoint world);

    void selected() override;
    void unselected() override;
    void activate() override;
    void update(float dt) override;

    bool init(cocos2d::CCSize size, float radius, cocos2d::ccColor4B colour, cocos2d::CCNode* content,
              std::function<void()> action);

protected:
    std::function<void()> m_action;
    cocos2d::CCNodeRGBA* m_root = nullptr;
    RoundedBox* m_hoverBox = nullptr;
    bool m_hovered = false;
    Tweened<float> m_scale {1.f};
    Tweened<float> m_hoverAlpha {0.f};
};

// An icon and a label side by side, for AnimatedButtonItem.
cocos2d::CCNode* iconLabel(char const* glyph, std::string const& text, float size);

// Buttons other mods added to one of GD's menus: their handler lives outside
// GD's binary (or they run a Geode callback and carry a mod's ID).
bool isModButton(cocos2d::CCMenuItem* item);

// The visible mod buttons in the CCMenus directly under `parent`.
std::vector<cocos2d::CCMenuItem*> collectModButtons(cocos2d::CCNode* parent);

// A small picture of another mod's button, fitted into a `size` square.
cocos2d::CCNode* modButtonImage(cocos2d::CCMenuItem* item, float size);

// A GD popup or one of our dialogs is open over the scene (the settings
// opened from the pause menu, a leaderboard): menus underneath stop hovering.
bool gameplayPopupOnTop();

} // namespace lazer
