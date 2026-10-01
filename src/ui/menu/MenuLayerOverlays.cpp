// The menu's overlays: settings, leaderboards, paths, quests, achievements
// and statistics are full-screen and replace each other, like osu!'s; the
// daily chests and the music player open over whatever's there.

#include "MenuLayerInternal.hpp"

#include "../../settings/SettingsContent.hpp"
#include "../core/Quips.hpp"
#include "../overlays/Dialog.hpp"
#include "../overlays/RewardsPopup.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

void LazerMenuLayer::toggleSettings() {
    auto& settings = m_fields->settings;
    if (!settings) {
        // Built on first use: it reads GD's option list from a hidden options layer.
        settings = lazer::SettingsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        settings->setID("settings"_spr);
        lazer::buildSettings(settings, this, m_fields->background);
        this->addChild(settings, 15);
    }
    if (settings->isOpen()) {
        settings->close();
    } else {
        closeOverlaysExcept(settings);
        settings->open();
        lazer::quips::say("settings", 0.25f);
    }
}

// The chests are a dialog over whatever's open, and open themselves.
void LazerMenuLayer::toggleRewards() {
    if (lazer::Dialog::isOpen()) return;
    lazer::quips::say("chests", 0.5f);
    lazer::showRewards();
}

void LazerMenuLayer::toggleLeaderboards() {
    auto& leaderboards = m_fields->leaderboards;
    if (!leaderboards) {
        leaderboards = lazer::LeaderboardsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        leaderboards->setID("leaderboards"_spr);
        this->addChild(leaderboards, 16);
    }
    if (leaderboards->isOpen()) {
        leaderboards->close();
    } else {
        closeOverlaysExcept(leaderboards);
        leaderboards->open();
        lazer::quips::say("leaderboards", 0.5f);
    }
}

void LazerMenuLayer::togglePaths() {
    auto& paths = m_fields->paths;
    if (!paths) {
        paths = lazer::PathsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        paths->setID("paths"_spr);
        this->addChild(paths, 16);
    }
    if (paths->isOpen()) {
        paths->close();
    } else {
        closeOverlaysExcept(paths);
        paths->open();
        lazer::quips::say("paths", 0.4f);
    }
}

void LazerMenuLayer::toggleQuests() {
    auto& quests = m_fields->quests;
    if (!quests) {
        quests = lazer::QuestsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        quests->setID("quests"_spr);
        this->addChild(quests, 16);
    }
    if (quests->isOpen()) {
        quests->close();
    } else {
        closeOverlaysExcept(quests);
        quests->open();
        lazer::quips::say("quests", 0.4f);
    }
}

void LazerMenuLayer::toggleAchievements() {
    auto& achievements = m_fields->achievements;
    if (!achievements) {
        achievements = lazer::AchievementsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        achievements->setID("achievements"_spr);
        this->addChild(achievements, 16);
    }
    if (achievements->isOpen()) {
        achievements->close();
    } else {
        closeOverlaysExcept(achievements);
        achievements->open();
        lazer::quips::say("achievements", 0.5f);
    }
}

void LazerMenuLayer::toggleStats() {
    auto& stats = m_fields->stats;
    if (!stats) {
        stats = lazer::StatsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        stats->setID("statistics"_spr);
        this->addChild(stats, 16);
    }
    if (stats->isOpen()) {
        stats->close();
    } else {
        closeOverlaysExcept(stats);
        stats->open();
        lazer::quips::say("stats", 0.5f);
    }
}

// Full-screen overlays replace each other, like osu!'s.
void LazerMenuLayer::closeOverlaysExcept(CCNode* keep) {
    auto& f = m_fields;
    if (f->settings && f->settings != keep) f->settings->close();
    if (f->quests && f->quests != keep) f->quests->close();
    if (f->leaderboards && f->leaderboards != keep) f->leaderboards->close();
    if (f->paths && f->paths != keep) f->paths->close();
    if (f->achievements && f->achievements != keep) f->achievements->close();
    if (f->stats && f->stats != keep) f->stats->close();
}

// Every overlay and popup card. Returns whether any was open.
bool LazerMenuLayer::closeAllOverlays() {
    auto& f = m_fields;
    bool closed = false;
    auto closeIf = [&](auto overlay) {
        if (overlay && overlay->isOpen()) {
            overlay->close();
            closed = true;
        }
    };
    closeIf(f->settings);
    closeIf(f->quests);
    closeIf(f->leaderboards);
    closeIf(f->paths);
    closeIf(f->achievements);
    closeIf(f->stats);
    closeIf(f->nowPlaying);
    closeIf(f->account);
    return closed;
}

void LazerMenuLayer::toggleNowPlaying() {
    auto& nowPlaying = m_fields->nowPlaying;
    if (!nowPlaying) {
        nowPlaying = lazer::NowPlayingOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
        nowPlaying->setID("now-playing"_spr);
        this->addChild(nowPlaying, 18);
    }
    if (m_fields->account) m_fields->account->close();
    nowPlaying->toggle();
    if (nowPlaying->isOpen() && m_fields->ticker) m_fields->ticker->hide();
}
