#pragma once

#include "../../levels/LevelLibrary.hpp"
#include "../../levels/MapPacks.hpp"
#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/ScrollArea.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/robtop/mouse_dispatcher/CCMouseDelegate.h>
#include <functional>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace lazer {

class MenuBackground;

// Play -> classic / platformer: every level of that kind you can play in one list,
// RobTop's and your saved ones, after osu!'s song select
// (osu.Game/Screens/Select/SongSelect.cs):
//   left:   the selected level's title wedge and details (progress, songs via
//           GD's own song widget, so Jukebox's controls appear too, per-level
//           options, leaderboard, description)
//   right:  a curved carousel of level panels, with search and filters on top
//   bottom: footer with back / random / level page / play
// The selected level's song previews and its thumbnail becomes the blurred
// background. Playing a level (or backing out of it) comes back here.
// (GD's CCLayer is already a CCMouseDelegate.)
class SongSelect : public cocos2d::CCLayer, public CustomSongDelegate, public LeaderboardManagerDelegate,
                   public LevelDownloadDelegate, public MusicDownloadDelegate {
public:
    // From the main menu: the menu's song keeps playing, with its level selected.
    static cocos2d::CCScene* scene(levels::Kind kind);
    // The kind last opened (where gameplay and level pages return to).
    static cocos2d::CCScene* scene();
    static SongSelect* create(levels::Kind kind, bool fromMenu = false);

    // Set while the player came from song select, so leaving gameplay or GD's
    // level page returns here instead of GD's own screens.
    static bool& returnsHere();
    // Set while the player went from song select to GD's online screens: going
    // back to GD's creator hub from them returns here (see main.cpp).
    static bool& browsingOnline();

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;
    void scrollWheel(float y, float x) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override { ccTouchEnded(touch, e); }

    // CustomSongDelegate (for the embedded song widget).
    void songIDChanged(int) override {}
    int getActiveSongID() override;
    gd::string getSongFileName() override { return ""; }
    LevelSettingsObject* getLevelSettings() override { return nullptr; }

    // LeaderboardManagerDelegate.
    void updateUserScoreFinished() override {}
    void updateUserScoreFailed() override {}
    void loadLeaderboardFinished(cocos2d::CCArray* scores, char const* key) override;
    void loadLeaderboardFailed(char const* key) override;

    // Downloads for a level that isn't ready to play (see the loader).
    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int response) override;
    void downloadSongFailed(int id, GJSongError error) override;
    void loadSongInfoFailed(int id, GJSongError error) override { downloadSongFailed(id, error); }

protected:
    // Saved first: most players mostly play online levels.
    enum class Group { Saved, Official, Liked };
    enum class Board { Hidden, Loading, Loaded, Failed };
    enum class LoaderPhase { In, Out, Pushed, Cancelling };
    // The level behind the loader: built while the card shows, entered at the push.
    enum class LevelLoad { Waiting, Queued, Loaded };

    struct Panel {
        size_t entry;
        cocos2d::CCNodeRGBA* root;
        RoundedBox* bg;
        RoundedBox* thumb;
        Tweened<float> active {0.f};
        Tweened<float> hover {0.f};
        Tweened<float> thumbAlpha {0.f};
        Tweened<float> appear {0.f};  // fade in once built
        bool hovered = false;
        bool seen = false;        // still in view this frame
        float visibleMs = 0;      // thumbnails load once a panel has settled in view
        bool thumbRequested = false;
        // Pack headers: the chevron turns as the pack opens, and the pictures
        // of every level in the pack sit side by side.
        cocos2d::CCLabelBMFont* chevron = nullptr;
        Tweened<float> expand {0.f};
        struct Tile {
            RoundedBox* box;
            int levelID;
            Tweened<float> alpha {0.f};
        };
        std::vector<Tile> tiles;
        // A pack's level has its own resting colour.
        cocos2d::ccColor4B base {36, 34, 44, 235};
    };

    struct Button {
        cocos2d::CCNode* node;
        RoundedBox* bg;
        cocos2d::ccColor4B color;
        std::function<void()> action;
        Tweened<float> hover {0.f};
        bool hovered = false;
        bool selected = false; // lit tab
        ScrollArea* clip = nullptr; // inside a scroll area: only hit while visible in it
    };

    bool init(levels::Kind kind, bool fromMenu);
    // Selects a level using the song at `path`, whose song ID (MusicPlayer's) is
    // `songID` (clearing the filters if they hide it).
    bool selectSong(std::string const& path, int songID);
    void buildFilter();
    void buildFooter();
    Button& addButton(std::vector<Button>& list, cocos2d::CCNode* parent, char const* glyph, std::string const& label,
                      cocos2d::CCPoint pos, float height, cocos2d::ccColor4B color, std::function<void()> action,
                      float skew = 0.f);
    void applyFilter();
    // Reads the levels again (after deleting some) and re-applies the filters.
    void reloadEntries();
    // Map packs (Kind::MapPacks): the packs are the rows, and the open one has
    // its levels under it, like osu!'s beatmap sets and their difficulties.
    bool packMode() const { return m_kind == levels::Kind::MapPacks; }
    // m_entries from the loaded packs: each header followed by its levels.
    void rebuildPackEntries();
    // The pack list or a pack's levels arrived (or failed).
    void onPacksChanged();
    // Opens a pack (closing any other) and selects its first unbeaten level,
    // fetching its levels first when needed. -1 closes the open pack.
    void expandPack(int pack);
    // GD hands out the pack's stars and coins.
    void claimPack(int pack);
    // Selects the open pack's first unbeaten level (or its first).
    void selectPackLevel();
    // Opens the pack that was open last time, once the list is here, and
    // sees to the open pack's levels (fetched, or its first one selected).
    void restoreExpandedPack();
    // Row positions (rows differ in height in pack mode).
    void layoutRows();
    float rowHeight(size_t visibleIndex) const;
    // The selected entry, or null.
    levels::Entry const* selectedEntry() const;
    void select(size_t visibleIndex, bool scroll = true);
    void selectRandom();
    // Play: osu!'s PlayerLoader, then the level.
    // Plays the selected level. A saved level whose song isn't downloaded asks
    // first: download it (and its extra songs and SFX), or play without.
    void start();
    void play(bool withSong);
    bool needsDownloads(levels::Entry const& e) const;
    void buildLoader(levels::Entry const& e);
    void updateLoader(float dt);
    void cancelLoader();
    // Builds the level while the loader is up (osu!'s prepareNewPlayer).
    void loadLevel();
    // Drops a level that was built but never entered (cancelled).
    void dropLevel();
    // The preview after a cancelled play: back from its fade, or started again.
    void restorePreview();
    // A level without its data or song: fetch them behind the loader first.
    void startDownloads(levels::Entry const& e);
    void updateDownloads(float dt);
    void downloadFailed(char const* message);
    void stopListening();
    // Fades a node tree, relative to each node's opacity when first faded.
    void setTreeOpacity(cocos2d::CCNode* node, float factor);
    void openLevelPage();
    // The online search page, with the search text already searched.
    void browseOnline();
    void back();
    void toggleFolders();
    void closeFolders();
    void confirmDeleteUnhearted();
    void confirmDeleteLevel();
    void loadLeaderboard();
    // The selected saved level's comments, as the osu!-style page over song select.
    void openComments();
    void buildDetails(float top, float bottom);
    // Mirrors the hidden song widget into the song card.
    void updateSongCard();

    void updateCarousel(float dt);
    // The carousel's scroll limits: the first and last panels centred.
    std::pair<float, float> scrollRange() const;
    void updateScrollbar(float dt);
    bool scrollbarHit(cocos2d::CCPoint world) const;
    void dragScrollbar(cocos2d::CCPoint touch);
    // What the scrollbar's label says for the level in the middle of the view:
    // its initial, difficulty or progress, by the sort.
    std::string scrollbarText() const;
    Panel& makePanel(size_t entry);
    void updateWedge(bool animate = true);
    // Rebuilds the details for the same level, keeping the scroll position.
    void refreshDetails();
    void previewSong();
    float itemTop(size_t visibleIndex) const;
    float viewHeight() const;
    size_t panelAt(cocos2d::CCPoint world);
    Button* buttonAt(cocos2d::CCPoint world);
    bool hittable(Button const& b, cocos2d::CCPoint world);

    float m_k = 1;
    cocos2d::CCSize m_win;
    float m_footerH = 0;
    float m_leftW = 0, m_rightW = 0;
    float m_carouselTop = 0, m_carouselBottom = 0; // screen y
    float m_panelH = 0, m_spacing = 0;

    levels::Kind m_kind = levels::Kind::Classic;
    std::vector<levels::Entry> m_entries;
    std::vector<size_t> m_visible; // filtered + sorted entry indices
    size_t m_selected = 0;         // index into m_visible
    bool m_hasSelection = false;
    Group m_group = Group::Saved;
    int m_folder = 0;              // 0 = all folders
    levels::Sort m_sort = levels::Sort::Default;
    std::string m_query;
    int m_expandedPack = -1;          // pack index whose levels are shown
    std::vector<float> m_rowTops;     // top of each visible row, then the end
    // What was selected last, so a rebuild that lands on the same level
    // doesn't animate the wedge in again.
    struct SelectionKey {
        int id = -1;
        bool official = false;
        bool header = false;
        bool operator==(SelectionKey const&) const = default;
    } m_lastSelection;
    // The cursor comments on a fruitless search once you've stopped typing.
    float m_noResultsMs = -1;
    std::string m_saidFor;


    MenuBackground* m_background = nullptr;
    cocos2d::CCNode* m_carousel = nullptr;
    std::map<size_t, Panel> m_panels; // by visible index
    float m_scroll = 0, m_scrollTarget = 0;
    bool m_touchDown = false, m_dragging = false;
    cocos2d::CCPoint m_touchStart, m_touchLast;
    float m_dragVelocity = 0;

    // osu!'s OsuScrollbar on the carousel's right edge.
    RoundedBox* m_bar = nullptr;
    float m_barLength = 0, m_barY = 0; // bar length and centre (screen y)
    bool m_barDragging = false;
    float m_barGrab = 0;              // touch y minus bar centre when grabbed
    bool m_barHovered = false;
    Tweened<float> m_barWidth {1.f};     // x the resting width: wider while held
    Tweened<float> m_barPull {0.f};      // sideways rubber-band offset
    cocos2d::CCNode* m_barLabel = nullptr;
    RoundedBox* m_barLabelBg = nullptr;
    cocos2d::CCLabelBMFont* m_barLabelText = nullptr;
    std::string m_barText;
    Tweened<float> m_barLabelAlpha {0.f};
    Tweened<float> m_barHover {0.f};     // Gray8 -> GrayF
    Tweened<float> m_barHighlight {0.f}; // -> Highlight1 while held

    cocos2d::CCNode* m_wedge = nullptr;     // title + details, rebuilt on selection
    Tweened<float> m_wedgeAlpha {0.f};
    geode::TextInput* m_search = nullptr;
    bool m_searchEnabled = true;    // off while an overlay covers song select
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCNode* m_loadingSpinner = nullptr;        // in the wedge while the packs load
    cocos2d::CCLabelBMFont* m_sortLabel = nullptr;
    std::vector<Button> m_tabs;
    std::vector<Button> m_buttons; // footer + sort
    std::vector<Button> m_wedgeButtons; // heart, level options, leaderboard (rebuilt per level)
    std::vector<Button> m_folderItems;  // the open folder dropdown
    Button* m_pressed = nullptr;
    size_t m_folderButton = 0;           // index in m_buttons
    cocos2d::CCLabelBMFont* m_folderLabel = nullptr;
    cocos2d::CCNode* m_folderMenu = nullptr;
    ScrollArea* m_details = nullptr;
    ScrollDragger m_detailsDrag;
    CustomSongWidget* m_songWidget = nullptr;
    struct SongCard {
        cocos2d::CCLabelBMFont* title = nullptr;
        cocos2d::CCLabelBMFont* artist = nullptr;
        cocos2d::CCLabelBMFont* info = nullptr;
        RoundedBox* track = nullptr;
        RoundedBox* fill = nullptr;
        float textW = 0, barW = 0;
        float buttonX = 0, buttonY = 0;
        std::vector<size_t> buttons; // indices in m_wedgeButtons
        size_t download = SIZE_MAX, cancel = SIZE_MAX, getInfo = SIZE_MAX, jukebox = SIZE_MAX;
        size_t more = SIZE_MAX, infoBtn = SIZE_MAX, remove = SIZE_MAX;
    } m_songCard;
    Board m_board = Board::Hidden;
    int m_boardLevel = 0;               // level the leaderboard was loaded for
    geode::Ref<cocos2d::CCArray> m_boardScores;
    bool m_starting = false;
    bool m_refreshPending = false;

    // The loader shown between pressing play and the level.
    cocos2d::CCNode* m_loader = nullptr;
    cocos2d::CCNode* m_loaderMeta = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;
    geode::Ref<GJGameLevel> m_loaderLevel;
    bool m_withSong = true;             // the loader also downloads the song
    bool m_downloading = false;         // the loader waits for the level's data and song
    bool m_downloadFailed = false;
    float m_failedMs = 0;
    cocos2d::CCLabelBMFont* m_loaderStatus = nullptr;
    RoundedBox* m_loaderFill = nullptr;
    float m_loaderBarW = 0;
    Tweened<float> m_downloadProgress {0.f};
    LevelLoad m_levelLoad = LevelLoad::Waiting;
    geode::Ref<cocos2d::CCScene> m_levelScene;
    Tweened<float> m_spinnerAlpha {1.f};
    Tweened<float> m_spinnerScale {1.f};
    LoaderPhase m_loaderPhase = LoaderPhase::In;
    float m_loaderMs = 0;
    std::vector<cocos2d::CCNode*> m_uiRoots;               // song select's own nodes, faded out
    std::unordered_map<cocos2d::CCNode*, GLubyte> m_baseOpacity;
    Tweened<float> m_uiAlpha {1.f};
    Tweened<float> m_loaderAlpha {0.f};
    Tweened<float> m_loaderScale {0.7f};
    Tweened<float> m_metaAlpha {0.f};
    Tweened<float> m_dimTween {0.55f};     // a details refresh waiting for the touch to end

    float m_previewDelay = -1;     // debounce before the selected song starts
    std::string m_previewPath;     // what's on the music channel
    bool m_leaving = false;        // back to the menu: the channel is the menu's again
    int m_backgroundRequest = 0;
    float m_enterMs = 0;
    float m_wheelClaimMs = 0;
};

} // namespace lazer
