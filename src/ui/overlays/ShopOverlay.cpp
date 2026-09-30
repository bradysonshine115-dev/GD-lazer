#include "ShopOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Quips.hpp"
#include "../core/Text.hpp"
#include "GameplayButtons.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJShopLayer.hpp>
#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu! sizes (768 px tall screen), scaled by m_k.
    constexpr float HORIZONTAL_PADDING = 20.f;  // the listing's cards and strip margin
    constexpr float STRIP_HEIGHT = 40.f;        // the listing's sort strip
    constexpr float HEADER_BUTTON = 20.f;       // HeaderButton
    constexpr float MOD_BUTTON = 28.f;          // other mods' buttons in the strip
    // Item cards: GD's art in a square-ish panel over the name and price, in
    // BeatmapCard's corner radius, spacing and transitions.
    constexpr float CARD_WIDTH = 150.f;
    constexpr float CARD_ART = 110.f;
    constexpr float CARD_TEXT = 70.f;
    constexpr float CARD_RADIUS = 8.f;          // BeatmapCard.CORNER_RADIUS
    constexpr float CARD_SPACING = 10.f;
    constexpr float CARDS_TOP = 15.f;
    constexpr float CARDS_BOTTOM = 20.f;
    constexpr float ITEM_ART = 64.f;            // GD's item art, fitted into this
    constexpr float CARD_TRANSITION = 360.f;    // BeatmapCard.TRANSITION_DURATION
    constexpr float CARD_FADE = 200.f;          // cards fade in, like the listing's

    constexpr ccColor4B CLEAR {0, 0, 0, 0};
    constexpr ccColor4B LIME1 {0xb3, 0xd9, 0x44, 255};   // OsuColour.Lime1: owned
    constexpr ccColor3B CANT_AFFORD {0xff, 0x66, 0x66};  // OsuColour.Red

    // The shops: their page title, keeper, and osu! colour scheme hue.
    struct ShopText { char const* title; char const* keeper; float hue; };
    ShopText textFor(ShopType type) {
        switch (type) {
            case ShopType::Secret: return {"secret shop", "Scratch", 255.f};     // Purple
            case ShopType::Community: return {"community shop", "Potbor", 125.f}; // Green
            case ShopType::Mechanic: return {"mechanic", "the Mechanic", 0.f};   // Red
            case ShopType::Diamond: return {"diamond shop", "the diamond shopkeeper", 200.f}; // Blue
            default: return {"shop", "the Shopkeeper", 45.f};                    // Orange
        }
    }

    // What an unlock is, in osu!'s lower-case words.
    std::string kindName(UnlockType type) {
        switch (type) {
            case UnlockType::Cube: return "cube";
            case UnlockType::Col1: return "colour";
            case UnlockType::Col2: return "second colour";
            case UnlockType::Ship: return "ship";
            case UnlockType::Ball: return "ball";
            case UnlockType::Bird: return "ufo";
            case UnlockType::Dart: return "wave";
            case UnlockType::Robot: return "robot";
            case UnlockType::Spider: return "spider";
            case UnlockType::Swing: return "swing";
            case UnlockType::Jetpack: return "jetpack";
            case UnlockType::Streak: return "trail";
            case UnlockType::Death: return "death effect";
            case UnlockType::ShipFire: return "ship fire";
            default: return "";
        }
    }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    bool nodeShown(CCNode* node) {
        for (auto n = node; n; n = n->getParent()) {
            if (!n->isVisible()) return false;
        }
        return true;
    }

    std::string withCommas(long long v) {
        auto s = fmt::format("{}", v);
        for (int i = int(s.size()) - 3; i > (s[0] == '-' ? 1 : 0); i -= 3) s.insert(size_t(i), ",");
        return s;
    }

    // Shrinks a label to fit `maxWidth`, cutting it with an ellipsis if it would get too small.
    void fit(CCLabelBMFont* label, float maxWidth) {
        float base = label->getScale();
        float w = label->getScaledContentSize().width;
        if (w <= maxWidth) return;
        if (w * 0.85f <= maxWidth) {
            label->setScale(base * maxWidth / w);
            return;
        }
        std::string text = label->getString();
        while (text.size() > 1 && label->getScaledContentSize().width > maxWidth) {
            text.pop_back();
            label->setString((text + "...").c_str());
        }
    }

    // Scales a node so its larger side is `size` (GD sprites vary a lot).
    void fitSquare(CCNode* node, float size) {
        auto s = node->getContentSize();
        float side = std::max(s.width, s.height);
        node->setScale(side > 0 ? size / side : 1.f);
    }

    // GD's own orb or diamond, as the shop shows next to prices.
    CCNode* currencyIcon(bool diamonds, float size) {
        auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
        constexpr std::array<char const*, 2> ORBS {"currencyOrbIcon_001.png", "currencyOrb_001.png"};
        constexpr std::array<char const*, 2> DIAMONDS {"currencyDiamondIcon_001.png", "GJ_diamondsIcon_001.png"};
        for (auto frame : diamonds ? DIAMONDS : ORBS) {
            if (!cache->spriteFrameByName(frame)) continue;
            auto sprite = CCSprite::createWithSpriteFrameName(frame);
            fitSquare(sprite, size);
            return sprite;
        }
        auto glyph = makeIcon(icon::GEM, size * 0.8f);
        glyph->setColor(diamonds ? ccColor3B {0x66, 0xcc, 0xff} : ccColor3B {0x88, 0x66, 0xee});
        return glyph;
    }

    // The first GD item art in a button's image.
    GJItemIcon* itemIconIn(CCNode* node, int depth = 0) {
        if (!node || depth > 4) return nullptr;
        if (auto icon = typeinfo_cast<GJItemIcon*>(node)) return icon;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (auto icon = itemIconIn(child, depth + 1)) return icon;
        }
        return nullptr;
    }

    // Every GD item button under `node`, in GD's order, with its art.
    void itemButtons(CCNode* node, std::vector<std::pair<CCMenuItemSpriteExtra*, GJItemIcon*>>& out) {
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (auto button = typeinfo_cast<CCMenuItemSpriteExtra*>(child)) {
                if (auto icon = itemIconIn(button->getNormalImage())) out.push_back({button, icon});
                continue;
            }
            itemButtons(child, out);
        }
    }

    // Nothing in the hidden shop may take touches: its page scroller would
    // otherwise still catch them (invisible) over ours.
    void deafen(CCNode* node) {
        if (typeinfo_cast<DialogLayer*>(node)) return;
        if (auto layer = typeinfo_cast<CCLayer*>(node)) layer->setTouchEnabled(false);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) deafen(child);
    }
}

bool ShopOverlay::wants(GJShopLayer* shop) {
    if (!shop || !Mod::get()->getSettingValue<bool>("enabled")) return false;
    switch (shop->m_type) {
        case ShopType::Normal:
        case ShopType::Secret:
        case ShopType::Community:
        case ShopType::Mechanic:
        case ShopType::Diamond:
            return true;
        default:
            return false;
    }
}

ShopOverlay* ShopOverlay::attach(GJShopLayer* shop) {
    auto ret = new ShopOverlay();
    if (!ret->init(shop)) {
        delete ret;
        return nullptr;
    }
    ret->autorelease();
    // A dark stage for the waves to rise over (FullscreenOverlay's Background6).
    auto backdrop = CCLayerColor::create(ret->m_scheme.background6());
    backdrop->setContentSize(CCDirector::get()->getWinSize());
    shop->addChild(backdrop, 99);
    ret->m_backdrop = backdrop;
    ret->setID("shop"_spr);
    shop->addChild(ret, 100);
    ret->hideVanilla();
    ret->open();
    return ret;
}

bool ShopOverlay::init(GJShopLayer* shop) {
    m_shop = shop;
    readItems();
    // No items found (another mod rebuilt the shop, or GD's layout changed): GD's shop.
    if (m_items.empty()) {
        log::warn("Shop: no items found in GD's shop, keeping GD's");
        return false;
    }
    auto text = textFor(shop->m_type);
    m_keeper = text.keeper;
    if (!WaveOverlay::init(0, theme::Scheme {text.hue}, icon::STORE, text.title, fmt::format("run by {}", text.keeper))) {
        return false;
    }
    m_pad = HORIZONTAL_PADDING * m_k;
    float k = m_k;

    // What you have to spend, at the header's right (GD's counter at the shop's top right).
    float pillH = 32 * k;
    m_balanceBg = RoundedBox::create({100 * k, pillH}, pillH / 2, m_scheme.background6());
    m_balanceBg->setAnchorPoint({1, 0.5f});
    auto headerSize = header()->getContentSize();
    m_balanceBg->setPosition({headerRight() - 16 * k, headerSize.height / 2});
    header()->addChild(m_balanceBg);
    m_balanceIcon = currencyIcon(m_diamonds, 18 * k);
    m_balanceBg->addChild(m_balanceIcon, 1);
    m_balanceLabel = makeText("0", Weight::Bold, 18 * k);
    m_balanceLabel->setAnchorPoint({0, 0.5f});
    m_balanceBg->addChild(m_balanceLabel, 1);

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);

    // As many cards as fit across, stretched to fill the row.
    float avail = bodySize().width - 2 * m_pad, spacing = CARD_SPACING * k;
    m_columns = std::max(1, static_cast<int>((avail + spacing) / (CARD_WIDTH * k + spacing)));
    m_cardW = (avail - (m_columns - 1) * spacing) / m_columns;
    m_cardH = (CARD_ART + CARD_TEXT) * k;

    m_cardsTop = buildStrip(0) + CARDS_TOP * k;
    m_list = CCNode::create();
    m_scroll->content()->addChild(m_list);

    m_signature = signature();
    updateBalance();
    for (size_t i = 0; i < m_items.size(); i++) addCard(m_items[i], i, true);
    int rows = static_cast<int>((m_items.size() + m_columns - 1) / m_columns);
    m_scroll->setContentHeight(m_cardsTop + rows * (m_cardH + spacing) + CARDS_BOTTOM * k);
    return true;
}

// --- GD's shop ---

void ShopOverlay::readItems() {
    auto gsm = GameStatsManager::sharedState();
    std::vector<std::pair<CCMenuItemSpriteExtra*, GJItemIcon*>> buttons;
    itemButtons(m_shop, buttons);
    for (auto [button, art] : buttons) {
        // GD keeps the store item on the button, or it's found from the art's unlock.
        auto item = typeinfo_cast<GJStoreItem*>(button->getUserObject());
        if (!item) {
            item = gsm->getStoreItem(art->m_unlockID, static_cast<int>(art->m_unlockType));
            // Only this shop's (another shop may sell the same unlock).
            if (item && item->m_shopType != m_shop->m_type) item = nullptr;
        }
        if (!item) continue;
        int index = item->m_index.value();
        bool seen = std::any_of(m_items.begin(), m_items.end(), [index](Item const& i) { return i.index == index; });
        if (seen) continue;
        Item entry;
        entry.item = item;
        entry.index = index;
        entry.id = item->m_typeID.value();
        entry.type = static_cast<UnlockType>(item->m_unlockType.value());
        entry.price = item->m_price.value();
        m_items.push_back(std::move(entry));
    }
    if (!m_items.empty()) {
        // GD's own key for the currency (orbs; diamonds in the diamond shop).
        m_currencyKey = m_items[0].item->getCurrencyKey();
        m_diamonds = m_shop->m_type == ShopType::Diamond;
    }
    log::debug("Shop: {} items", m_items.size());
}

CCMenuItem* ShopOverlay::findButton(Item const& item) {
    std::vector<std::pair<CCMenuItemSpriteExtra*, GJItemIcon*>> buttons;
    itemButtons(m_shop, buttons);
    for (auto [button, art] : buttons) {
        if (button->getUserObject() == item.item.data()) return button;
        if (art->m_unlockID == item.id && art->m_unlockType == item.type) return button;
    }
    return nullptr;
}

bool ShopOverlay::keepsShowing(CCNode* child) {
    // The page, and the shopkeeper's dialogue and GD's popups over it.
    return child == this || child == m_backdrop || typeinfo_cast<DialogLayer*>(child) || typeinfo_cast<FLAlertLayer*>(child);
}

void ShopOverlay::hideVanilla() {
    for (auto child : CCArrayExt<CCNode*>(m_shop->getChildren())) {
        if (keepsShowing(child)) continue;
        child->setVisible(false);
        deafen(child);
    }
}

bool ShopOverlay::vanillaShown() {
    for (auto child : CCArrayExt<CCNode*>(m_shop->getChildren())) {
        if (child->isVisible() && !keepsShowing(child)) return true;
    }
    return false;
}

void ShopOverlay::takeModButtons() {
    // The same as the pause menu: other mods' buttons in the shop's menus,
    // as small round buttons with their picture, leftwards in the strip.
    auto found = collectModButtons(m_shop);
    float k = m_k, size = MOD_BUTTON * k;
    for (auto item : found) {
        log::debug("Shop mod button: {}", item->getID().view());
        Ref<CCMenuItem> target = item;
        auto& pill = addStripButton(m_scroll->content(), {size, size}, size / 2);
        auto image = modButtonImage(item, size * 0.72f);
        image->setPosition({size / 2, size / 2});
        pill.node->addChild(image, 1);
        pill.action = [target] { target->activate(); };
    }
}

void ShopOverlay::raiseDialogs() {
    // GD adds the shopkeeper's lines to the shop layer: over the page, not under it.
    for (auto child : CCArrayExt<CCNode*>(m_shop->getChildren())) {
        if (typeinfo_cast<DialogLayer*>(child) && child->getZOrder() <= this->getZOrder()) {
            m_shop->reorderChild(child, this->getZOrder() + 10);
        }
    }
}

bool ShopOverlay::dialogOpen() {
    auto open = [](CCNode* parent) {
        if (!parent) return false;
        for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
            if (child->isVisible() && typeinfo_cast<DialogLayer*>(child)) return true;
        }
        return false;
    };
    return open(m_shop) || open(CCDirector::get()->getRunningScene());
}

int ShopOverlay::balance() {
    return GameStatsManager::sharedState()->getStat(m_currencyKey.c_str());
}

bool ShopOverlay::owned(Item const& item) {
    return GameStatsManager::sharedState()->isStoreItemUnlocked(item.index);
}

std::string ShopOverlay::signature() {
    std::string s = std::to_string(balance()) + ":";
    for (auto const& item : m_items) s += owned(item) ? '1' : '0';
    return s;
}

void ShopOverlay::select(Item const& item) {
    // Yours already: GD's item info (its name and who made it).
    if (owned(item)) {
        if (auto popup = ItemInfoPopup::create(item.id, item.type)) popup->show();
        return;
    }
    // GD's own button: GD checks the price, asks with its purchase popup, buys and saves.
    if (auto button = findButton(item)) {
        button->activate();
        return;
    }
    // Its button is gone: what the button does, through GD's pieces.
    if (balance() < item.price) {
        m_shop->showCantAffordMessage(item.item.data());
        return;
    }
    if (auto popup = PurchaseItemPopup::create(item.item.data())) {
        quips::say("shop", 0.5f);
        popup->m_delegate = m_shop;
        popup->show();
    }
}

void ShopOverlay::goBack() {
    if (m_leaving) return;
    m_leaving = true;
    // The close button already played WaveContainer's pop out.
    if (isOpen()) sfx::play(sfx::sound::WAVE_POP_OUT);
    // GD's back: the garage, the vault or wherever the shop was opened from.
    m_shop->onBack(nullptr);
}

// --- building ---

ShopOverlay::Pill& ShopOverlay::addPill(std::vector<Pill>& list, CCNode* parent, CCSize size, float radius,
                                        CCPoint pos, CCPoint anchor, ccColor4B color, ccColor4B hoverColor,
                                        std::function<void()> action) {
    auto node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint(anchor);
    node->setPosition(pos);
    parent->addChild(node, 1);
    auto bg = RoundedBox::create(size, radius, color);
    bg->setPosition({size.width / 2, size.height / 2});
    node->addChild(bg);

    Pill pill;
    pill.node = node;
    pill.bg = bg;
    pill.color = color;
    pill.hoverColor = hoverColor;
    pill.action = std::move(action);
    list.push_back(std::move(pill));
    m_pressed = nullptr; // the list may have moved
    return list.back();
}

ShopOverlay::Pill& ShopOverlay::addStripButton(CCNode* content, CCSize size, float radius) {
    auto& pill = addPill(m_fixedPills, content, size, radius, {m_stripX, m_stripY}, {1, 0.5f}, CLEAR,
                         m_scheme.background3(), nullptr);
    m_stripX -= size.width + 5 * m_k;
    return pill;
}

float ShopOverlay::buildStrip(float y) {
    float k = m_k, W = bodySize().width, h = STRIP_HEIGHT * k;
    auto content = m_scroll->content();
    auto bg = CCLayerColor::create(m_scheme.background4());
    bg->setContentSize({W, h});
    bg->setPosition({0, -(y + h)});
    content->addChild(bg);
    m_stripY = -(y + h / 2);
    m_stripX = W - m_pad;

    // How many items, and how many are yours (the listing's result count).
    m_countLabel = makeText(" ", Weight::SemiBold, 12 * k);
    m_countLabel->setColor(theme::rgb(m_scheme.content2()));
    m_countLabel->setAnchorPoint({0, 0.5f});
    m_countLabel->setPosition({m_pad, m_stripY});
    content->addChild(m_countLabel, 1);

    // HeaderButtons at the right: the shopkeeper talks (GD's lines, as when
    // you tap them in GD's shop), and the community shop's credits.
    auto button = [&](char const* glyph, std::string const& text, std::function<void()> action) {
        auto mark = makeIcon(glyph, 10 * k);
        auto label = makeText(text, Weight::SemiBold, 12 * k);
        float markW = mark->getScaledContentSize().width;
        float w = markW + 5 * k + label->getScaledContentSize().width + 20 * k;
        float ph = HEADER_BUTTON * k;
        auto& pill = addStripButton(content, {w, ph}, 3 * k);
        pill.action = std::move(action);
        mark->setPosition({10 * k + markW / 2, ph / 2});
        pill.node->addChild(mark, 1);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({10 * k + markW + 5 * k, ph / 2});
        pill.node->addChild(label, 1);
    };
    auto shop = m_shop;
    button(icon::COMMENTS, "talk", [shop] { shop->showReactMessage(); });
    if (shop->m_type == ShopType::Community) {
        button(icon::USERS, "credits", [shop] { shop->onCommunityCredits(shop); });
    }
    return y + h;
}

void ShopOverlay::updateBalance() {
    int value = balance();
    if (value != m_shownBalance) {
        m_shownBalance = value;
        float k = m_k;
        m_balanceLabel->setString(withCommas(value).c_str());
        float iconW = m_balanceIcon->getScaledContentSize().width;
        float w = 14 * k + iconW + 8 * k + m_balanceLabel->getScaledContentSize().width + 16 * k;
        auto size = m_balanceBg->getContentSize();
        m_balanceBg->setContentSize({w, size.height});
        m_balanceIcon->setPosition({14 * k + iconW / 2, size.height / 2});
        m_balanceLabel->setPosition({14 * k + iconW + 8 * k, size.height / 2});
    }

    int mine = 0;
    for (auto const& item : m_items) mine += owned(item) ? 1 : 0;
    auto count = fmt::format("{} items, {} owned", m_items.size(), mine);
    m_countLabel->setString(count.c_str());
}

void ShopOverlay::rebuildCards() {
    m_list->removeAllChildren();
    m_cards.clear();
    m_cardPills.clear();
    m_pressed = nullptr;
    for (size_t i = 0; i < m_items.size(); i++) addCard(m_items[i], i, false);
}

void ShopOverlay::addCard(Item const& item, size_t index, bool fade) {
    float k = m_k, w = m_cardW, h = m_cardH, r = CARD_RADIUS * k, art = CARD_ART * k;
    bool mine = owned(item);
    bool affordable = balance() >= item.price;

    size_t columns = static_cast<size_t>(m_columns);
    int col = static_cast<int>(index % columns), row = static_cast<int>(index / columns);
    float spacing = CARD_SPACING * k;
    auto root = CCNodeRGBA::create();
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(fade ? 0 : 255);
    root->setContentSize({w, h});
    root->setAnchorPoint({0, 1});
    root->setPosition({m_pad + col * (w + spacing), -(m_cardsTop + row * (h + spacing))});
    m_list->addChild(root);

    // BeatmapCardContent: the body in Background2 (Background4 while hovered).
    auto bg = RoundedBox::create({w, h}, r, m_scheme.background2());
    bg->setPosition({w / 2, h / 2});
    root->addChild(bg, 0);

    // The art panel (where a beatmap card has its cover), GD's own item on it.
    auto panel = RoundedBox::create({w, art}, r, m_scheme.background3());
    panel->setCornerRadii(r, r, 0, 0);
    panel->setPosition({w / 2, h - art / 2});
    root->addChild(panel, 1);
    if (auto gdArt = GJItemIcon::createBrowserItem(item.type, item.id)) {
        fitSquare(gdArt, ITEM_ART * k);
        gdArt->setPosition({w / 2, h - art / 2});
        // Out of reach for now: dimmed, like GD's shop greys what you can't buy.
        if (!mine && !affordable) gdArt->setOpacity(110);
        root->addChild(gdArt, 2);
    }
    // Owned: a check in the corner; can't afford yet: a lock.
    if (mine || !affordable) {
        float badge = 20 * k;
        auto box = RoundedBox::create({badge, badge}, badge / 2, mine ? LIME1 : m_scheme.background6());
        box->setCascadeOpacityEnabled(true);
        box->setPosition({w - 8 * k - badge / 2, h - 8 * k - badge / 2});
        root->addChild(box, 3);
        auto mark = makeIcon(mine ? icon::CHECK : icon::LOCK, 9 * k);
        mark->setColor(mine ? theme::rgb(m_scheme.background6()) : theme::rgb(m_scheme.foreground1()));
        mark->setPosition({badge / 2, badge / 2});
        box->addChild(mark);
    }

    // The name: what it is and its number (GD's own name for special items).
    float textX = 10 * k, textW = w - 20 * k;
    std::string kind = kindName(item.type);
    std::string name = kind.empty() ? std::string(ItemInfoPopup::nameForUnlockType(item.id, item.type))
                                    : fmt::format("{} {}", kind, item.id);
    if (name.empty()) name = fmt::format("item {}", item.id);
    auto title = makeText(name, Weight::SemiBold, 16 * k);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({textX, h - art - 20 * k});
    fit(title, textW);
    root->addChild(title, 2);

    // The price with GD's currency icon, or an "owned" pill (the listing's status pill).
    float py = 18 * k;
    if (mine) {
        auto label = makeText("owned", Weight::Bold, 10 * k);
        label->setColor(theme::rgb(m_scheme.background6()));
        float pw = label->getScaledContentSize().width + 10 * k, ph = 16 * k;
        auto pill = RoundedBox::create({pw, ph}, ph / 2, LIME1);
        pill->setCascadeOpacityEnabled(true);
        pill->setAnchorPoint({0, 0.5f});
        pill->setPosition({textX, py});
        root->addChild(pill, 2);
        label->setPosition({pw / 2, ph / 2});
        pill->addChild(label);
    } else {
        auto coin = currencyIcon(m_diamonds, 14 * k);
        float coinW = coin->getScaledContentSize().width;
        coin->setPosition({textX + coinW / 2, py});
        root->addChild(coin, 2);
        auto price = makeText(withCommas(item.price), Weight::Bold, 14 * k);
        price->setAnchorPoint({0, 0.5f});
        price->setPosition({textX + coinW + 5 * k, py});
        if (!affordable) price->setColor(CANT_AFFORD);
        root->addChild(price, 2);
    }

    Pill pill;
    pill.kind = Pill::Kind::Card;
    pill.node = root;
    pill.bg = bg;
    pill.color = m_scheme.background2();
    pill.hoverColor = m_scheme.background4();
    int itemIndex = item.index;
    pill.action = [this, itemIndex] {
        auto it = std::find_if(m_items.begin(), m_items.end(), [itemIndex](Item const& i) { return i.index == itemIndex; });
        if (it != m_items.end()) this->select(*it);
    };
    m_cardPills.push_back(std::move(pill));
    m_pressed = nullptr; // the list may have moved

    Card card;
    card.root = root;
    // New cards fade in, like the listing's; rebuilt ones (after buying) just change.
    card.appear.set(fade ? 0.f : 1.f);
    if (fade) card.appear.to(1.f, CARD_FADE, Easing::OutQuint);
    m_cards.push_back(std::move(card));
}

// --- per frame and input ---

void ShopOverlay::updatePill(Pill& p, bool hovered, float dt) {
    if (hovered && !p.hovered) sfx::hover(p.kind == Pill::Kind::Card ? sfx::sound::BUTTON_HOVER : sfx::sound::DEFAULT_HOVER);
    p.hovered = hovered;
    if (p.kind == Pill::Kind::Card) {
        // BeatmapCardContentBackground: Background2, Background4 while hovered.
        float target = hovered ? 1.f : 0.f;
        if (p.hover.target() != target) p.hover.to(target, CARD_TRANSITION, Easing::OutQuint);
        p.hover.update(dt);
        p.bg->setFillColor(theme::lerp(p.color, p.hoverColor, p.hover.get()));
        return;
    }
    p.bg->setFillColor(hovered ? p.hoverColor : p.color);
}

void ShopOverlay::onUpdate(float dt) {
    if (m_leaving) return;
    // The close button: leave right away (the waves keep dropping during the fade).
    if (!isOpen()) return goBack();

    // Once every mod has built its part of the shop: hide what they added
    // and gather their buttons.
    if (!m_scanned) {
        m_scanned = true;
        hideVanilla();
        takeModButtons();
        // GD's hidden scrollers may have taken the mouse wheel after us.
        m_scroll->claimWheel();
    }
    // GD shows its menus again after some of its steps (a dialogue closing):
    // hidden again, before they can take a touch.
    if (vanillaShown()) hideVanilla();
    raiseDialogs();

    // Bought something (GD saved it and took the price): the cards follow.
    auto now = signature();
    if (now != m_signature) {
        m_signature = now;
        updateBalance();
        rebuildCards();
    }

    for (auto& card : m_cards) {
        card.appear.update(dt);
        auto alpha = static_cast<GLubyte>(255 * std::clamp(card.appear.get(), 0.f, 1.f));
        if (card.root->getOpacity() != alpha) card.root->setOpacity(alpha);
    }

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !popupOnTop() && !dialogOpen() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    for (auto list : {&m_fixedPills, &m_cardPills}) {
        for (auto& p : *list) {
            bool hovered = interactive && p.action && nodeShown(p.node) && nodeContains(p.node, mouse);
            updatePill(p, hovered, dt);
        }
    }
}

ShopOverlay::Pill* ShopOverlay::pillAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    Pill* hit = nullptr;
    for (auto list : {&m_fixedPills, &m_cardPills}) {
        for (auto& p : *list) {
            if (p.action && nodeShown(p.node) && nodeContains(p.node, world)) hit = &p;
        }
    }
    return hit;
}

bool ShopOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (m_leaving) return false;
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    auto loc = touch->getLocation();
    m_pressed = pillAt(loc);
    m_drag.began(m_scroll, loc);
    return true;
}

void ShopOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void ShopOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (m_leaving || !pressed || !pressed->action || !nodeContains(pressed->node, touch->getLocation())) return;
    // HoverSampleSet.Button for cards; the strip's buttons use the default.
    sfx::click(pressed->kind == Pill::Kind::Card ? sfx::sound::BUTTON_SELECT : sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the cards, and the pill with them
    action();
}

} // namespace lazer

// GD's shops get the page. GJShopLayer stays in the scene underneath, hidden,
// and keeps doing the work: its item buttons buy, its dialogue plays, its
// back button leaves. Nothing is removed, so other mods' hooks on it still work.
class $modify(LazerShopLayer, GJShopLayer) {
    struct Fields {
        lazer::ShopOverlay* page = nullptr;
    };

    bool init(ShopType type) {
        if (!GJShopLayer::init(type)) return false;
        if (!lazer::ShopOverlay::wants(this)) return true;
        m_fields->page = lazer::ShopOverlay::attach(this);
        return true;
    }

    // GD's shopkeeper reacts to taps on the (hidden) shop: the page has them.
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        if (m_fields->page) return false;
        return GJShopLayer::ccTouchBegan(touch, event);
    }

    // GD's back (its button, Escape, Android's back): the page goes first with
    // its sound, then GD leaves the way it always does.
    void onBack(CCObject* sender) {
        auto page = m_fields->page;
        if (page && !page->leaving()) return page->goBack();
        GJShopLayer::onBack(sender);
    }
};
