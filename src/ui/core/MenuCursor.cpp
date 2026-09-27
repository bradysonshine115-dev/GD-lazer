// osu!'s menu cursor (MenuCursorContainer): the arrow from osu-resources in
// place of the system cursor, drawn above every scene. It shrinks and glows
// pink while a button is held, turns to follow a drag, springs back on
// release, and taps. It shows wherever GD would show the system cursor.
// PC only: phones have no pointer.

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

#include <Geode/modify/CCEGLView.hpp>
#include <Geode/ui/OverlayManager.hpp>

#include "../../audio/Sfx.hpp"
#include "Easing.hpp"

#include <Windows.h>

#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Where GD wants the system cursor (it hides it in gameplay).
    bool g_gdShowsCursor = true;
    bool g_enabled = false;

    bool settingEnabled() { return Mod::get()->getSettingValue<bool>("custom-cursor"); }

    constexpr float BASE_SCALE = 0.15f;              // Cursor.base_scale
    constexpr ccColor3B PINK {255, 102, 170};        // OsuColour.Pink
    // The arrow's tip in the (unpadded) 312 x 442 texture: the click point.
    constexpr float TIP_X = 16.f, TIP_Y = 6.f, TEX_W = 312.f, TEX_H = 442.f;

    CCSprite* cursorSprite(char const* file) {
        auto texture = CCTextureCache::get()->addImage(file, false);
        if (!texture) return nullptr;
        // The images are padded to 512 x 512 so they can be mipmapped: drawn at
        // ~15% of their size, plain linear filtering would make them jagged.
        texture->generateMipmap();
        ccTexParams params {GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        texture->setTexParameters(&params);
        float csf = CC_CONTENT_SCALE_FACTOR();
        auto sprite = CCSprite::createWithTexture(texture, {0, 0, TEX_W / csf, TEX_H / csf});
        sprite->setAnchorPoint({TIP_X / TEX_W, 1 - TIP_Y / TEX_H});
        return sprite;
    }

    enum class Drag { None, Started, Rotating };

    class MenuCursor : public CCNode {
    public:
        static MenuCursor* create() {
            auto ret = new MenuCursor();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        bool init() override {
            if (!CCNode::init()) return false;
            m_holder = CCNode::create();
            this->addChild(m_holder);
            m_base = cursorSprite("menu-cursor.png"_spr);
            m_additive = cursorSprite("menu-cursor-additive.png"_spr);
            if (!m_base || !m_additive) return false;
            m_holder->addChild(m_base);
            m_additive->setColor(PINK);
            m_additive->setOpacity(0);
            m_additive->setBlendFunc({GL_ONE, GL_ONE}); // additive (the texture is premultiplied)
            m_holder->addChild(m_additive);
            return true;
        }

        void visit() override {
            float dt = CCDirector::get()->getDeltaTime();
            float ms = dt * 1000.f;

            bool enabled = settingEnabled();
            if (enabled != g_enabled) {
                g_enabled = enabled;
                // Re-apply GD's wish with our cursor on or off (see the hook).
                CCEGLView::get()->showCursor(g_gdShowsCursor);
            }
            if (!enabled) return;

            auto view = CCEGLView::get();
            float pxPerPoint = view->getScaleX();

            // Mouse position from Windows, in the game window's client area.
            HWND window = WindowFromDC(wglGetCurrentDC());
            POINT p;
            RECT client;
            bool inside = false;
            CCPoint pos = m_lastPos;
            if (window && GetCursorPos(&p) && ScreenToClient(window, &p) && GetClientRect(window, &client)
                && client.right > 0 && client.bottom > 0) {
                inside = p.x >= 0 && p.y >= 0 && p.x < client.right && p.y < client.bottom;
                auto size = CCDirector::get()->getWinSize();
                pos = CCPoint(p.x / float(client.right) * size.width, (1 - p.y / float(client.bottom)) * size.height);
            }
            bool focused = window && GetForegroundWindow() == window;

            // Visibility: GD's say, and only over the window (outside, the system cursor is back).
            bool visible = g_gdShowsCursor && inside;
            if (visible != m_visible) {
                m_visible = visible;
                if (visible) { // PopIn
                    m_alpha.to(1, 250, Easing::OutQuint);
                    m_scale.to(1, 400, Easing::OutQuint);
                } else { // PopOut
                    m_alpha.to(0, 250, Easing::OutQuint);
                    m_scale.to(0.6f, 250, Easing::In);
                }
                if (m_drag == Drag::None) m_rotation.to(0, 400, Easing::OutQuint);
                // Leaving the window: gone at once, the system cursor takes over.
                if (!inside) m_alpha.set(0);
            }

            // Buttons.
            bool down = focused && inside
                && ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000);
            if (down && !m_down) onDown(pos);
            else if (!down && m_down) onUp();
            m_down = down;

            // Drag rotation (in pixels, like osu!).
            CCPoint px = pos * pxPerPoint;
            if (m_drag != Drag::None) {
                if (px != m_lastMovePx) onMove(px);
                if (ccpDistance(m_downPx, m_lastMovePx) > 60) {
                    // Interpolation.ValueAt(0.04, down, last, 0, elapsed): the pivot floats after the cursor.
                    float f = ms > 0 ? std::min(1.f, 0.04f / ms) : 0.f;
                    m_downPx = m_downPx + (m_lastMovePx - m_downPx) * f;
                }
            }

            for (auto t : {&m_alpha, &m_scale, &m_press, &m_rotation, &m_glow}) t->update(dt);

            this->setPosition(pos);
            m_lastPos = pos;
            float size = static_cast<float>(Mod::get()->getSettingValue<double>("cursor-size"));
            // osu! draws it in screen pixels: texture pixels x base scale x size.
            float scale = BASE_SCALE * size * CC_CONTENT_SCALE_FACTOR() / pxPerPoint;
            m_holder->setScale(scale * m_scale.get() * m_press.get());
            m_holder->setRotation(m_rotation.get());
            auto alpha = static_cast<GLubyte>(std::clamp(m_alpha.get(), 0.f, 1.f) * 255);
            m_base->setOpacity(alpha);
            m_additive->setOpacity(static_cast<GLubyte>(std::clamp(m_glow.get() * m_alpha.get(), 0.f, 1.f) * 255));
            if (alpha > 0) CCNode::visit();
        }

    private:
        void onDown(CCPoint pos) {
            if (!m_visible) return;
            m_press.set(1);
            m_press.to(0.9f, 800, Easing::OutQuint);
            m_glow.set(0);
            m_glow.to(1, 800, Easing::OutQuint);
            if (Mod::get()->getSettingValue<bool>("cursor-rotation") && m_drag != Drag::Rotating) {
                m_drag = Drag::Started;
                m_downPx = pos * CCEGLView::get()->getScaleX();
                m_lastMovePx = m_downPx;
            }
            sfx::play(sfx::sound::CURSOR_TAP, 0.01f, 1.f);
        }

        void onUp() {
            m_glow.set(1);
            m_glow.to(0, 500, Easing::OutQuint);
            m_press.to(1, 500, Easing::OutElastic);
            if (m_drag != Drag::None) {
                float r = m_rotation.get();
                m_rotation.to(0, 400 * (0.5f + std::abs(r / 960)), Easing::OutElasticQuarter);
                m_drag = Drag::None;
            }
            if (m_visible) sfx::play(sfx::sound::CURSOR_TAP, 0.01f, 0.8f);
        }

        void onMove(CCPoint px) {
            m_lastMovePx = px;
            float distance = ccpDistance(px, m_downPx);
            // Not until it's moved a bit from where the button went down.
            if (m_drag == Drag::Started && distance > 80) m_drag = Drag::Rotating;
            if (m_drag != Drag::Rotating || distance <= 0) return;
            // osu!'s y points down: flip ours.
            CCPoint offset = px - m_downPx;
            float degrees = std::atan2(-offset.x, -offset.y) * 180.f / float(M_PI) + 24.3f;
            // The shortest way round.
            float current = m_rotation.get();
            float diff = std::fmod(degrees - current, 360.f);
            if (diff < -180) diff += 360;
            if (diff > 180) diff -= 360;
            m_rotation.to(current + diff, 120, Easing::OutQuint);
        }

        CCNode* m_holder = nullptr;
        CCSprite* m_base = nullptr;
        CCSprite* m_additive = nullptr;
        Tweened<float> m_alpha {0.f};
        Tweened<float> m_scale {1.f};
        Tweened<float> m_press {1.f};
        Tweened<float> m_rotation {0.f};
        Tweened<float> m_glow {0.f};
        bool m_visible = false;
        bool m_down = false;
        Drag m_drag = Drag::None;
        CCPoint m_downPx, m_lastMovePx, m_lastPos;
    };
}

} // namespace lazer

class $modify(LazerCursorView, CCEGLView) {
    void showCursor(bool state) {
        lazer::g_gdShowsCursor = state;
        CCEGLView::showCursor(state && !lazer::g_enabled);
    }
};

// Only once the game has loaded (the intro is starting): the loading screen
// stutters, and a drawn cursor would stutter with it. The system cursor
// stays until then.
$on_game(Loaded) {
    Loader::get()->queueInMainThread([] {
        if (auto cursor = lazer::MenuCursor::create()) {
            geode::OverlayManager::get()->addChild(cursor, 1 << 30);
        }
    });
}

#endif
