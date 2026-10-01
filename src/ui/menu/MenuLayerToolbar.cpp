// The menu's toolbar: GD's buttons and other mods' (from the menus the Lazer
// menu hides, and from the creator hub) gathered onto its right side in a
// fixed order, and the user button with its account card.

#include "MenuLayerInternal.hpp"

#include "../../integrations/ModIntegrations.hpp"
#include "../../settings/Account.hpp"
#include "../core/Quips.hpp"
#include "../core/Text.hpp"

#include <Geode/Geode.hpp>
#include <unordered_map>

using namespace geode::prelude;
using lazer::ButtonSystem;
namespace icon = lazer::icon;

namespace {
    struct KnownButton {
        char const* icon;
        char const* tooltip;
    };
    // Vanilla buttons get proper icons; anything unknown (other mods) keeps its own sprite.
    std::unordered_map<std::string, KnownButton> const KNOWN_BUTTONS {
        {"achievements-button", {icon::TROPHY, "achievements"}},
        {"stats-button", {icon::CHART, "statistics"}},
        {"newgrounds-button", {icon::MUSIC, "now playing"}},
        {"daily-chest-button", {icon::GIFT, "daily chests"}},
        {"geode.loader/geode-button", {icon::PUZZLE, "mods"}},
        // Globed (multiplayer mod): its button just says "main menu" otherwise.
        {"dankmeme.globed2/main-menu-button", {icon::GLOBE, "multiplayer"}},
        {"joseii.ventilla/button", {icon::RADIO, "ventilla"}},
    };
    // Already covered elsewhere (button bar / user section), so not duplicated.
    constexpr std::array SKIPPED_BUTTONS {"settings-button", "profile-button"};

    std::string prettyId(std::string id) {
        // "some.mod/cool-button" -> "cool"
        if (auto slash = id.rfind('/'); slash != std::string::npos) id = id.substr(slash + 1);
        if (id.ends_with("-button")) id.resize(id.size() - 7);
        for (auto& c : id) if (c == '-' || c == '_') c = ' ';
        return id;
    }
}

// Right side of the toolbar, left to right: the player's pages first
// (achievements, statistics, leaderboards, quests, paths), then rewards and
// secrets, then the music player, then mods, then anything unknown.
int LazerMenuLayer::rightOrder(std::string const& id) {
    static std::unordered_map<std::string, int> const order {
        {"achievements-button", 0}, {"stats-button", 1}, {"leaderboards", 2}, {"quests", 3}, {"paths", 4},
        {"daily-chest-button", 10}, {"vault", 11}, {"treasure-room", 12},
        {"newgrounds-button", 20},
        {"geode.loader/geode-button", 30}, {"dankmeme.globed2/main-menu-button", 31},
    };
    auto it = order.find(id);
    return it != order.end() ? it->second : 40;
}

void LazerMenuLayer::collectToolbarButtons() {
    auto toolbar = m_fields->toolbar;
    if (!toolbar) return;
    // Gathered first, then added in a fixed order (see rightOrder).
    std::vector<std::pair<int, lazer::Toolbar::Item>> right;
    auto add = [&](std::string const& id, lazer::Toolbar::Item item) {
        right.push_back({rightOrder(id), std::move(item)});
    };

    for (auto menuId : TOOLBAR_SOURCE_MENUS) {
        auto menu = this->getChildByID(menuId);
        if (!menu) continue;

        for (auto item : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
            if (!typeinfo_cast<CCMenuItem*>(item) || !item->isVisible()) continue;
            std::string id = item->getID();
            if (std::find(SKIPPED_BUTTONS.begin(), SKIPPED_BUTTONS.end(), id) != SKIPPED_BUTTONS.end()) continue;

            CCNode* iconNode = nullptr;
            std::string tooltip;
            if (auto known = KNOWN_BUTTONS.find(id); known != KNOWN_BUTTONS.end()) {
                iconNode = lazer::makeIcon(known->second.icon, 1);
                tooltip = known->second.tooltip;
            } else {
                // Another mod's button: keep its own look.
                if (auto sprite = typeinfo_cast<CCMenuItemSprite*>(item)) {
                    if (auto normal = sprite->getNormalImage()) iconNode = lazer::snapshotNode(normal);
                }
                tooltip = id.empty() ? "" : prettyId(id);
            }

            Ref<CCMenuItem> target = item;
            std::function<void()> action = [target] { target->activate(); };
            // Daily chests get our own overlay instead of GD's popup.
            if (id == "daily-chest-button") action = [this] { this->toggleRewards(); };
            if (id == "achievements-button") action = [this] { this->toggleAchievements(); };
            if (id == "stats-button") action = [this] { this->toggleStats(); };
            // The music button opens the player (GD's song browser is in settings > audio).
            if (id == "newgrounds-button" && Mod::get()->getSettingValue<bool>("music-player")) {
                action = [this] { this->toggleNowPlaying(); };
            }
            add(id, {iconNode, tooltip, action});
        }
        menu->setVisible(false);
    }

    // The rest of GD's creator hub.
    auto hub = [this](void (CreatorLayer::*handler)(CCObject*)) {
        return [this, handler] {
            g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
            creatorAction(handler);
        };
    };
    add("leaderboards", {lazer::makeIcon(icon::RANKING_STAR, 1), "leaderboards", [this] { this->toggleLeaderboards(); }});
    add("quests", {lazer::makeIcon(icon::LIST_CHECK, 1), "quests", [this] { this->toggleQuests(); }});
    add("paths", {lazer::makeIcon(icon::ROUTE, 1), "paths", [this] { this->togglePaths(); }});
    add("vault", {lazer::makeIcon(icon::VAULT, 1), "vault", [hub] {
        lazer::quips::say("vault", 0.6f);
        hub(&CreatorLayer::onSecretVault)();
    }});
    add("treasure-room", {lazer::makeIcon(icon::DUNGEON, 1), "treasure room", [hub] {
        lazer::quips::say("treasure", 0.6f);
        hub(&CreatorLayer::onTreasureRoom)();
    }});

    // Other mods' creator hub buttons.
    if (auto scanned = Ref(CreatorLayer::create())) {
        for (auto& button : scanCreatorModButtons(scanned)) {
            log::debug("Creator hub mod button: {}", button.id);
            auto iconNode = button.image ? lazer::snapshotNode(button.image) : nullptr;
            add(button.id, {iconNode, button.id.empty() ? "" : prettyId(button.id), [this, path = button.path] {
                g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
                creatorModAction(path);
            }});
        }
    }
    std::stable_sort(right.begin(), right.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [_, item] : right) toolbar->addRight(std::move(item));

    // Profile: the vanilla button lives in profile-menu (or main-menu on some setups).
    CCMenuItem* profile = nullptr;
    for (auto menuId : {"profile-menu", "main-menu"}) {
        if (auto menu = this->getChildByID(menuId)) {
            if (auto p = typeinfo_cast<CCMenuItem*>(menu->getChildByID("profile-button"))) profile = p;
        }
    }
    // The user button opens our account card; GD's profile page is one of its items.
    Ref<CCMenuItem> profileRef = profile;
    auto panel = lazer::AccountPanel::create(toolbar->height(), {
        [this, profileRef] {
            // The profile page replaces whatever is open, like osu!'s overlays.
            this->closeAllOverlays();
            if (profileRef) profileRef->activate();
        },
        [this] {
            g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
            this->onGarage(nullptr);
        },
    });
    panel->setID("account"_spr);
    this->addChild(panel, 18);
    m_fields->account = panel;
    float avatar = toolbar->height() * 0.62f;
    toolbar->setUser(lazer::account::username(), lazer::integrations::playerIcon(false, avatar * 0.62f), [this] {
        if (m_fields->nowPlaying) m_fields->nowPlaying->close();
        m_fields->account->toggle();
    });
}
