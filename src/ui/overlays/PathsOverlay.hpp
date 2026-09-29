#pragma once

#include "../core/Easing.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer {

class ButtonRow;

// GD's Paths as an osu!-style page: the ten paths down the left (their art,
// rank and progress, which one is active), and the chosen one on the right:
// its ten ranks as a segmented bar, one action (unlock it with GD's purchase
// popup, make it the active path, or claim its chest with GD's reward popup)
// and every rank's reward as a card, dimmed until it's reached.
//
// Everything is read from GameStatsManager the way GD's own page reads it:
// a path's points are its stat (30 + path - 1, capped at 1000, a rank per
// hundred), a path is unlocked as GJItem `path`, its price is the store item
// for that, and rank rewards come from the "geometry.ach.pathXX.YY" achievements.
class PathsOverlay : public WaveOverlay, public GJPurchaseDelegate {
public:
    static PathsOverlay* create(float topInset);

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void didPurchaseItem(GJStoreItem* item) override;

protected:
    static constexpr int PATHS = 10, RANKS = 10, POINTS_PER_RANK = 100;

    struct Row {
        int path = 0;
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        RoundedBox* accent = nullptr;
        cocos2d::CCSprite* icon = nullptr;
        cocos2d::CCLabelBMFont* name = nullptr;
        cocos2d::CCLabelBMFont* sub = nullptr;
        RoundedBox* barFill = nullptr;
        RoundedBox* activeTag = nullptr;
        float barWidth = 0;
        bool hovered = false;
    };

    struct Card {
        int rank = 0;                // 0 = unlocking the path, 1..10, 11 = the chest
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        int itemID = 0;
        UnlockType itemType = UnlockType::Cube;
        bool hovered = false;
    };

    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;

    void buildList();
    void buildDetail();
    void refresh(bool force);
    void refreshRow(Row& row);
    void refreshDetail();
    void rebuildCards();
    void select(int path);

    // GD's numbers.
    int points(int path) const;
    int rank(int path) const { return std::min(RANKS, points(path) / POINTS_PER_RANK); }
    bool unlocked(int path) const;
    bool active(int path) const;
    bool chestClaimed(int path) const;
    GJStoreItem* storeItem(int path) const;
    std::string pathName(int path) const;
    std::string signature() const;

    void unlock();
    void activate();
    void claim();
    void playUnlocked();

    int m_selected = 1;
    float m_pad = 0;
    float m_listWidth = 0;
    std::vector<Row> m_rows;
    Row* m_pressedRow = nullptr;
    Card* m_pressedCard = nullptr;
    std::string m_signature;
    float m_sinceCheck = 0;

    // The detail panel.
    cocos2d::CCNode* m_detail = nullptr;
    float m_detailX = 0, m_detailWidth = 0;
    cocos2d::CCSprite* m_bigIcon = nullptr;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_pointsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    std::vector<RoundedBox*> m_segments;
    std::vector<RoundedBox*> m_segmentFills;
    float m_segmentWidth = 0;
    ButtonRow* m_action = nullptr;
    float m_actionY = 0;
    float m_cardsTop = 0;
    std::vector<Card> m_cards;
    Tweened<float> m_iconPop {1.f};
    float m_openedAt = 0, m_time = 0;
};

} // namespace lazer
