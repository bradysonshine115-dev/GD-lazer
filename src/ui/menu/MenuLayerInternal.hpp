#pragma once

// Private to the main menu's files (MenuLayer*.cpp and CreatorHub.cpp): the
// state they share, GD's hidden creator hub, and the MenuLayer hook itself.

#include "../../audio/MusicPlayer.hpp"
#include "../overlays/AchievementsOverlay.hpp"
#include "../overlays/LeaderboardsOverlay.hpp"
#include "../overlays/PathsOverlay.hpp"
#include "../overlays/QuestsOverlay.hpp"
#include "../overlays/SettingsOverlay.hpp"
#include "../overlays/StatsOverlay.hpp"
#include "AccountPanel.hpp"
#include "ButtonSystem.hpp"
#include "MenuBackground.hpp"
#include "NowPlayingOverlay.hpp"
#include "SongTicker.hpp"
#include "Toolbar.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include <array>
#include <string>
#include <vector>

using namespace geode::prelude;
using lazer::ButtonSystem;

// Set when a button leaves the menu, so coming back re-opens the menu it
// was in (top level or a submenu). Initial = nothing to restore.
inline ButtonSystem::State g_returnState = ButtonSystem::State::Initial;

// Set by "new level": leaving that level's page goes back to the create menu
// rather than GD's "my levels" list.
inline bool g_newLevelFlow = false;

// Vanilla menus whose buttons move into the toolbar. Mods often add buttons here too.
inline constexpr std::array TOOLBAR_SOURCE_MENUS {
    "bottom-menu", "right-side-menu", "side-menu", "top-right-menu", "profile-menu",
};
// Everything else we hide.
inline constexpr std::array HIDDEN_NODES {
    "main-menu", "main-title", "player-username", "social-media-menu",
    "more-games-menu", "close-menu",
};

// GD's hidden creator hub (CreatorHub.cpp).
void creatorAction(void (CreatorLayer::*handler)(CCObject*));

// Buttons other mods add to the creator hub (GDDP's demon progression,
// BetterInfo's...): the hub is hidden, so they move to the toolbar. Found by
// their path of child indices, which is the same in every fresh hub.
struct CreatorModButton {
    std::vector<unsigned> path;
    std::string id;
    CCNode* image = nullptr; // the button's own image, in the scanned hub
};

std::vector<CreatorModButton> scanCreatorModButtons(CreatorLayer* layer);
void creatorModAction(std::vector<unsigned> const& path);

class $modify(LazerMenuLayer, MenuLayer) {
    struct Fields {
        ButtonSystem* buttons = nullptr;
        lazer::Toolbar* toolbar = nullptr;
        lazer::MenuBackground* background = nullptr;
        lazer::SettingsOverlay* settings = nullptr;
        lazer::QuestsOverlay* quests = nullptr;
        lazer::LeaderboardsOverlay* leaderboards = nullptr;
        lazer::PathsOverlay* paths = nullptr;
        lazer::AchievementsOverlay* achievements = nullptr;
        lazer::StatsOverlay* stats = nullptr;
        lazer::NowPlayingOverlay* nowPlaying = nullptr;
        lazer::AccountPanel* account = nullptr;
        lazer::SongTicker* ticker = nullptr;
        int backgroundRequest = 0; // newest thumbnail request; older results are dropped
    };

    // Building the menu, its background and the music it follows (MenuLayerHooks.cpp).
    bool init();
    void setupBackground();
    void onTrackChanged(lazer::MusicPlayer::Track const* track);

    // The toolbar's buttons (MenuLayerToolbar.cpp).
    static int rightOrder(std::string const& id);
    void collectToolbarButtons();

    // The overlays (MenuLayerOverlays.cpp).
    void toggleSettings();
    void toggleRewards();
    void toggleLeaderboards();
    void togglePaths();
    void toggleQuests();
    void toggleAchievements();
    void toggleStats();
    void closeOverlaysExcept(CCNode* keep);
    bool closeAllOverlays();
    void toggleNowPlaying();

    // Keys and quitting (MenuLayerQuit.cpp).
    void onQuit(CCObject* sender);
    void quitGame();
    void quitLikeGD();
    bool lazerBack();
    void keyDown(enumKeyCodes key, double timestamp);
#ifndef GEODE_IS_ANDROID
    void keyBackClicked();
#endif
};
