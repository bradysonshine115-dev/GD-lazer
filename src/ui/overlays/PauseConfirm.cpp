// The pause menu's "exit level?" check (GD's Confirm Exit option) as one of
// osu!'s dialogs instead of GD's alert.

#include "../core/Text.hpp"
#include "Dialog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

namespace {
    // GD's Confirm Exit option: PauseLayer::tryQuit asks first when it's on.
    constexpr auto CONFIRM_EXIT = "0167";
}

class $modify(LazerPauseLayer, PauseLayer) {
    void tryQuit(CCObject* sender) {
        if (!Mod::get()->getSettingValue<bool>("enabled") || !GameManager::get()->getGameVariable(CONFIRM_EXIT)) {
            return PauseLayer::tryQuit(sender);
        }
        if (lazer::Dialog::isOpen()) return;
        std::string level;
        if (auto play = PlayLayer::get(); play && play->m_level) level = play->m_level->m_levelName;
        Ref<PauseLayer> self = this;
        lazer::Dialog::show(lazer::icon::TRIANGLE_EXCLAMATION, "Are you sure you want to exit the level?", level, {
            {"Let me out!", lazer::Dialog::Kind::Ok, [self] { self->onQuit(nullptr); }},
            {"Just a little more...", lazer::Dialog::Kind::Cancel, nullptr},
        });
    }

    // The dialog takes Escape (cancel) and keys: the pause menu underneath
    // would resume.
    void keyBackClicked() {
        if (lazer::Dialog::isOpen()) return;
        PauseLayer::keyBackClicked();
    }

    void keyDown(enumKeyCodes key, double timestamp) {
        if (lazer::Dialog::isOpen()) return;
        PauseLayer::keyDown(key, timestamp);
    }
};
