#pragma once

#include "../core/Easing.hpp"
#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// GD's shops (GJShopLayer: the Shopkeeper's, Scratch's secret shop, Potbor's
// community shop, the Mechanic's and the diamond shop) as an osu!-style
// full-screen page: the shop and its keeper in the header with what you have
// to spend, then the items as cards in a grid (after osu!'s beatmap cards):
// GD's own art for each, its price, and whether you own it or can afford it.
//
// GD's shop stays underneath, hidden, and does the work: a card taps the
// item's own (hidden) button, so GD's purchase popup, its checks and its
// saving are GD's; the shopkeeper's lines (GD's dialogues) show over the
// page. Other mods' buttons on the shop are gathered into the page, and
// backing out goes through GD's own back.
class ShopOverlay : public WaveOverlay {
public:
    // Whether this shop gets the page (all but the paths, which aren't a shop page).
    static bool wants(GJShopLayer* shop);
    // Puts the page over `shop` (just built) and hides GD's. Null when GD's
    // items couldn't be found: the shop then stays GD's own.
    static ShopOverlay* attach(GJShopLayer* shop);

    // Leaves through GD's back (to wherever the shop was opened from).
    void goBack();
    bool leaving() const { return m_leaving; }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

protected:
    // One of GD's store items (GameStatsManager's GJStoreItem) on sale here.
    struct Item {
        geode::Ref<GJStoreItem> item;
        int index = 0;      // GD's store index (what it saves as bought)
        int id = 0;         // the icon, colour... number
        UnlockType type = UnlockType::Cube;
        int price = 0;
    };

    // A tappable thing: a card or a strip button.
    struct Pill {
        enum class Kind { Button, Card };
        Kind kind = Kind::Button;
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        cocos2d::ccColor4B color {};       // resting fill
        cocos2d::ccColor4B hoverColor {};
        std::function<void()> action;
        Tweened<float> hover {0.f};        // cards: background transition
        bool hovered = false;
    };

    struct Card {
        cocos2d::CCNodeRGBA* root = nullptr;
        Tweened<float> appear {0.f};
    };

    bool init(GJShopLayer* shop);
    void onUpdate(float dt) override;

    // GD's item buttons in the hidden shop, and the store items behind them.
    void readItems();
    // GD's button for an item, found again each time (GD may rebuild them).
    cocos2d::CCMenuItem* findButton(Item const& item);
    // Hides GD's nodes (never removes them) and stops them taking touches.
    void hideVanilla();
    bool vanillaShown();
    bool keepsShowing(cocos2d::CCNode* child);
    void takeModButtons();
    // Keeps GD's shopkeeper dialogue over the page.
    void raiseDialogs();
    bool dialogOpen();

    int balance();
    bool owned(Item const& item);
    // What the cards show (balance and what's owned): rebuilt when it changes.
    std::string signature();
    void updateBalance();
    void rebuildCards();
    void addCard(Item const& item, size_t index, bool fade);
    void select(Item const& item);

    float buildStrip(float y);
    // A strip button, placed right to left from m_stripX.
    Pill& addStripButton(cocos2d::CCNode* content, cocos2d::CCSize size, float radius);
    Pill& addPill(std::vector<Pill>& list, cocos2d::CCNode* parent, cocos2d::CCSize size, float radius,
                  cocos2d::CCPoint pos, cocos2d::CCPoint anchor, cocos2d::ccColor4B color,
                  cocos2d::ccColor4B hoverColor, std::function<void()> action);
    Pill* pillAt(cocos2d::CCPoint world);
    void updatePill(Pill& p, bool hovered, float dt);

    GJShopLayer* m_shop = nullptr;
    cocos2d::CCNode* m_backdrop = nullptr;
    std::vector<Item> m_items;
    std::string m_currencyKey;          // GD's stat for what this shop takes
    bool m_diamonds = false;
    std::string m_keeper;
    std::string m_signature;
    bool m_leaving = false;
    bool m_scanned = false;             // other mods' buttons gathered

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    float m_pad = 0;
    cocos2d::CCNode* m_list = nullptr;  // the cards
    float m_cardsTop = 0;
    float m_cardW = 0, m_cardH = 0;
    int m_columns = 1;
    float m_stripX = 0, m_stripY = 0;   // next strip button's right edge, the strip's centre
    std::vector<Card> m_cards;
    std::vector<Pill> m_fixedPills;     // the strip's buttons
    std::vector<Pill> m_cardPills;
    Pill* m_pressed = nullptr;

    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    RoundedBox* m_balanceBg = nullptr;
    cocos2d::CCNode* m_balanceIcon = nullptr;
    cocos2d::CCLabelBMFont* m_balanceLabel = nullptr;
    int m_shownBalance = -1;
};

} // namespace lazer
