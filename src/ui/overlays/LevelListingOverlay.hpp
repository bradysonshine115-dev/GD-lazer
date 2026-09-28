#pragma once

#include "../core/Easing.hpp"
#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace lazer {

// GD's online level lists as osu!'s beatmap listing (osu.Game/Overlays/
// BeatmapListingOverlay + Overlays/BeatmapListing/): a titled page of level
// cards (Beatmaps/Drawables/Cards/BeatmapCardNormal) that loads the next page
// as you scroll. The search page has osu!'s search box, filter rows and sort
// tabs on top; the other pages (featured, hall of fame, magic, recent...) are
// just the cards under their title.
//
// It sits over GD's own LevelBrowserLayer, which stays hidden underneath and
// does the work: it asks GameLevelManager for pages and gets the results as
// its LevelManagerDelegate; the hooks at the end of the .cpp hand them here.
class LevelListingOverlay : public WaveOverlay, public TextInputDelegate {
public:
    // Whether GD's browser for this search gets this page (its online lists;
    // the local ones, "my levels" and so on, keep GD's own screen).
    static bool wants(GJSearchObject* search);
    // GD's browser scene for one of the online lists, shown as its own titled
    // page even for the quick-search types (magic, recent...).
    static cocos2d::CCScene* pageScene(SearchType type);
    // The search page, with `query` already searched (empty: most downloaded).
    static cocos2d::CCScene* searchScene(std::string const& query);

    static LevelListingOverlay* create(LevelBrowserLayer* owner, GJSearchObject* search);

    // GD's LevelManagerDelegate calls, from the LevelBrowserLayer hooks.
    void levelsLoaded(cocos2d::CCArray* items, char const* key);
    void levelsFailed(char const* key);
    void pageInfo(std::string const& info, char const* key);
    // Leaves the page for where the player came from (the menu or song select).
    void goBack();
    bool leaving() const { return m_leaving; }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

    // TextInputDelegate (the search box).
    void textChanged(CCTextInputNode* node) override;
    void enterPressed(CCTextInputNode* node) override;

protected:
    enum class Mode { Search, Plain };
    enum class State { Loading, Loaded, Failed };
    // The filter rows of the search page (BeatmapSearchFilterRow).
    enum class Row { Difficulty, Demon, Length, General, Played, Type, Count };

    // A tappable thing: a card, a filter tab, a sort tab or a rounded button.
    struct Pill {
        enum class Kind { Button, FilterTab, SortTab, Card };
        Kind kind = Kind::Button;
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        cocos2d::ccColor4B color {};          // resting fill
        cocos2d::ccColor4B hoverColor {};
        std::function<void()> action;
        std::function<bool()> active;         // tabs: selected
        std::function<bool()> usable;         // tabs: greyed out otherwise
        cocos2d::CCLabelBMFont* label = nullptr;     // regular weight
        cocos2d::CCLabelBMFont* boldLabel = nullptr; // shown instead while active
        RoundedBox* activeBg = nullptr;       // multi-select tabs: the white pill
        cocos2d::CCNode* activeIcon = nullptr;
        std::vector<cocos2d::CCLabelBMFont*> tinted; // buttons: recoloured on hover
        cocos2d::ccColor3B textColor {255, 255, 255};
        cocos2d::ccColor3B textHover {255, 255, 255};
        Tweened<float> hover {0.f};           // cards: background transition
        int tag = 0;
        bool enabled = true;
        bool hovered = false;
        bool wasActive = false;
    };

    // One result: a level or a list.
    struct Card {
        geode::Ref<cocos2d::CCObject> item;
        int thumbLevel = 0;            // level whose thumbnail to show (a list's first)
        cocos2d::CCNodeRGBA* root = nullptr;
        RoundedBox* thumb = nullptr;
        size_t pill = 0;               // index into m_cardPills
        Tweened<float> appear {0.f};
        float top = 0;                 // from the top of the cards area
        bool thumbRequested = false;
    };

    bool init(LevelBrowserLayer* owner, GJSearchObject* search);
    void onUpdate(float dt) override;

    // Reads the query, filters and sort GD's search object carries.
    void readSearch(GJSearchObject* search);
    // The search object for `page` of what the controls say.
    GJSearchObject* makeSearch(int page);
    // Asks GD for a page (replacing what's shown when `fresh`).
    void request(GJSearchObject* search, bool fresh);
    // Drops the results and loads the first page of the current controls.
    void startSearch();
    void loadMore();
    void refresh();
    void openItem(cocos2d::CCObject* item);

    // Building: the top part once, cards as they arrive, the footer per state.
    float buildSearchControl(float y);
    float buildFilterRow(Row row, float y, float x, float width);
    float buildStrip(float y);
    void rebuildSort();
    void addCard(cocos2d::CCObject* item);
    void layoutCards();
    void rebuildFooter();
    void clearCards();
    Pill& addPill(std::vector<Pill>& list, cocos2d::CCNode* parent, cocos2d::CCSize size, float radius,
                  cocos2d::CCPoint pos, cocos2d::CCPoint anchor, cocos2d::ccColor4B color,
                  cocos2d::ccColor4B hoverColor, std::function<void()> action);
    Pill* pillAt(cocos2d::CCPoint world);
    // Hover / active looks; true when a filter tab's active state changed (its row re-flows).
    bool updatePill(Pill& p, bool hovered, float dt);
    // Re-flows a filter row's tabs (active multi-select tabs take more room).
    void layoutRow(Row row);
    bool rowUsable(Row row) const;
    bool optionActive(Row row, int option) const;
    void toggleOption(Row row, int option);
    void requestThumbnail(Card& card);
    bool nearView(float top, float height) const;
    void queueSearch(float delayMs);

    LevelBrowserLayer* m_owner = nullptr;
    Mode m_mode = Mode::Plain;
    State m_state = State::Loading;
    geode::Ref<GJSearchObject> m_current;   // the page in flight or last loaded
    std::string m_key;                      // GD's key for the request in flight
    std::string m_lastKey;                  // and for the page that last arrived
    float m_loadingMs = 0;
    bool m_more = true;                     // another page may exist
    int m_total = -1;                       // items on the server (-1: not known)
    int m_pendingTotal = -1, m_pendingEnd = -1; // from pageInfo, for the page in flight
    bool m_leaving = false;
    bool m_dirty = false;
    float m_searchDelay = -1;               // ms until the queued search runs (-1: none)

    // Search page controls.
    std::string m_query;
    SearchType m_sort = SearchType::Downloaded; // the quick search when there's no query
    uint32_t m_difficulty = 0;              // bits: NA, easy, normal, hard, harder, insane, demon, auto
    int m_demon = 0;                        // 0 any, 1-5 easy..extreme
    uint32_t m_length = 0;                  // bits: tiny, short, medium, long, XL, platformer
    uint32_t m_general = 0;                 // bits: see GENERAL in the .cpp
    int m_played = 0;                       // 0 any, 1 uncompleted, 2 completed
    bool m_lists = false;

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    float m_pad = 0;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCNode* m_sortHolder = nullptr;   // the sort strip's tabs, rebuilt
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCNode* m_list = nullptr;         // the cards
    cocos2d::CCNode* m_footer = nullptr;       // spinner / messages after them
    float m_cardsTop = 0;
    float m_cardW = 0, m_cardH = 0;
    int m_columns = 1;
    std::vector<Card> m_cards;
    std::unordered_set<int> m_seen;            // level / list IDs shown
    std::vector<Pill> m_fixedPills;            // search control and strip
    std::vector<Pill> m_sortPills;             // rebuilt with the strip's tabs
    std::vector<Pill> m_cardPills;
    std::vector<Pill> m_footerPills;
    std::vector<std::vector<size_t>> m_rowPills; // per Row: indices into m_fixedPills
    std::vector<float> m_rowX, m_rowY, m_rowW;   // per Row: where its tabs flow
    Pill* m_pressed = nullptr;
    std::vector<cocos2d::CCNode*> m_spinners;
    std::shared_ptr<char> m_alive;             // thumbnail callbacks check it
};

} // namespace lazer
