#include "GameplayButtons.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "../menu/Toolbar.hpp"
#include "Dialog.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr float IDLE_WIDTH = 0.8f, HOVER_WIDTH = 0.9f; // DialogButton
    constexpr float GLOW_SPREAD = 1.08f;
    constexpr float HOVER_MS = 400, CLICK_MS = 200;
    constexpr float SHEAR = 0.2f;                          // OsuGame.SHEAR

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    bool inGameBinary(uintptr_t addr) {
#ifdef GEODE_IS_WINDOWS
        static uintptr_t start = 0, end = 0;
        if (!start) {
            start = geode::base::get();
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(start);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(start + dos->e_lfanew);
            end = start + nt->OptionalHeader.SizeOfImage;
        }
        return addr >= start && addr < end;
#else
        Dl_info info {};
        if (!dladdr(reinterpret_cast<void*>(addr), &info)) return false;
        return reinterpret_cast<uintptr_t>(info.dli_fbase) == geode::base::get();
#endif
    }

    bool inAnyBinary(uintptr_t addr) {
#ifdef GEODE_IS_WINDOWS
        HMODULE module = nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(addr), &module) && module;
#else
        Dl_info info {};
        return dladdr(reinterpret_cast<void*>(addr), &info) && info.dli_fbase;
#endif
    }
}

// ---------------------------------------------------------------------------
// DialogButtonItem

DialogButtonItem* DialogButtonItem::create(std::string const& text, ccColor4B colour, float width, float height,
                                           float k, std::function<void()> action) {
    auto ret = new DialogButtonItem();
    if (ret->init(text, colour, width, height, k, std::move(action))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool DialogButtonItem::init(std::string const& text, ccColor4B colour, float width, float height, float k,
                            std::function<void()> action) {
    if (!CCMenuItem::initWithTarget(nullptr, nullptr)) return false;
    m_action = std::move(action);
    m_k = k;
    m_fSizeMult = 1.f;
    this->setContentSize({width, height});
    this->setAnchorPoint({0.5f, 0.5f});
    this->setCascadeOpacityEnabled(true);
    float skew = CC_RADIANS_TO_DEGREES(std::atan(SHEAR));
    CCPoint centre {width / 2, height / 2};

    // The glow: the button's colour fading out at both ends, behind the bar.
    ccColor4B clear {colour.r, colour.g, colour.b, 0};
    m_glow = CCNodeRGBA::create();
    m_glow->setCascadeOpacityEnabled(true);
    m_glow->setPosition(centre);
    m_glow->setSkewX(skew);
    m_glow->setOpacity(0);
    m_glowLeft = CCLayerGradient::create(clear, colour, {1, 0});
    m_glowCentre = CCLayerColor::create(colour);
    m_glowRight = CCLayerGradient::create(colour, clear, {1, 0});
    for (CCNode* part : {static_cast<CCNode*>(m_glowLeft), static_cast<CCNode*>(m_glowCentre), static_cast<CCNode*>(m_glowRight)}) {
        m_glow->addChild(part);
    }
    this->addChild(m_glow, -2);

    m_bar = RoundedBox::create({width * IDLE_WIDTH, height}, 5 * k, colour);
    m_bar->setShadow(6 * k, {0, 0, 0, 13});
    m_bar->setAnchorPoint({0.5f, 0.5f});
    m_bar->setPosition(centre);
    m_bar->setSkewX(skew);
    this->addChild(m_bar, -1);

    m_flash = RoundedBox::create({width * IDLE_WIDTH, height}, 5 * k, {255, 255, 255, 60});
    m_flash->setAnchorPoint({0.5f, 0.5f});
    m_flash->setPosition(centre);
    m_flash->setSkewX(skew);
    m_flash->setOpacity(0);
    this->addChild(m_flash);

    m_text = makeText(text, Weight::Bold, 28 * k);
    m_textBase = m_text->getScale();
    m_text->setPosition(centre);
    this->addChild(m_text, 1);

    layout();
    this->scheduleUpdate();
    return true;
}

void DialogButtonItem::setHeight(float height) {
    auto size = this->getContentSize();
    if (height == size.height) return;
    this->setContentSize({size.width, height});
    CCPoint centre {size.width / 2, height / 2};
    for (CCNode* node : {static_cast<CCNode*>(m_glow), static_cast<CCNode*>(m_bar), static_cast<CCNode*>(m_flash),
                         static_cast<CCNode*>(m_text)}) {
        node->setPosition(centre);
    }
    layout();
}

void DialogButtonItem::layout() {
    auto size = this->getContentSize();
    float h = size.height;
    float bar = size.width * m_width.get();
    m_bar->setContentSize({bar, h});
    m_flash->setContentSize({bar, h});
    float glow = size.width * m_glowWidth.get();
    m_glowLeft->setContentSize({glow * 0.125f, h});
    m_glowLeft->setPosition({-glow / 2, -h / 2});
    m_glowCentre->setContentSize({glow * 0.75f, h});
    m_glowCentre->setPosition({-glow * 0.375f, -h / 2});
    m_glowRight->setContentSize({glow * 0.125f, h});
    m_glowRight->setPosition({glow * 0.375f, -h / 2});
    m_glow->setOpacity(toByte(m_glowAlpha.get()));
    m_flash->setOpacity(toByte(m_flashAlpha.get()));
    m_text->setScale(m_textBase * m_textScale.get());
}

void DialogButtonItem::setSelectedState(bool selected) {
    if (selected == m_selectedState) return;
    m_selectedState = selected;
    // The click's widen plays out first (DialogButton.clickAnimating).
    if (m_clickMs > 0) return;
    if (selected) {
        m_textScale.to(1.02f, HOVER_MS, Easing::OutQuint);
        m_width.to(HOVER_WIDTH, HOVER_MS, Easing::OutQuint);
        m_glowWidth.to(HOVER_WIDTH * GLOW_SPREAD, HOVER_MS, Easing::OutQuint);
        m_glowAlpha.to(1, HOVER_MS, Easing::OutQuint);
    } else {
        m_width.to(IDLE_WIDTH, HOVER_MS / 2, Easing::OutQuint);
        m_glowWidth.to(IDLE_WIDTH * GLOW_SPREAD, HOVER_MS, Easing::OutQuint);
        m_textScale.to(1, HOVER_MS / 2, Easing::OutQuint);
        m_glowAlpha.to(0, HOVER_MS / 2, Easing::OutQuint);
    }
}

bool DialogButtonItem::containsWorldPoint(CCPoint world) {
    auto local = this->convertToNodeSpace(world);
    auto size = this->getContentSize();
    return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
}

void DialogButtonItem::selected() {
    CCMenuItem::selected();
#ifndef GEODE_IS_DESKTOP
    // No hover on touch screens: pressing is hovering.
    setSelectedState(true);
#endif
    m_width.to(HOVER_WIDTH * 0.98f, CLICK_MS * 4, Easing::OutQuad);
}

void DialogButtonItem::unselected() {
    CCMenuItem::unselected();
    if (m_selectedState) m_width.to(HOVER_WIDTH, CLICK_MS, Easing::In);
#ifndef GEODE_IS_DESKTOP
    setSelectedState(false);
#endif
}

void DialogButtonItem::activate() {
    if (!m_bEnabled) return;
    m_flashAlpha.set(1);
    m_flashAlpha.to(0, 100, Easing::None);
    m_width.to(m_width.get() * 1.05f, 100, Easing::OutQuint);
    m_clickMs = 100;
    sfx::click(sfx::sound::BUTTON_SELECT);
    // GD's handler may close the layer holding this button: nothing after it.
    auto action = m_action;
    if (action) action();
}

void DialogButtonItem::update(float dt) {
    if (m_clickMs > 0) {
        m_clickMs -= dt * 1000.f;
        if (m_clickMs <= 0) {
            // Back to whatever the selection is now.
            bool state = m_selectedState;
            m_selectedState = !state;
            setSelectedState(state);
        }
    }
    bool moving = false;
    for (auto t : {&m_width, &m_glowWidth, &m_glowAlpha, &m_textScale, &m_flashAlpha}) moving |= t->update(dt);
    // Settled buttons need no new layout.
    if (moving || m_settling) layout();
    m_settling = moving;
}

// ---------------------------------------------------------------------------
// AnimatedButtonItem

AnimatedButtonItem* AnimatedButtonItem::create(CCSize size, float radius, ccColor4B colour, CCNode* content,
                                               std::function<void()> action) {
    auto ret = new AnimatedButtonItem();
    if (ret->init(size, radius, colour, content, std::move(action))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AnimatedButtonItem::init(CCSize size, float radius, ccColor4B colour, CCNode* content,
                              std::function<void()> action) {
    if (!CCMenuItem::initWithTarget(nullptr, nullptr)) return false;
    m_action = std::move(action);
    m_fSizeMult = 1.f;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});
    this->setCascadeOpacityEnabled(true);

    m_root = CCNodeRGBA::create();
    m_root->setCascadeOpacityEnabled(true);
    m_root->setPosition(size / 2);
    this->addChild(m_root);

    auto bg = RoundedBox::create(size, radius, colour);
    bg->setShadow(radius * 0.5f, {0, 0, 0, 10});
    bg->setAnchorPoint({0.5f, 0.5f});
    m_root->addChild(bg);
    if (content) m_root->addChild(content, 1);
    // HoverColour: white at 10%, flashing to 30% on click.
    m_hoverBox = RoundedBox::create(size, radius, {255, 255, 255, 255});
    m_hoverBox->setAnchorPoint({0.5f, 0.5f});
    m_hoverBox->setOpacity(0);
    m_root->addChild(m_hoverBox, 2);

    this->scheduleUpdate();
    return true;
}

bool AnimatedButtonItem::containsWorldPoint(CCPoint world) {
    auto local = this->convertToNodeSpace(world);
    auto size = this->getContentSize();
    return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
}

void AnimatedButtonItem::setHovered(bool hovered) {
    if (hovered == m_hovered) return;
    m_hovered = hovered;
    if (hovered) sfx::hover(sfx::sound::BUTTON_HOVER);
    m_hoverAlpha.to(hovered ? 0.1f : 0.f, 500, Easing::OutQuint);
}

void AnimatedButtonItem::selected() {
    CCMenuItem::selected();
#ifndef GEODE_IS_DESKTOP
    setHovered(true);
#endif
    m_scale.to(0.75f, 2000, Easing::OutQuint);
}

void AnimatedButtonItem::unselected() {
    CCMenuItem::unselected();
#ifndef GEODE_IS_DESKTOP
    setHovered(false);
#endif
    m_scale.to(1, 1000, Easing::OutElastic);
}

void AnimatedButtonItem::activate() {
    if (!m_bEnabled) return;
    m_hoverAlpha.set(0.3f);
    m_hoverAlpha.to(m_hovered ? 0.1f : 0.f, 800, Easing::OutQuint);
    sfx::click(sfx::sound::BUTTON_SELECT);
    auto action = m_action;
    if (action) action();
}

void AnimatedButtonItem::update(float dt) {
    m_scale.update(dt);
    m_hoverAlpha.update(dt);
    m_root->setScale(m_scale.get());
    m_hoverBox->setOpacity(toByte(m_hoverAlpha.get()));
}

// ---------------------------------------------------------------------------

CCNode* iconLabel(char const* glyph, std::string const& text, float size) {
    auto node = CCNodeRGBA::create();
    node->setCascadeOpacityEnabled(true);
    node->setCascadeColorEnabled(true);
    auto icon = makeIcon(glyph, size);
    float iconW = icon->getScaledContentSize().width;
    float gap = text.empty() ? 0 : size * 0.45f;
    CCLabelBMFont* label = text.empty() ? nullptr : makeText(text, Weight::SemiBold, size);
    float labelW = label ? label->getScaledContentSize().width : 0;
    float total = iconW + gap + labelW;
    icon->setAnchorPoint({0, 0.5f});
    icon->setPosition({-total / 2, 0});
    node->addChild(icon);
    if (label) {
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({-total / 2 + iconW + gap, 0});
        node->addChild(label);
    }
    node->setContentSize({total, size});
    return node;
}

bool isModButton(CCMenuItem* item) {
    auto selector = item->m_pfnSelector;
    if (!selector) {
        // Geode's callback buttons carry no selector; mods' ones have mod IDs.
        return item->getID().view().find('/') != std::string_view::npos;
    }
    // A member function pointer starts with the function's address (virtual
    // ones hold an offset instead, which isn't in any binary: skipped).
    uintptr_t address = 0;
    static_assert(sizeof(selector) >= sizeof(address));
    std::memcpy(&address, &selector, sizeof(address));
    return inAnyBinary(address) && !inGameBinary(address);
}

std::vector<CCMenuItem*> collectModButtons(CCNode* parent) {
    std::vector<CCMenuItem*> out;
    auto ours = Mod::get()->getID().view();
    for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
        auto menu = typeinfo_cast<CCMenu*>(child);
        if (!menu || menu->getID().view().starts_with(ours)) continue;
        for (auto node : CCArrayExt<CCNode*>(menu->getChildren())) {
            auto item = typeinfo_cast<CCMenuItem*>(node);
            if (item && item->isVisible() && isModButton(item)) out.push_back(item);
        }
    }
    return out;
}

CCNode* modButtonImage(CCMenuItem* item, float size) {
    CCNode* image = nullptr;
    if (auto sprite = typeinfo_cast<CCMenuItemSprite*>(item)) {
        if (auto normal = sprite->getNormalImage()) image = snapshotNode(normal);
    }
    if (!image) image = makeIcon(icon::PUZZLE, size * 0.6f);
    auto bounds = image->getScaledContentSize();
    float fit = std::max(bounds.width, bounds.height);
    if (fit > 0) image->setScale(image->getScale() * size / fit);
    return image;
}

bool gameplayPopupOnTop() {
    if (Dialog::isOpen()) return true;
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child->isVisible() || child->getUserObject("hidden"_spr)) continue;
        if (typeinfo_cast<FLAlertLayer*>(child)) return true;
    }
    return false;
}

} // namespace lazer
