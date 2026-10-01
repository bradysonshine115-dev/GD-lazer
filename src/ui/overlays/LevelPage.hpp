#pragma once

#include "../core/Easing.hpp"
#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lazer {

// An online level's page, after osu!'s beatmap set overlay (osu.Game/
// Overlays/BeatmapSetOverlay + Overlays/BeatmapSet/): the level's picture
// across the top with what it is (its rating, difficulty and stars, song,
// downloads and likes) and what you can do with it (play, heart, like,
// comments, add to a list), then its description, its details, your
// progress, and its scores (loaded on request, since asking GD for them
// also uploads your best).
//
// It sits over GD's own LevelInfoLayer, which stays hidden underneath and
// keeps doing the work: downloading the level, playing it, liking and
// favouriting, its lists picker and profile page. The hooks in
// LevelPageHooks.cpp hand its events here. "GD's page" shows the real
// thing (its rating buttons, other mods' additions).
class LevelPage : public WaveOverlay, public LeaderboardManagerDelegate {
public:
    // Whether GD's page for this level gets ours (not its daily and weekly
    // pages, which have their own furniture).
    static bool wants(GJGameLevel* level, bool challenge);
    static LevelPage* create(LevelInfoLayer* owner);
    ~LevelPage() override;

    // From the hooks: the level's download ended (well or not), or its
    // numbers changed (a like, a favourite).
    void levelChanged();
    void downloadFailed();
    // Leaves the page the way GD's own back does.
    void goBack();
    bool leaving() const { return m_leaving; }
    // Puts GD's own page in front, for what this one doesn't do.
    void showVanilla();
    bool vanillaShown() const { return m_vanilla; }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

    // LeaderboardManagerDelegate (the scores).
    void loadLeaderboardFinished(cocos2d::CCArray* scores, char const* key) override;
    void loadLeaderboardFailed(char const* key) override;
    void updateUserScoreFinished() override {}
    void updateUserScoreFailed() override {}

protected:
    enum class Board { Hidden, Loading, Loaded, Failed };

    // A tappable thing: a filled or outlined button, a tab, a plain row.
    struct Button {
        enum class Kind { Filled, Ghost, Tab };
        Kind kind = Kind::Ghost;
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        cocos2d::ccColor4B color {};
        cocos2d::ccColor4B hoverColor {};
        cocos2d::ccColor4B activeColor {};
        std::function<void()> action;
        std::function<bool()> active;
        std::vector<cocos2d::CCLabelBMFont*> tinted;
        cocos2d::ccColor3B textColor {255, 255, 255};
        cocos2d::ccColor3B textHover {255, 255, 255};
        cocos2d::ccColor3B textActive {255, 255, 255};
        Tweened<float> hover {0.f};
        bool enabled = true;
        bool hovered = false;
    };

    bool init(LevelInfoLayer* owner);
    void onUpdate(float dt) override;

    // The top band: the picture, the facts and the actions. Built once.
    void buildHero();
    // Everything under it, rebuilt when the level changes.
    void rebuildSections();
    float buildDescription(float y, float x, float w);
    float buildDetails(float y, float x, float w);
    float buildProgress(float y, float x, float w);
    float buildScores(float y);
    // The actions' state: play's caption while the level downloads, the heart.
    void updateActions();
    // Flows the actions under the facts (again, when play's caption grows).
    float layoutHeroButtons();
    void play();
    // Presses one of GD's own buttons (its handlers read their sender): false when it isn't there.
    bool pressGD(char const* menuID, char const* buttonID);
    bool hasGD(char const* menuID, char const* buttonID) const;
    void loadScores(LevelLeaderboardType type);
    void requestThumbnail();

    Button& addButton(std::vector<Button>& list, cocos2d::CCNode* parent, Button::Kind kind, char const* glyph,
                      std::string const& text, float height, cocos2d::CCPoint pos, cocos2d::CCPoint anchor,
                      std::function<void()> action);
    Button* buttonAt(cocos2d::CCPoint world);
    void updateButton(Button& b, bool hovered, float dt);
    // Another of our pages (comments, a profile) or a GD popup is over this one.
    bool covered() const;

    LevelInfoLayer* m_owner = nullptr;
    geode::Ref<GJGameLevel> m_level;
    bool m_leaving = false;
    bool m_vanilla = false;
    bool m_playWhenReady = false;   // play was pressed while the level downloaded
    bool m_downloadFailed = false;
    float m_copiedMs = 0;           // "copied" shows on the ID button this long

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    float m_pad = 0;
    float m_heroHeight = 0;
    float m_heroButtonsX = 0, m_heroButtonsY = 0; // where the actions start
    cocos2d::CCNode* m_hero = nullptr;
    RoundedBox* m_cover = nullptr;      // the picture, dimmed, across the band
    RoundedBox* m_thumb = nullptr;      // and sharp, at the left
    cocos2d::CCLabelBMFont* m_thumbFallback = nullptr;
    bool m_thumbShimmer = true;
    float m_shimmerPhase = 0;
    cocos2d::CCLabelBMFont* m_playLabel = nullptr;
    cocos2d::CCLabelBMFont* m_playIcon = nullptr;
    cocos2d::CCLabelBMFont* m_heartIcon = nullptr;
    cocos2d::CCLabelBMFont* m_heartLabel = nullptr;
    cocos2d::CCLabelBMFont* m_idLabel = nullptr;
    cocos2d::CCLabelBMFont* m_likesLabel = nullptr;
    cocos2d::CCLabelBMFont* m_downloadsLabel = nullptr;
    size_t m_playButton = SIZE_MAX;     // index into m_heroButtons
    size_t m_heartButton = SIZE_MAX;
    size_t m_songButton = SIZE_MAX;
    size_t m_listButton = SIZE_MAX;
    cocos2d::CCNode* m_sections = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;
    float m_sectionsHeight = 0;

    Board m_board = Board::Hidden;
    LevelLeaderboardType m_boardType = LevelLeaderboardType::Global;
    geode::Ref<cocos2d::CCArray> m_scores;

    std::vector<Button> m_heroButtons;
    std::vector<Button> m_sectionButtons;
    Button* m_pressed = nullptr;
    std::shared_ptr<char> m_alive;      // thumbnail callbacks check it
};

} // namespace lazer
