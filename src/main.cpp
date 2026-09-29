#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "audio/MusicPlayer.hpp"
#include "audio/Sfx.hpp"
#include "integrations/LevelThumbnails.hpp"
#include "integrations/ModIntegrations.hpp"
#include "settings/Account.hpp"
#include "settings/SettingsContent.hpp"
#include "ui/core/Text.hpp"
#include "ui/core/Theme.hpp"
#include "ui/menu/AccountPanel.hpp"
#include "ui/menu/ButtonSystem.hpp"
#include "ui/menu/MenuBackground.hpp"
#include "ui/menu/NowPlayingOverlay.hpp"
#include "ui/menu/SideFlashes.hpp"
#include "ui/menu/SongTicker.hpp"
#include "ui/menu/Toolbar.hpp"
#include "ui/select/SongSelect.hpp"
#include "update/Updater.hpp"
#include "ui/core/MenuCursor.hpp"
#include "ui/overlays/AchievementsOverlay.hpp"
#include "ui/overlays/Dialog.hpp"
#include "ui/overlays/LevelListingOverlay.hpp"
#include "ui/overlays/QuestsOverlay.hpp"
#include "ui/overlays/LeaderboardsOverlay.hpp"
#include "ui/overlays/RewardsPopup.hpp"
#include "ui/overlays/SettingsOverlay.hpp"
#include "ui/overlays/StatsOverlay.hpp"
#include "ui/startup/IntroSequence.hpp"

#include <Geode/fmod/fmod.hpp>
#include <unordered_map>

using namespace geode::prelude;
using lazer::ButtonSystem;
namespace icon = lazer::icon;

namespace {
    // Set when a button leaves the menu, so coming back re-opens the menu it
    // was in (top level or a submenu). Initial = nothing to restore.
    ButtonSystem::State g_returnState = ButtonSystem::State::Initial;
    // The intro plays once, on the first menu after the game starts.
    bool g_introPlayed = false;
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

    // Vanilla menus whose buttons move into the toolbar. Mods often add buttons here too.
    constexpr std::array TOOLBAR_SOURCE_MENUS {
        "bottom-menu", "right-side-menu", "side-menu", "top-right-menu", "profile-menu",
    };
    // Everything else we hide.
    constexpr std::array HIDDEN_NODES {
        "main-menu", "main-title", "player-username", "social-media-menu",
        "more-games-menu", "close-menu",
    };

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

// GD's CreatorLayer is the old hub for everything online. Its pages are now
// reached from the button system and the toolbar, through a hidden instance
// (its handlers show GD's own screens and popups).
void creatorAction(void (CreatorLayer::*handler)(CCObject*)) {
    log::debug("Creator hub action");
    static Ref<CreatorLayer> layer;
    layer = CreatorLayer::create();
    if (!layer) return;
    // Handlers poke at sprites that only exist in the visible hub (the quests
    // badge, the vault door): give them stand-ins.
    for (auto sprite : {&layer->m_questsSprite, &layer->m_secretDoorSprite}) {
        if (!*sprite) {
            *sprite = CCSprite::create();
            layer->addChild(*sprite);
        }
    }
    // Handlers use the button that was clicked (quests reads it straight
    // away): hand them the hub's own button wired to the same handler.
    // Fallback: a stand-in menu button with an image (quests reads the
    // button's image to clear its "!" badge).
    auto menu = CCMenu::create();
    auto standIn = CCMenuItemSpriteExtra::create(CCSprite::create(), nullptr, nullptr);
    menu->addChild(standIn);
    layer->addChild(menu);
    CCObject* sender = standIn;
    auto target = static_cast<SEL_MenuHandler>(handler);
    std::function<CCMenuItem*(CCNode*)> find = [&](CCNode* node) -> CCMenuItem* {
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (auto item = typeinfo_cast<CCMenuItem*>(child); item && item->m_pfnSelector == target) return item;
            if (auto found = find(child)) return found;
        }
        return nullptr;
    };
    if (auto item = find(layer.data())) sender = item;
    (layer.data()->*handler)(sender);
}

// Buttons other mods add to the creator hub (GDDP's demon progression,
// BetterInfo's...): the hub is hidden, so they move to the toolbar. Found by
// their path of child indices, which is the same in every fresh hub.
struct CreatorModButton {
    std::vector<unsigned> path;
    std::string id;
    CCNode* image = nullptr; // the button's own image, in the scanned hub
};

// GD's own hub buttons (Geode's node IDs); anything else in a hub menu is a mod's.
constexpr std::array VANILLA_CREATOR_BUTTONS = {
    "create-button", "saved-button", "scores-button", "quests-button", "daily-button",
    "weekly-button", "event-button", "gauntlets-button", "featured-button", "lists-button",
    "paths-button", "map-packs-button", "search-button", "map-button", "versus-button",
    "exit-button", "back-button", "vault-button", "treasure-room-button", "secret-door-button", "leaderboards-button",
};

std::vector<CreatorModButton> scanCreatorModButtons(CreatorLayer* layer) {
    std::vector<CreatorModButton> found;
    std::vector<unsigned> path;
    std::function<void(CCNode*, bool)> walk = [&](CCNode* node, bool inMenu) {
        unsigned i = 0;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            path.push_back(i++);
            if (auto item = typeinfo_cast<CCMenuItem*>(child); item && inMenu) {
                std::string id = item->getID();
                bool vanilla = std::find(VANILLA_CREATOR_BUTTONS.begin(), VANILLA_CREATOR_BUTTONS.end(), id) != VANILLA_CREATOR_BUTTONS.end();
                if (!vanilla && item->isVisible()) {
                    CCNode* image = nullptr;
                    if (auto sprite = typeinfo_cast<CCMenuItemSprite*>(item)) image = sprite->getNormalImage();
                    found.push_back({path, id, image});
                }
            } else if (child->isVisible()) {
                walk(child, inMenu || typeinfo_cast<CCMenu*>(child));
            }
            path.pop_back();
        }
    };
    walk(layer, false);
    return found;
}

// Presses a mod's hub button in a fresh hidden hub (its handler may use the hub).
void creatorModAction(std::vector<unsigned> const& path) {
    static Ref<CreatorLayer> layer;
    layer = CreatorLayer::create();
    if (!layer) return;
    CCNode* node = layer;
    for (auto i : path) {
        auto children = node->getChildren();
        if (!children || i >= children->count()) return;
        node = static_cast<CCNode*>(children->objectAtIndex(i));
    }
    if (auto item = typeinfo_cast<CCMenuItem*>(node)) item->activate();
}

void showScene(CCScene* scene) {
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

// Set by "new level": leaving that level's page goes back to the create menu
// rather than GD's "my levels" list.
bool g_newLevelFlow = false;

class $modify(LazerLevelBrowser, LevelBrowserLayer) {
    static CCScene* scene(GJSearchObject* search) {
        if (g_newLevelFlow && search && search->m_searchType == SearchType::MyLevels
            && Mod::get()->getSettingValue<bool>("enabled")) {
            g_newLevelFlow = false;
            g_returnState = ButtonSystem::State::Create;
            return MenuLayer::scene(false);
        }
        return LevelBrowserLayer::scene(search);
    }
};

// Screens that go "back" to CreatorLayer come back to the menu instead.
class $modify(LazerCreatorLayer, CreatorLayer) {
    static CCScene* scene() {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return CreatorLayer::scene();
        // Your levels and lists go back to the create buttons, even after the
        // editor or a level page in between.
        if (lazer::LevelListingOverlay::backToCreate()) {
            lazer::LevelListingOverlay::backToCreate() = false;
            lazer::SongSelect::browsingOnline() = false;
            g_returnState = ButtonSystem::State::Create;
            return MenuLayer::scene(false);
        }
        // GD's online screens opened from song select go back there.
        if (lazer::SongSelect::browsingOnline()) {
            lazer::SongSelect::browsingOnline() = false;
            return lazer::SongSelect::scene();
        }
        if (g_returnState == ButtonSystem::State::Initial) g_returnState = ButtonSystem::State::TopLevel;
        log::debug("CreatorLayer::scene -> menu (return {})", static_cast<int>(g_returnState));
        return MenuLayer::scene(false);
    }
};

class $modify(LazerMenuLayer, MenuLayer) {
    struct Fields {
        ButtonSystem* buttons = nullptr;
        lazer::Toolbar* toolbar = nullptr;
        lazer::MenuBackground* background = nullptr;
        lazer::SettingsOverlay* settings = nullptr;
        lazer::QuestsOverlay* quests = nullptr;
        lazer::LeaderboardsOverlay* leaderboards = nullptr;
        lazer::AchievementsOverlay* achievements = nullptr;
        lazer::StatsOverlay* stats = nullptr;
        lazer::NowPlayingOverlay* nowPlaying = nullptr;
        lazer::AccountPanel* account = nullptr;
        lazer::SongTicker* ticker = nullptr;
        int backgroundRequest = 0; // newest thumbnail request; older results are dropped
    };

    bool init() {
        auto mod = Mod::get();
        bool intro = !g_introPlayed && mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("intro");
        g_introPlayed = true;
        // GD starts the menu music during init: hold the first song for the intro.
        if (intro) lazer::MusicPlayer::get().holdForIntro();

        if (!MenuLayer::init()) return false;
        // Not on the Geode index: look for updates on GitHub (even with the Lazer menu off).
        lazer::updater::onMenu(this, intro ? 4.f : 1.f);
        g_newLevelFlow = false;
        // Back at the menu: gameplay no longer returns to song select.
        lazer::SongSelect::returnsHere() = false;
        lazer::SongSelect::browsingOnline() = false;
        if (!mod->getSettingValue<bool>("enabled")) return true;

        for (auto id : HIDDEN_NODES) {
            // Hide (not remove) vanilla nodes so other mods hooking them keep working.
            if (auto node = this->getChildByID(id)) node->setVisible(false);
        }

        this->setupBackground();

        using State = ButtonSystem::State;
        // A button that leaves the menu, remembering which menu to come back to.
        auto leave = [](State from, auto fn) {
            return [from, fn] {
                g_returnState = from;
                fn();
            };
        };
        auto creator = [leave](State from, void (CreatorLayer::*handler)(CCObject*)) {
            return leave(from, [handler] { creatorAction(handler); });
        };

        // The ButtonSystem* is only known after create(); submenu buttons reach it through the fields.
        auto open = [this](State state) {
            return [this, state] { if (m_fields->buttons) m_fields->buttons->setState(state); };
        };

        constexpr ccColor3B PLAY_SUB {94, 63, 186};
        constexpr ccColor3B CREATE_SUB {220, 160, 0};
        constexpr ccColor3B BROWSE_SUB {140, 180, 0};
        auto defaultSound = lazer::sfx::sound::MENU_DEFAULT_SELECT;

        std::vector<ButtonSystem::ButtonDef> defs {
            {"settings", icon::GEAR, {85, 85, 85}, [this] { this->toggleSettings(); }, false, defaultSound, State::TopLevel, true},

            {"play", icon::PLAY, {102, 68, 204}, open(State::Play), false, lazer::sfx::sound::MENU_PLAY_SELECT},
            {"create", icon::PEN, {238, 170, 0}, open(State::Create), false, lazer::sfx::sound::MENU_PLAY_SELECT},
            {"browse", icon::COMPASS, {165, 204, 0}, open(State::Browse), false},
            {"icons", icon::SHIRT, {0, 160, 200}, leave(State::TopLevel, [this] { this->onGarage(nullptr); })},
            {"exit", icon::CIRCLE_XMARK, {238, 51, 153}, [this] { this->onQuit(this); }, false},

            // play: everything you can play right away
            // Song select, one per kind of level: RobTop's levels and your saved ones in one list.
            {"classic", icon::CUBE, {102, 68, 204}, leave(State::Play, [] {
                showScene(lazer::SongSelect::scene(lazer::levels::Kind::Classic));
            }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
            {"platformer", icon::RUNNING, {102, 68, 204}, leave(State::Play, [] {
                showScene(lazer::SongSelect::scene(lazer::levels::Kind::Platformer));
            }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
            {"daily", icon::CALENDAR_DAY, PLAY_SUB, [] { creatorAction(&CreatorLayer::onDailyLevel); }, false, defaultSound, State::Play},
            {"gauntlets", icon::FIST, PLAY_SUB, creator(State::Play, &CreatorLayer::onGauntlets), true, defaultSound, State::Play},
            {"map packs", icon::BOXES, PLAY_SUB, creator(State::Play, &CreatorLayer::onMapPacks), true, defaultSound, State::Play},

            // create: your own levels
            {"my levels", icon::FOLDER_OPEN, {238, 170, 0}, creator(State::Create, &CreatorLayer::onMyLevels), true, defaultSound, State::Create},
            {"new level", icon::SQUARE_PLUS, CREATE_SUB, leave(State::Create, [] {
                g_newLevelFlow = true;
                showScene(EditLevelLayer::scene(GameLevelManager::get()->createNewLevel()));
            }), true, defaultSound, State::Create},
            {"my lists", icon::LIST, CREATE_SUB, leave(State::Create, [] {
                showScene(LevelBrowserLayer::scene(GJSearchObject::create(SearchType::MyLists)));
            }), true, defaultSound, State::Create},

            // browse: other people's levels, as our listing pages over GD's hidden browser
            {"search", icon::SEARCH, {165, 204, 0}, leave(State::Browse, [] {
                showScene(lazer::LevelListingOverlay::searchScene(""));
            }), true, defaultSound, State::Browse},
            {"featured", icon::STAR, BROWSE_SUB, creator(State::Browse, &CreatorLayer::onFeaturedLevels), true, defaultSound, State::Browse},
            {"lists", icon::LAYERS, BROWSE_SUB, creator(State::Browse, &CreatorLayer::onTopLists), true, defaultSound, State::Browse},
            {"hall of fame", icon::AWARD, BROWSE_SUB, leave(State::Browse, [] {
                showScene(lazer::LevelListingOverlay::pageScene(SearchType::HallOfFame));
            }), true, defaultSound, State::Browse},
            {"magic", icon::WAND_MAGIC, BROWSE_SUB, leave(State::Browse, [] {
                showScene(lazer::LevelListingOverlay::pageScene(SearchType::Magic));
            }), true, defaultSound, State::Browse},
            {"recent", icon::CLOCK, BROWSE_SUB, leave(State::Browse, [] {
                showScene(lazer::LevelListingOverlay::pageScene(SearchType::Recent));
            }), true, defaultSound, State::Browse},
        };
        // Levels sent for a rating: GD only shows that list to players with rating power.
        if (GameManager::get()->m_hasRP.value() > 0) {
            defs.push_back({"sent", icon::PAPER_PLANE, BROWSE_SUB, leave(State::Browse, [] {
                showScene(lazer::LevelListingOverlay::pageScene(SearchType::Sent));
            }), true, defaultSound, State::Browse});
        }
        auto buttons = ButtonSystem::create(std::move(defs));
        buttons->setID("button-system"_spr);
        this->addChild(buttons, 10);
        m_fields->buttons = buttons;

        auto toolbar = lazer::Toolbar::create();
        toolbar->setID("toolbar"_spr);
        this->addChild(toolbar, 20);
        m_fields->toolbar = toolbar;

        toolbar->addLeft({lazer::makeIcon(icon::GEAR, 1), "settings", [this] { this->toggleSettings(); }});
        // Home closes whatever is open (like osu!'s CloseAllOverlays); with nothing open, back one menu.
        toolbar->addLeft({lazer::makeIcon(icon::HOUSE, 1), "home", [this, buttons] {
            if (!this->closeAllOverlays()) buttons->back();
        }});

        buttons->setStateCallback([toolbar](ButtonSystem::State state) {
            // Back in a menu: nothing left to restore on the next menu load.
            if (state != ButtonSystem::State::EnteringMode) g_returnState = ButtonSystem::State::Initial;
            // osu! shows the toolbar once the logo lands in the button bar.
            if (state == ButtonSystem::State::Initial) toolbar->hide();
            else toolbar->show();
        });

        // Song ticker at the top right, under the toolbar.
        auto win = CCDirector::sharedDirector()->getWinSize();
        float k = lazer::unitScale();
        auto ticker = lazer::SongTicker::create(k);
        ticker->setPosition({win.width - 15 * k, win.height - toolbar->height() - 5 * k});
        this->addChild(ticker, 12);
        m_fields->ticker = ticker;

        // Follow the music: new song -> ticker + that level's thumbnail as the background.
        this->addChild(lazer::MusicListener::create([this](auto track, auto) { this->onTrackChanged(track); }));
        if (lazer::MusicPlayer::get().isActive()) this->onTrackChanged(lazer::MusicPlayer::get().current());

        // Collect vanilla + mod buttons next frame, after other mods' MenuLayer hooks ran.
        Loader::get()->queueInMainThread([self = Ref(this)] {
            static_cast<LazerMenuLayer*>(self.data())->collectToolbarButtons();
        });

        log::debug("Menu init: return {}", static_cast<int>(g_returnState));
        if (g_returnState != ButtonSystem::State::Initial) {
            buttons->resume(g_returnState);
            g_returnState = ButtonSystem::State::Initial;
        }

        if (intro) {
            auto sequence = lazer::IntroSequence::create(buttons->logoRadius(), [this] {
                // The ticker ran behind the intro: show it again now it can be seen.
                auto& player = lazer::MusicPlayer::get();
                if (m_fields->ticker && player.isActive()) m_fields->ticker->show(player.current());
            });
            sequence->setID("intro"_spr);
            this->addChild(sequence, 1000);
        }
        return true;
    }

    void onQuit(CCObject* sender) {
#ifdef GEODE_IS_ANDROID
        // The back button: see keyBackClicked below.
        if (!sender && this->lazerBack()) return;
#endif
        if (!Mod::get()->getSettingValue<bool>("enabled")) return MenuLayer::onQuit(sender);
        if (g_exiting || lazer::Dialog::isOpen()) return;
        // osu!'s ConfirmExitDialog in place of GD's quit popup.
        Ref<MenuLayer> self = this;
        lazer::Dialog::show(icon::TRIANGLE_EXCLAMATION, "Are you sure you want to exit Geometry Dash?", "Last chance to turn back", {
            {"Let me out!", lazer::Dialog::Kind::Ok, [self] { static_cast<LazerMenuLayer*>(self.data())->quitGame(); }},
            {"Just a little more...", lazer::Dialog::Kind::Cancel, nullptr},
        });
    }

    // Quits, after osu!'s outro (see you next time) when the intro is on.
    void quitGame() {
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
    void quitLikeGD() {
        auto alert = FLAlertLayer::create(nullptr, "", "", "", nullptr);
        if (!alert) return this->endGame();
        alert->setTag(0);
        MenuLayer::FLAlert_Clicked(alert, true);
    }

    void setupBackground() {
        auto mod = Mod::get();
        auto source = this->getChildByID("main-menu-bg");
        if (!source) return;

        auto bg = lazer::MenuBackground::create(
            source,
            mod->getSettingValue<int64_t>("background-dim") / 100.f,
            mod->getSettingValue<bool>("background-blur"),
            mod->getSettingValue<bool>("background-triangles")
        );
        bg->setID("background"_spr);
        // Beat flashes at the screen edges, over the background.
        bg->addChild(lazer::SideFlashes::create(), 3);
        m_fields->background = bg;
        // Draw right after GD's background, before everything else at the same z.
        this->addChild(bg, source->getZOrder());
        bg->setOrderOfArrival(source->getOrderOfArrival());
    }

    void collectToolbarButtons() {
        auto toolbar = m_fields->toolbar;
        if (!toolbar) return;

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
                toolbar->addRight({iconNode, tooltip, action});
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
        toolbar->addRight({lazer::makeIcon(icon::RANKING_STAR, 1), "leaderboards", [this] { this->toggleLeaderboards(); }});
        toolbar->addRight({lazer::makeIcon(icon::LIST_CHECK, 1), "quests", [this] { this->toggleQuests(); }});
        toolbar->addRight({lazer::makeIcon(icon::ROUTE, 1), "paths", hub(&CreatorLayer::onPaths)});
        toolbar->addRight({lazer::makeIcon(icon::CALENDAR_WEEK, 1), "weekly demon", hub(&CreatorLayer::onWeeklyLevel)});
        toolbar->addRight({lazer::makeIcon(icon::BOLT, 1), "event level", hub(&CreatorLayer::onEventLevel)});
        toolbar->addRight({lazer::makeIcon(icon::VAULT, 1), "vault", hub(&CreatorLayer::onSecretVault)});
        toolbar->addRight({lazer::makeIcon(icon::DUNGEON, 1), "treasure room", hub(&CreatorLayer::onTreasureRoom)});

        // Other mods' creator hub buttons.
        if (auto scanned = Ref(CreatorLayer::create())) {
            for (auto& button : scanCreatorModButtons(scanned)) {
                log::debug("Creator hub mod button: {}", button.id);
                auto iconNode = button.image ? lazer::snapshotNode(button.image) : nullptr;
                toolbar->addRight({iconNode, button.id.empty() ? "" : prettyId(button.id), [this, path = button.path] {
                    g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
                    creatorModAction(path);
                }});
            }
        }

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

    void toggleSettings() {
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
        }
    }

    // The chests are a dialog over whatever's open, and open themselves.
    void toggleRewards() {
        if (lazer::Dialog::isOpen()) return;
        lazer::showRewards();
    }

    void toggleLeaderboards() {
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
        }
    }

    void toggleQuests() {
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
        }
    }

    void toggleAchievements() {
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
        }
    }

    void toggleStats() {
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
        }
    }

    // Full-screen overlays replace each other, like osu!'s.
    void closeOverlaysExcept(CCNode* keep) {
        auto& f = m_fields;
        if (f->settings && f->settings != keep) f->settings->close();
        if (f->quests && f->quests != keep) f->quests->close();
        if (f->leaderboards && f->leaderboards != keep) f->leaderboards->close();
        if (f->achievements && f->achievements != keep) f->achievements->close();
        if (f->stats && f->stats != keep) f->stats->close();
    }

    // Every overlay and popup card. Returns whether any was open.
    bool closeAllOverlays() {
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
        closeIf(f->achievements);
        closeIf(f->stats);
        closeIf(f->nowPlaying);
        closeIf(f->account);
        return closed;
    }

    void onTrackChanged(lazer::MusicPlayer::Track const* track) {
        auto nowPlaying = m_fields->nowPlaying;
        if (m_fields->ticker && !(nowPlaying && nowPlaying->isOpen())) m_fields->ticker->show(track);

        if (!m_fields->background) return;
        int request = ++m_fields->backgroundRequest;
        if (!track) {
            m_fields->background->setImage(nullptr);
            return;
        }
        // No thumbnail for any of the song's levels: keep GD's own menu scene.
        Ref<MenuLayer> self = this;
        auto show = [self, request](CCTexture2D* texture) {
            auto layer = static_cast<LazerMenuLayer*>(self.data());
            if (layer->m_fields->backgroundRequest != request || !layer->m_fields->background) return;
            layer->m_fields->background->setImage(texture);
        };
        if (track->songID < 0) {
            // A main level's song: that level's screenshot (level N uses audio track N - 1).
            lazer::thumbnails::fetchOfficial(-track->songID, show);
            return;
        }
        lazer::thumbnails::fetchFirst(track->levelIDs(), [show](CCTexture2D* texture, int) { show(texture); });
    }

    void toggleNowPlaying() {
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

    // Escape closes overlays, then collapses the button bar (like osu!), then GD's quit prompt.
    // Returns whether it handled the key.
    bool lazerBack() {
        if (g_exiting) return true;
        if (m_fields->nowPlaying && m_fields->nowPlaying->back()) return true;
        if (m_fields->account && m_fields->account->back()) return true;
        if (m_fields->settings && m_fields->settings->back()) return true;
        if (m_fields->quests && m_fields->quests->back()) return true;
        if (m_fields->leaderboards && m_fields->leaderboards->back()) return true;
        if (m_fields->achievements && m_fields->achievements->back()) return true;
        if (m_fields->stats && m_fields->stats->back()) return true;
        if (m_fields->buttons && m_fields->buttons->back()) return true;
        return false;
    }

    // GD's Enter and Space open its level select. Here they press the logo, like osu!'s Select.
    void keyDown(enumKeyCodes key, double timestamp) {
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
    void keyBackClicked() {
        if (this->lazerBack()) return;
        MenuLayer::keyBackClicked();
    }
#endif
};
