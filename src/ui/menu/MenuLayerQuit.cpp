// The menu's keys and its way out: Escape and the back button close what's
// open first, Enter and Space press the logo, and quitting asks through
// osu!'s exit dialog, then (with the intro on) plays osu!'s outro before GD's
// own quit.

#include "MenuLayerInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Easing.hpp"
#include "../core/MenuCursor.hpp"
#include "../core/Quips.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "../overlays/Dialog.hpp"
#include "../startup/IntroSequence.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>

using namespace geode::prelude;
namespace icon = lazer::icon;

namespace {
    // Quitting: the outro is playing.
    bool g_exiting = false;

    constexpr float OUTRO_MS = 3000; // IntroScreen.exit_delay

    // The outro (osu!'s IntroScreen.OnResuming): the logo turns away in the
    // middle while osu!'s "see you next time" plays and the words spread out
    // under it, the music ducks and fades, and the screen goes to black. Then `done` quits.
    class Outro : public CCLayerColor {
    public:
        static Outro* create(float logoRadius, std::function<void()> done) {
            auto ret = new Outro();
            ret->m_done = std::move(done);
            ret->initWithColor({0, 0, 0, 0});
            ret->autorelease();
            ret->setTouchEnabled(true);
            ret->scheduleUpdate();

            auto win = CCDirector::get()->getWinSize();
            float k = win.height / 768.f;
            ret->m_k = k;
            ret->m_text = CCNode::create();
            ret->m_text->setPosition({win.width / 2, win.height / 2 - logoRadius - 60 * k});
            ret->addChild(ret->m_text);
            for (char c : std::string("see you next time")) {
                auto label = lazer::makeText(std::string(1, c), lazer::Weight::Regular, 30 * k);
                label->setOpacity(0);
                ret->m_text->addChild(label);
                ret->m_chars.push_back(label);
            }
            ret->layoutText(0);

            lazer::sfx::playCue(lazer::sfx::cue::SEEYA);
            return ret;
        }

        void layoutText(float t) {
            // Spreads out over the whole outro, like the intro's welcome text.
            float spacing = (4 + 14 * static_cast<float>(lazer::ease(lazer::Easing::OutQuint, t))) * m_k;
            float total = 0;
            std::vector<float> widths;
            for (auto label : m_chars) {
                float w = label->getScaledContentSize().width;
                if (std::string_view(label->getString()) == " ") w = std::max(w, 30 * m_k * 0.25f);
                widths.push_back(w);
                total += w;
            }
            total += spacing * (m_chars.size() - 1);
            float x = -total / 2;
            for (size_t i = 0; i < m_chars.size(); i++) {
                m_chars[i]->setPosition({x + widths[i] / 2, 0});
                x += widths[i] + spacing;
            }
        }

        void registerWithTouchDispatcher() override {
            CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
        }
        bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

        void update(float dt) override {
            m_ms += dt * 1000.f;
            float t = std::min(1.f, m_ms / OUTRO_MS);
            // osu! fades the whole game out linearly over the outro.
            this->setOpacity(static_cast<GLubyte>(t * 255));
            if (auto channel = FMODAudioEngine::get()->getActiveMusicChannel(0)) {
                // Duck to almost silent, then ramp out over the rest (osu!'s voice-on fade).
                constexpr float INITIAL_FADE = 200;
                float volume = m_ms < INITIAL_FADE
                    ? 1.f - 0.97f * m_ms / INITIAL_FADE
                    : 0.03f * (1.f - static_cast<float>(lazer::ease(lazer::Easing::In, (m_ms - INITIAL_FADE) / (OUTRO_MS - INITIAL_FADE))));
                channel->setVolume(std::max(0.f, volume));
            }
            // The words stay bright over the darkening screen, then go at the very end.
            float in = std::clamp((m_ms - 150) / 500.f, 0.f, 1.f);
            float out = std::clamp((OUTRO_MS - m_ms) / 500.f, 0.f, 1.f);
            auto alpha = static_cast<GLubyte>(lazer::ease(lazer::Easing::OutQuad, std::min(in, out)) * 255);
            for (auto label : m_chars) label->setOpacity(alpha);
            layoutText(t);
            if (t >= 1.f && m_done) {
                auto done = std::move(m_done);
                m_done = nullptr;
                done();
            }
        }

    private:
        std::function<void()> m_done;
        float m_ms = 0;
        float m_k = 1;
        CCNode* m_text = nullptr;
        std::vector<CCLabelBMFont*> m_chars;
    };
}

void LazerMenuLayer::onQuit(CCObject* sender) {
#ifdef GEODE_IS_ANDROID
    // The back button: see keyBackClicked below.
    if (!sender && this->lazerBack()) return;
#endif
    if (!Mod::get()->getSettingValue<bool>("enabled")) return MenuLayer::onQuit(sender);
    if (g_exiting || lazer::Dialog::isOpen()) return;
    // osu!'s ConfirmExitDialog in place of GD's quit popup.
    lazer::quips::say("exit", 0.7f);
    Ref<MenuLayer> self = this;
    lazer::Dialog::show(icon::TRIANGLE_EXCLAMATION, "Are you sure you want to exit Geometry Dash?", "Last chance to turn back", {
        {"Let me out!", lazer::Dialog::Kind::Ok, [self] { static_cast<LazerMenuLayer*>(self.data())->quitGame(); }},
        {"Just a little more...", lazer::Dialog::Kind::Cancel, nullptr},
    });
}

// Quits, after osu!'s outro (see you next time) when the intro is on.
void LazerMenuLayer::quitGame() {
    if (g_exiting) return;
    g_exiting = true;
    lazer::releaseMenuCursor();
    if (!Mod::get()->getSettingValue<bool>("intro")) return this->quitLikeGD();

    auto& f = m_fields;
    this->closeOverlaysExcept(nullptr);
    if (f->nowPlaying) f->nowPlaying->close();
    if (f->account) f->account->close();
    if (f->ticker) f->ticker->hide();
    if (f->buttons) f->buttons->playExit(OUTRO_MS);

    Ref<MenuLayer> self = this;
    float logoRadius = f->buttons ? f->buttons->logoRadius() : 0.f;
    this->addChild(Outro::create(logoRadius, [self] { static_cast<LazerMenuLayer*>(self.data())->quitLikeGD(); }), 1000);
}

// GD's quit popup's "yes": it saves the game while everything still runs,
// then ends. (Just ending saves during shutdown, after Geode's async
// runtime is gone, and mods saving through it crash: BetterInfo.) A
// stand-in alert with the quit popup's tag (0) takes that branch.
void LazerMenuLayer::quitLikeGD() {
    auto alert = FLAlertLayer::create(nullptr, "", "", "", nullptr);
    if (!alert) return this->endGame();
    alert->setTag(0);
    MenuLayer::FLAlert_Clicked(alert, true);
}

// Escape closes overlays, then collapses the button bar (like osu!), then GD's quit prompt.
// Returns whether it handled the key.
bool LazerMenuLayer::lazerBack() {
    if (g_exiting) return true;
    if (m_fields->nowPlaying && m_fields->nowPlaying->back()) return true;
    if (m_fields->account && m_fields->account->back()) return true;
    if (m_fields->settings && m_fields->settings->back()) return true;
    if (m_fields->quests && m_fields->quests->back()) return true;
    if (m_fields->leaderboards && m_fields->leaderboards->back()) return true;
    if (m_fields->paths && m_fields->paths->back()) return true;
    if (m_fields->achievements && m_fields->achievements->back()) return true;
    if (m_fields->stats && m_fields->stats->back()) return true;
    if (m_fields->buttons && m_fields->buttons->back()) return true;
    return false;
}

// GD's Enter and Space open its level select. Here they press the logo, like osu!'s Select.
void LazerMenuLayer::keyDown(enumKeyCodes key, double timestamp) {
    auto& f = m_fields;
    bool select = key == KEY_Enter || key == KEY_NumEnter || key == KEY_Space;
    if (!f->buttons || !select) return MenuLayer::keyDown(key, timestamp);
    auto intro = typeinfo_cast<lazer::IntroSequence*>(this->getChildByID("intro"_spr));
    bool busy = g_exiting || lazer::menuBlocked() || (intro && !intro->revealed())
        || (f->nowPlaying && f->nowPlaying->isOpen()) || (f->account && f->account->isOpen());
    if (!busy) f->buttons->pressLogo();
}

#ifndef GEODE_IS_ANDROID
// On Android, keyBackClicked is just onQuit(nullptr): too small to hook (the
// hook's patch spills into the next function), so onQuit handles it there.
void LazerMenuLayer::keyBackClicked() {
    if (this->lazerBack()) return;
    MenuLayer::keyBackClicked();
}
#endif
