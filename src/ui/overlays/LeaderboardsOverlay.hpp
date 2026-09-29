#pragma once

#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer {

// GD's leaderboards as osu!'s rankings overlay: tabs for top 100 / friends /
// global / creators, a sort by stars, moons, demons or user coins, and a
// table of players: rank, icon and name (opens their profile), every stat
// with the sorted one highlighted, and their Better Progression level.
// Scores come straight from GameLevelManager, cached the way GD's page
// caches them; rows are built as they scroll into view.
class LeaderboardsOverlay : public WaveOverlay, public LeaderboardManagerDelegate {
public:
    static LeaderboardsOverlay* create(float topInset);
    ~LeaderboardsOverlay() override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

    void loadLeaderboardFinished(cocos2d::CCArray* scores, char const* key) override;
    void loadLeaderboardFailed(char const* key) override;

protected:
    // A clickable text (tab) or pill (sort, refresh).
    struct Chip {
        cocos2d::CCNode* node;
        cocos2d::CCLabelBMFont* label;
        RoundedBox* bg;  // pills only
        int value;
        bool hovered = false;
    };

    // A table column: caption, width (osu! pixels) and what it shows.
    enum class Column { Rank, Player, Stars, Moons, Demons, Diamonds, Coins, UserCoins, CreatorPoints, Level };
    struct ColumnDef {
        Column column;
        float width;
        float x = 0; // laid out, GD units
    };

    struct Row {
        geode::Ref<GJUserScore> score;
        int rank = 0;
        cocos2d::CCNode* node = nullptr; // built on first view
        RoundedBox* bg = nullptr;
        bool mine = false;
    };

    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;
    void onClosed() override;

    void buildTabs();
    void buildPills();
    void buildHeader();
    void updateHeaderColours();
    void layoutColumns();
    void selectType(LeaderboardType type);
    void selectStat(LeaderboardStat stat);
    void load(bool refresh);
    void showScores(cocos2d::CCArray* scores);
    void setStatus(std::string const& text);
    void layoutVisibleRows();
    cocos2d::CCNode* buildRow(Row& row);
    std::string key() const;
    Column sortedColumn() const;
    bool needsAccount() const;

    LeaderboardType m_type = LeaderboardType::Top100;
    LeaderboardStat m_stat = LeaderboardStat::Stars;
    std::string m_loadingKey;      // request in flight
    std::vector<Row> m_rows;
    std::vector<ColumnDef> m_columns;
    std::vector<cocos2d::CCLabelBMFont*> m_headerLabels;
    cocos2d::CCNode* m_header = nullptr;
    cocos2d::CCLabelBMFont* m_sortCaption = nullptr;
    float m_playerWidth = 0;     // GD units, after the leftover width

    float m_pad = 0;
    float m_topHeight = 0;
    float m_rowHeight = 0, m_rowGap = 0;
    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    cocos2d::CCLabelBMFont* m_status = nullptr;

    std::vector<Chip> m_tabs;
    std::vector<Chip> m_pills;     // sort pills, then refresh
    RoundedBox* m_tabUnderline = nullptr;
    Tweened<float> m_underlineX {0.f};
    Tweened<float> m_underlineW {0.f};
    Chip* m_pressedChip = nullptr;
    Row* m_hoveredRow = nullptr;
    Row* m_pressedRow = nullptr;
};

} // namespace lazer
