#include "Dialog.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "SettingsRows.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr float WIDTH = 500;          // DialogOverlay: dialogs are 500 wide
    constexpr float CORNER = 20;
    constexpr float RING = 100, RING_MIN = 20, RING_BORDER = 5, ICON = 50;
    constexpr float HEADER_SIZE = 25, BODY_SIZE = 18;
    constexpr float BUTTON_HEIGHT = 50, BUTTON_SPACING = 5;
    constexpr float IDLE_WIDTH = 0.8f, HOVER_WIDTH = 0.9f; // DialogButton
    constexpr float SHEAR = 0.2f;         // OsuGame.SHEAR
    constexpr float ENTER_MS = 500, EXIT_MS = 500;
    constexpr float DANGEROUS_HOLD_MS = 500; // HoldToConfirmContainer.DANGEROUS_HOLD_ACTIVATION_DELAY
    constexpr ccColor4B BACKGROUND {0x22, 0x1a, 0x21, 255};
    constexpr ccColor4B PINK {0xff, 0x66, 0xaa, 255};
    constexpr ccColor4B BLUE {0x66, 0xcc, 0xff, 255};
    constexpr ccColor4B RED3 {0xcc, 0x33, 0x33, 255};

    int g_open = 0;

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    void setOpacityDeep(CCNode* node, GLubyte o, CCNode* skip) {
        // Lists hold rows that set their own opacities, and so does a panel:
        // they're shown or hidden instead.
        if (node == skip || typeinfo_cast<ScrollArea*>(node)) return;
        if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(node)) rgba->setOpacity(o);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) setOpacityDeep(child, o, skip);
    }

    ccColor4B colourFor(Dialog::Kind kind) {
        switch (kind) {
            case Dialog::Kind::Cancel: return BLUE;
            case Dialog::Kind::Dangerous: return RED3;
            default: return PINK;
        }
    }
}

bool Dialog::isOpen() { return g_open > 0; }

float Dialog::listWidth(float width) {
    float k = unitScale();
    auto win = CCDirector::get()->getWinSize();
    return std::min(width * k, win.width - 40 * k) - 60 * k;
}

Dialog* Dialog::show(char const* icon, std::string const& header, std::string const& body, std::vector<Button> buttons) {
    Content content;
    content.icon = icon;
    content.header = header;
    content.body = body;
    content.buttons = std::move(buttons);
    return show(std::move(content));
}

Dialog* Dialog::show(Content content) {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return nullptr;
    auto ret = new Dialog();
    if (!ret->init(std::move(content))) {
        delete ret;
        return nullptr;
    }
    ret->autorelease();
    scene->addChild(ret, 1000);
    return ret;
}

bool Dialog::init(Content content) {
    if (!CCLayer::init()) return false;
    g_open++;
    m_k = unitScale();
    auto win = CCDirector::get()->getWinSize();

    m_dim = CCLayerColor::create({0, 0, 0, 0});
    this->addChild(m_dim);
    // The content node scales around the card's centre.
    m_content = CCNode::create();
    m_content->setPosition(win / 2);
    this->addChild(m_content);

    build(std::move(content));

    m_dimAlpha.to(0.5f, ENTER_MS, Easing::OutQuint);
    m_alpha.to(1, ENTER_MS, Easing::OutQuint);
    m_scale.to(1, 750, Easing::OutElasticHalf);
    m_ringSize.to(RING, ENTER_MS * 1.5f, Easing::OutQuint);
    sfx::play(sfx::sound::DIALOG_POP_IN);

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    this->scheduleUpdate();
    update(0);
    return true;
}

void Dialog::setContent(Content content) {
    if (m_closing) return;
    build(std::move(content));
    // A small pop, so the change is noticed.
    m_scale.set(0.96f);
    m_scale.to(1, 500, Easing::OutElasticHalf);
    m_iconScale.set(0.6f);
    m_iconScale.to(1, ENTER_MS, Easing::OutQuint);
    update(0);
}

void Dialog::build(Content content) {
    float k = m_k;
    auto win = CCDirector::get()->getWinSize();
    m_width = std::min(content.width * k, win.width - 40 * k);

    if (m_column) m_column->removeFromParent();
    m_buttons.clear();
    m_pressed = nullptr;
    m_holding = false;
    m_scroll = nullptr;
    m_items.clear();
    m_rows.clear();
    m_hoveredRow = m_pressedRow = nullptr;
    m_rowDragging = false;
    m_listTouch = false;
    m_tooltipHolder = nullptr;
    m_tooltip.clear();
    m_panel = nullptr;
    m_progressTrack = m_progressFill = nullptr;
    m_progressText = nullptr;

    // Laid out top-down from y = 0, then centred.
    auto column = CCNode::create();
    m_column = column;
    float y = -40 * k;

    m_ring = RoundedBox::create({RING * k, RING * k}, RING / 2 * k, {0, 0, 0, 0});
    m_ring->setBorder(RING_BORDER * k, {255, 255, 255, 255});
    m_ring->setAnchorPoint({0.5f, 0.5f});
    float ringY = y - RING / 2 * k;
    m_ring->setPosition({0, ringY});
    column->addChild(m_ring);
    // The glyph's box isn't centred on what's drawn (the bin sits left): a
    // holder at the ring's centre, the icon offset so its ink is centred.
    m_icon = CCNode::create();
    m_icon->setPosition({0, ringY});
    column->addChild(m_icon);
    auto glyph = makeIcon(content.icon ? content.icon : icon::CIRCLE_INFO, ICON * k);
    m_iconBaseScale = 1;
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (auto letter : CCArrayExt<CCNode*>(glyph->getChildren())) {
        auto box = letter->boundingBox();
        minX = std::min(minX, box.getMinX());
        minY = std::min(minY, box.getMinY());
        maxX = std::max(maxX, box.getMaxX());
        maxY = std::max(maxY, box.getMaxY());
    }
    auto size = glyph->getContentSize();
    glyph->setAnchorPoint({0.5f, 0.5f});
    if (minX <= maxX) {
        CCPoint offset {size.width / 2 - (minX + maxX) / 2, size.height / 2 - (minY + maxY) / 2};
        glyph->setPosition(offset * glyph->getScale());
    }
    m_icon->addChild(glyph);
    y -= (RING + 30) * k;

    float textW = m_width - 30 * k;
    auto title = makeWrappedText(content.header, HEADER_SIZE * k, textW, theme::CONTENT1);
    y -= 4 * k;
    for (auto line : CCArrayExt<CCNode*>(title->getChildren())) {
        line->setAnchorPoint({0.5f, 1});
        line->setPositionX(0);
    }
    title->setPosition({0, y});
    column->addChild(title);
    y -= title->getContentSize().height + 20 * k;

    if (!content.body.empty()) {
        auto text = makeWrappedText(content.body, BODY_SIZE * k, textW, theme::CONTENT2);
        for (auto line : CCArrayExt<CCNode*>(text->getChildren())) {
            line->setAnchorPoint({0.5f, 1});
            line->setPositionX(0);
        }
        text->setPosition({0, y});
        column->addChild(text);
        y -= text->getContentSize().height;
    }

    if (content.progress) {
        y -= 24 * k;
        m_progressWidth = m_width - 80 * k;
        m_progressTrack = RoundedBox::create({m_progressWidth, 8 * k}, 4 * k, {255, 255, 255, 30});
        m_progressTrack->setAnchorPoint({0, 0.5f});
        m_progressTrack->setPosition({-m_progressWidth / 2, y});
        column->addChild(m_progressTrack);
        m_progressFill = RoundedBox::create({8 * k, 8 * k}, 4 * k, PINK);
        m_progressFill->setAnchorPoint({0, 0.5f});
        m_progressFill->setPosition({-m_progressWidth / 2, y});
        column->addChild(m_progressFill);
        m_progress.set(0);
        y -= 22 * k;
        m_progressText = makeText(" ", Weight::Regular, 15 * k);
        m_progressText->setColor(theme::LIGHT1);
        m_progressText->setPosition({0, y});
        column->addChild(m_progressText);
        y -= 10 * k;
    }

    if (content.panel) {
        y -= 24 * k;
        m_panel = content.panel;
        m_panel->setAnchorPoint({0.5f, 1});
        m_panel->setPosition({0, y});
        m_panel->setVisible(false);
        column->addChild(m_panel);
        y -= m_panel->getContentSize().height;
    }

    if (!content.items.empty()) {
        y -= 24 * k;
        float listW = m_width - 60 * k;
        bool tooltips = false;
        for (auto item : content.items) {
            m_items.push_back(item);
            if (auto row = typeinfo_cast<SettingsRow*>(item)) {
                m_rows.push_back(row);
                tooltips = tooltips || !row->tooltip().empty();
            }
        }
        float total = 0;
        for (auto item : m_items) total += item->getContentSize().height;
        float listH = std::min(total, content.listHeight * k);
        m_scroll = ScrollArea::create({listW, listH});
        m_scroll->setPosition({-listW / 2, y - listH});
        column->addChild(m_scroll);
        for (auto item : m_items) m_scroll->content()->addChild(item);
        layoutItems();
        y -= listH;
        if (tooltips) {
            // The hovered (or last tapped) row's description.
            y -= 12 * k;
            m_tooltipWidth = listW;
            m_tooltipHolder = CCNode::create();
            m_tooltipHolder->setPosition({-listW / 2, y});
            column->addChild(m_tooltipHolder);
            y -= 15 * 1.15f * 2 * k;
        }
    }
    y -= 30 * k;

    m_buttons.reserve(content.buttons.size());
    for (auto& def : content.buttons) {
        ButtonNode b;
        b.def = std::move(def);
        b.root = CCNode::create();
        b.root->setPosition({0, y - BUTTON_HEIGHT / 2 * k});
        CCSize size {m_width * IDLE_WIDTH, BUTTON_HEIGHT * k};
        b.bg = RoundedBox::create(size, 5 * k, colourFor(b.def.kind));
        b.bg->setShadow(6 * k, {0, 0, 0, 13});
        b.bg->setAnchorPoint({0.5f, 0.5f});
        b.bg->setSkewX(CC_RADIANS_TO_DEGREES(std::atan(SHEAR)));
        b.root->addChild(b.bg);
        if (b.def.kind == Kind::Dangerous) {
            b.progress = RoundedBox::create({1, size.height}, 5 * k, {255, 255, 255, 70});
            b.progress->setAnchorPoint({0, 0.5f});
            b.progress->setSkewX(CC_RADIANS_TO_DEGREES(std::atan(SHEAR)));
            b.root->addChild(b.progress);
        }
        b.flash = RoundedBox::create(size, 5 * k, {255, 255, 255, 60});
        b.flash->setAnchorPoint({0.5f, 0.5f});
        b.flash->setSkewX(CC_RADIANS_TO_DEGREES(std::atan(SHEAR)));
        b.root->addChild(b.flash);
        b.text = makeText(b.def.label, Weight::Bold, 28 * k);
        b.text->setAnchorPoint({0.5f, 0.5f});
        b.root->addChild(b.text);
        column->addChild(b.root);
        m_buttons.push_back(std::move(b));
        y -= (BUTTON_HEIGHT + BUTTON_SPACING) * k;
    }
    if (m_buttons.empty()) y += 10 * k;
    else y -= 30 * k - BUTTON_SPACING * k;

    float height = -y;
    // Short screens (phones, big UI scale): the whole dialog shrinks to fit.
    m_fit = std::min(1.f, (win.height - 24 * k) / height);
    auto card = RoundedBox::create({m_width, height}, CORNER * k, BACKGROUND);
    card->setShadow(14 * k, {0, 0, 0, 51});
    card->setAnchorPoint({0.5f, 1});
    column->addChild(card, -1);

    column->setPosition({0, height / 2});
    m_content->addChild(column);
}

void Dialog::layoutItems() {
    if (!m_scroll) return;
    float y = 0;
    for (auto item : m_items) {
        auto row = typeinfo_cast<SettingsRow*>(item);
        bool shown = !row || row->applicable();
        item->setVisible(shown);
        if (!shown) continue;
        if (row) {
            // Rows are drawn up from their origin; anything else hangs below
            // it (like makeWrappedText).
            row->setAnchorPoint({0, 1});
            row->setPosition({0, -y});
        } else {
            item->setPosition({0, -y});
        }
        y += item->getContentSize().height;
    }
    m_scroll->setContentHeight(y);
}

void Dialog::refreshRows() {
    for (auto row : m_rows) row->refresh();
    layoutItems();
}

void Dialog::setProgress(float progress, std::string const& text) {
    if (!m_progressFill) return;
    m_progress.to(std::clamp(progress, 0.f, 1.f), 200, Easing::OutQuint);
    if (m_progressText) m_progressText->setString(text.empty() ? " " : text.c_str());
}

void Dialog::showTooltip(std::string const& text) {
    if (!m_tooltipHolder || text == m_tooltip) return;
    m_tooltip = text;
    m_tooltipHolder->removeAllChildren();
    if (text.empty()) return;
    m_tooltipHolder->addChild(makeWrappedText(text, 15 * m_k, m_tooltipWidth, theme::LIGHT1));
}

SettingsRow* Dialog::rowAt(CCPoint world) {
    for (auto row : m_rows) {
        if (!row->isVisible()) continue;
        auto local = row->convertToNodeSpace(world);
        auto size = row->getContentSize();
        if (local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height) return row;
    }
    return nullptr;
}

void Dialog::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -550, true);
}

void Dialog::onExit() {
    if (!m_closing) g_open--;
    m_closing = true;
    CCLayer::onExit();
}

void Dialog::close() {
    if (m_closing) return;
    m_closing = true;
    g_open--;
    m_pressed = nullptr;
    m_dimAlpha.to(0, EXIT_MS, Easing::OutQuint);
    m_alpha.to(0, EXIT_MS, Easing::OutQuint);
    m_scale.to(0.7f, EXIT_MS, Easing::Out);
    sfx::play(sfx::sound::DIALOG_POP_OUT);
    this->setTouchEnabled(false);
    this->setKeypadEnabled(false);
}

void Dialog::update(float dt) {
    float ms = dt * 1000.f;
    m_ms += ms;
    for (auto t : {&m_dimAlpha, &m_alpha, &m_scale, &m_ringSize, &m_iconScale}) t->update(dt);
    // The icon grows in just after the ring starts.
    if (m_ms >= 100 && m_iconScale.target() == 0.f && !m_closing) m_iconScale.to(1, ENTER_MS * 1.5f, Easing::OutQuint);

    m_dim->setOpacity(toByte(m_dimAlpha.get()));
    m_content->setScale(m_scale.get() * m_fit);
    float ring = m_ringSize.get() * m_k;
    m_ring->setContentSize({ring, ring});
    m_ring->setRadius(ring / 2);
    m_icon->setScale(m_iconScale.get() * m_iconBaseScale);

    auto mouse = geode::cocos::getMousePos();
    float k = m_k;

    if (m_scroll) m_scroll->setVisible(m_alpha.get() > 0.6f);
    if (m_panel) m_panel->setVisible(m_alpha.get() > 0.6f);
    if (m_progressFill) {
        m_progress.update(dt);
        float h = 8 * k;
        m_progressFill->setContentSize({std::max(h, m_progressWidth * m_progress.get()), h});
    }
    if (m_scroll && !m_closing && !m_rowDragging) {
        auto hovered = m_scroll->containsWorldPoint(mouse) ? rowAt(mouse) : nullptr;
        if (hovered != m_hoveredRow) {
            if (m_hoveredRow) m_hoveredRow->setHovered(false);
            m_hoveredRow = hovered;
            if (hovered) {
                hovered->setHovered(true);
                showTooltip(hovered->tooltip());
            }
        }
    }
    for (auto& b : m_buttons) {
        if (!m_closing) setHovered(b, buttonAt(mouse) == &b || (m_pressed == &b && m_holding));
        b.width.update(dt);
        b.hold.update(dt);
        b.flashAlpha.update(dt);
        float w = m_width * b.width.get();
        CCSize size {w, BUTTON_HEIGHT * k};
        b.bg->setContentSize(size);
        b.flash->setContentSize(size);
        b.flash->setOpacity(toByte(b.flashAlpha.get() * m_alpha.get()));
        if (b.progress) {
            float p = b.hold.get();
            b.progress->setVisible(p > 0.001f);
            b.progress->setContentSize({std::max(size.height * 0.2f, w * p), size.height});
            b.progress->setPositionX(-w / 2);
            // Ticks rise in pitch while the hold fills (PopupDialogDangerousButton).
            if (m_holding && m_pressed == &b && m_ms - b.lastTickMs >= 40 && p < 1.f) {
                b.lastTickMs = m_ms;
                sfx::play(sfx::sound::DIALOG_DANGEROUS_TICK, 0.f, 1.f + p);
            }
            if (m_holding && m_pressed == &b && p >= 1.f) {
                m_holding = false;
                sfx::play(sfx::sound::DIALOG_DANGEROUS_SELECT);
                auto action = b.def.action;
                if (b.def.closes) close();
                else b.hold.to(0.f, 400, Easing::InSine);
                if (action) action();
                return;
            }
        }
    }

    auto alpha = toByte(m_alpha.get());
    for (auto child : CCArrayExt<CCNode*>(m_content->getChildren())) setOpacityDeep(child, alpha, m_panel);
    for (auto& b : m_buttons) b.flash->setOpacity(toByte(b.flashAlpha.get() * m_alpha.get()));
    if (m_closing && m_alpha.get() <= 0.001f && m_dimAlpha.get() <= 0.001f) this->removeFromParent();
}

Dialog::ButtonNode* Dialog::buttonAt(CCPoint world) {
    for (auto& b : m_buttons) {
        auto local = b.root->convertToNodeSpace(world);
        float w = m_width * std::max(b.width.get(), IDLE_WIDTH);
        float h = BUTTON_HEIGHT * m_k;
        // Undo the shear, then it's a rectangle.
        float x = local.x - local.y * SHEAR;
        if (std::abs(x) <= w / 2 && std::abs(local.y) <= h / 2) return &b;
    }
    return nullptr;
}

void Dialog::setHovered(ButtonNode& b, bool hovered) {
    if (hovered == b.hovered) return;
    b.hovered = hovered;
    if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
    b.width.to(hovered ? HOVER_WIDTH : IDLE_WIDTH, hovered ? 400 : 200, Easing::OutQuint);
}

bool Dialog::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_closing) return true;
    auto loc = touch->getLocation();
    m_listTouch = false;
    if (m_scroll && m_scroll->containsWorldPoint(loc)) {
        m_listTouch = true;
        m_drag.began(m_scroll, loc);
        m_pressedRow = rowAt(loc);
        if (m_pressedRow) showTooltip(m_pressedRow->tooltip());
        if (m_pressedRow && m_pressedRow->wantsDrag()) {
            m_rowDragging = true;
            m_pressedRow->onDrag(m_pressedRow->convertToNodeSpace(loc));
        }
        return true;
    }
    m_pressed = buttonAt(loc);
    if (m_pressed) {
        m_pressed->width.to(HOVER_WIDTH * 0.98f, 800, Easing::OutQuad);
        if (m_pressed->def.kind == Kind::Dangerous) {
            m_holding = true;
            float left = 1.f - m_pressed->hold.get();
            m_pressed->hold.to(1.f, DANGEROUS_HOLD_MS * left, Easing::Out);
        }
    }
    return true;
}

void Dialog::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_listTouch) {
        auto loc = touch->getLocation();
        if (m_rowDragging && m_pressedRow) m_pressedRow->onDrag(m_pressedRow->convertToNodeSpace(loc));
        else if (m_drag.moved(loc)) m_pressedRow = nullptr;
        return;
    }
    if (!m_pressed || m_pressed->def.kind != Kind::Dangerous) return;
    // Sliding off a held dangerous button lets go of it.
    bool over = buttonAt(touch->getLocation()) == m_pressed;
    if (!over && m_holding) {
        m_holding = false;
        m_pressed->hold.to(0.f, 400, Easing::InSine);
    }
}

void Dialog::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_listTouch) {
        m_listTouch = false;
        auto row = m_pressedRow;
        m_pressedRow = nullptr;
        if (m_rowDragging) {
            m_rowDragging = false;
            refreshRows();
            return;
        }
        auto loc = touch->getLocation();
        if (m_drag.ended() || !row || m_closing || rowAt(loc) != row) return;
        row->onClick(row->convertToNodeSpace(loc));
        // A change in one row can change others (and what's shown).
        if (!m_closing) refreshRows();
        return;
    }
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || m_closing) return;
    if (pressed->def.kind == Kind::Dangerous) {
        // Let go too early: the hold drains away.
        m_holding = false;
        pressed->hold.to(0.f, 400, Easing::InSine);
        pressed->width.to(pressed->hovered ? HOVER_WIDTH : IDLE_WIDTH, 200, Easing::In);
        return;
    }
    if (buttonAt(touch->getLocation()) == pressed) press(*pressed);
    else pressed->width.to(pressed->hovered ? HOVER_WIDTH : IDLE_WIDTH, 200, Easing::In);
}

void Dialog::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (m_listTouch) {
        m_listTouch = false;
        m_rowDragging = false;
        m_pressedRow = nullptr;
        m_drag.ended();
        return;
    }
    if (m_pressed && m_pressed->def.kind == Kind::Dangerous) {
        m_holding = false;
        m_pressed->hold.to(0.f, 400, Easing::InSine);
    }
    m_pressed = nullptr;
}

void Dialog::press(ButtonNode& b) {
    b.flashAlpha.set(1);
    b.flashAlpha.to(0, 100, Easing::None);
    b.width.to(b.width.get() * 1.05f, 100, Easing::OutQuint);
    sfx::play(b.def.kind == Kind::Cancel ? sfx::sound::DIALOG_CANCEL_SELECT : sfx::sound::DIALOG_OK_SELECT);
    auto action = b.def.action;
    if (b.def.closes) close();
    else b.width.to(b.hovered ? HOVER_WIDTH : IDLE_WIDTH, 200, Easing::In);
    // May replace the content (and this button): nothing after it.
    if (action) action();
}

void Dialog::keyBackClicked() {
    if (m_closing) return;
    for (auto it = m_buttons.rbegin(); it != m_buttons.rend(); ++it) {
        if (it->def.kind == Kind::Cancel) return press(*it);
    }
    close();
}

} // namespace lazer
