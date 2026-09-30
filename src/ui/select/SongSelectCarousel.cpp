#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Theme.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

// --- carousel ---

SongSelect::Panel& SongSelect::makePanel(size_t visibleIndex) {
    float k = m_k;
    levels::resolve(m_entries[m_visible[visibleIndex]]); // coins for the info row
    auto const& e = m_entries[m_visible[visibleIndex]];
    float pw = m_rightW + 60 * k, ph = m_panelH;
    auto accent = levels::difficultyColor(e.difficulty);

    // Fades in as a whole: its parts follow its opacity.
    auto root = CCNodeRGBA::create();
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(0);
    root->setContentSize({pw, ph});
    root->setAnchorPoint({0, 0.5f});
    m_carousel->addChild(root);

    auto bg = RoundedBox::create({pw, ph}, CORNER * k, PANEL_BG);
    bg->setAnchorPoint({0, 0});
    root->addChild(bg, 0);

    // Thumbnail on the right, fading into the panel (PanelSetBackground).
    auto thumb = RoundedBox::create({pw * 0.62f, ph}, CORNER * k, {255, 255, 255, 255});
    thumb->setCornerRadii(0, CORNER * k, 0, CORNER * k);
    thumb->setAnchorPoint({0, 0});
    thumb->setPosition({pw * 0.38f, 0});
    thumb->setOpacity(0);
    thumb->setVisible(false);
    root->addChild(thumb, 1);
    auto fade = CascadingGradient::create({PANEL_BG.r, PANEL_BG.g, PANEL_BG.b, 255}, {PANEL_BG.r, PANEL_BG.g, PANEL_BG.b, 0}, {1, 0});
    fade->setContentSize({pw * 0.3f, ph});
    fade->setPosition({pw * 0.38f, 0});
    root->addChild(fade, 2);

    // Difficulty strip.
    auto strip = RoundedBox::create({STRIP_WIDTH * k, ph}, CORNER * k, {accent.r, accent.g, accent.b, 255});
    strip->setCornerRadii(CORNER * k, 0, CORNER * k, 0);
    strip->setAnchorPoint({0, 0});
    root->addChild(strip, 3);
    auto face = difficultyFace(e, ph * 0.56f);
    face->setPosition({STRIP_WIDTH * k / 2, ph * 0.6f});
    root->addChild(face, 4);
    if (e.stars > 0) {
        auto stars = infoRow({{rewardIcon(e), std::to_string(e.stars)}}, 12 * k, {255, 255, 255});
        stars->setPosition({(STRIP_WIDTH * k - stars->getContentSize().width + 10 * k) / 2, ph * 0.17f});
        root->addChild(stars, 4);
    }

    float x = STRIP_WIDTH * k + 14 * k;
    float maxW = pw * 0.62f - x;
    auto title = makeText(e.name, Weight::SemiBold, 22 * k);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({x, ph * 0.72f});
    fit(title, maxW);
    root->addChild(title, 4);

    auto creator = makeText("by " + e.creator, Weight::Regular, 14 * k);
    creator->setColor(theme::CONTENT2);
    creator->setAnchorPoint({0, 0.5f});
    creator->setPosition({x, ph * 0.46f});
    fit(creator, maxW);
    root->addChild(creator, 4);

    std::vector<std::pair<char const*, std::string>> info;
    if (!e.platformer) info.push_back({icon::CLOCK, levels::lengthName(e.length)});
    if (e.coins > 0) info.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
    if (!e.platformer) info.push_back({e.normalPercent >= 100 ? icon::CHECK : nullptr, fmt::format("{}%", e.normalPercent)});
    else if (e.normalPercent >= 100) info.push_back({icon::CHECK, e.bestTime > 0 ? formatTime(e.bestTime) : "completed"});
    auto row = infoRow(info, 12 * k, theme::LIGHT1);
    row->setPosition({x, ph * 0.2f});
    root->addChild(row, 4);

    auto& panel = m_panels[visibleIndex];
    panel = Panel {m_visible[visibleIndex], root, bg, thumb};
    panel.appear.to(1.f, PANEL_FADE, Easing::OutQuint);
    return panel;
}

void SongSelect::updateCarousel(float dt) {
    float ms = dt * 1000.f;
    float k = m_k;
    float viewH = viewHeight(), halfH = viewH / 2;
    float step = m_panelH + m_spacing;

    if (!m_visible.empty()) {
        auto [minScroll, maxScroll] = scrollRange();
        if (!m_dragging) m_scrollTarget = std::clamp(m_scrollTarget, minScroll, maxScroll);
    }
    if (m_dragging || m_barDragging) m_scroll = m_scrollTarget;
    else m_scroll = damp(m_scroll, m_scrollTarget, SCROLL_DECAY, ms);

    for (auto& [index, panel] : m_panels) panel.seen = false;
    if (!m_visible.empty()) {
        int first = std::max(0, static_cast<int>(std::floor((m_scroll - m_panelH) / step)));
        int last = std::min(static_cast<int>(m_visible.size()) - 1, static_cast<int>(std::ceil((m_scroll + viewH + m_panelH) / step)));
        auto mouse = geode::cocos::getMousePos();
        float colLeft = m_win.width - m_rightW;

        // Missing panels, a few per frame, from the middle of the view outwards.
        float middle = (m_scroll + halfH - m_panelH / 2) / step;
        std::vector<int> missing;
        for (int i = first; i <= last; i++) {
            if (!m_panels.contains(i)) missing.push_back(i);
        }
        std::sort(missing.begin(), missing.end(), [middle](int a, int b) {
            return std::abs(a - middle) < std::abs(b - middle);
        });
        for (size_t n = 0; n < missing.size() && n < static_cast<size_t>(PANEL_LOADS_PER_FRAME); n++) makePanel(missing[n]);

        for (int i = first; i <= last; i++) {
            auto it = m_panels.find(i);
            if (it == m_panels.end()) continue; // built in a later frame
            Panel& p = it->second;
            p.seen = true;
            p.appear.update(dt);
            auto alpha = static_cast<GLubyte>(255 * std::clamp(p.appear.get(), 0.f, 1.f));
            if (p.root->getOpacity() != alpha) p.root->setOpacity(alpha);

            float centerFromTop = itemTop(i) - m_scroll + m_panelH / 2;
            float y = m_carouselTop - centerFromTop;
            // Carousel.offsetX: panels curve away towards the top and bottom.
            float dist = std::abs(1.f - centerFromTop / halfH);
            float offset = (3.f - std::sqrt(std::max(0.f, 9.f - dist * dist))) * halfH;

            bool selected = m_hasSelection && static_cast<size_t>(i) == m_selected;
            if (p.active.target() != (selected ? 1.f : 0.f)) p.active.to(selected ? 1.f : 0.f, 400, Easing::OutQuint);

            p.root->setPosition({colLeft + offset - p.active.get() * ACTIVE_X * k, y});
            bool hovered = !m_dragging && !g_overlayOpen && mouse.y > m_carouselBottom && mouse.y < m_carouselTop
                && containsWorld(p.root, mouse);
            if (hovered != p.hovered) {
                p.hovered = hovered;
                p.hover.to(hovered ? 1.f : 0.f, hovered ? 100 : 500, Easing::OutQuint);
                if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            }
            p.active.update(dt);
            p.hover.update(dt);
            p.bg->setFillColor(theme::lerp(PANEL_BG, PANEL_HOVER, p.hover.get()));
            auto const& e = m_entries[p.entry];
            auto accent = levels::difficultyColor(e.difficulty);
            p.bg->setBorder(2.5f * k * p.active.get(), {accent.r, accent.g, accent.b, static_cast<GLubyte>(255 * p.active.get())});

            // Thumbnail, once the panel has settled in view.
            p.visibleMs += ms;
            if (!p.thumbRequested && p.visibleMs > THUMB_DELAY) {
                p.thumbRequested = true;
                Ref<RoundedBox> thumb = p.thumb;
                levelThumbnail(e, [thumb](CCTexture2D* texture) {
                    if (!texture || !thumb->getParent()) return;
                    thumb->setTexture(texture);
                    thumb->setVisible(true);
                    thumb->setUserObject("loaded"_spr, CCBool::create(true));
                }, [thumb] {
                    // Its panel left the view before its turn came: skip it.
                    return thumb->getParent() != nullptr;
                });
            }
            if (p.thumb->getUserObject("loaded"_spr) && p.thumbAlpha.target() < 1.f) p.thumbAlpha.to(1.f, 300, Easing::OutQuint);
            p.thumbAlpha.update(dt);
            p.thumb->setOpacity(static_cast<GLubyte>(p.thumbAlpha.get() * 150));
        }
    }
    for (auto it = m_panels.begin(); it != m_panels.end();) {
        if (!it->second.seen) {
            it->second.root->removeFromParent();
            it = m_panels.erase(it);
        } else {
            ++it;
        }
    }
}

std::pair<float, float> SongSelect::scrollRange() const {
    float halfH = viewHeight() / 2;
    float last = m_visible.empty() ? 0.f : itemTop(m_visible.size() - 1);
    return {m_panelH / 2 - halfH, last + m_panelH / 2 - halfH};
}

// Length from the visible share of the list (at least three widths, like
// osu!), position from the scroll. Grey, white on hover, highlight while held.
void SongSelect::updateScrollbar(float dt) {
    float k = m_k;
    auto [minScroll, maxScroll] = scrollRange();
    float range = maxScroll - minScroll;
    float viewH = viewHeight();
    m_bar->setVisible(range > 1.f);
    if (range <= 1.f) return;

    m_barLength = std::max(SCROLLBAR_WIDTH * 3 * k, viewH * viewH / (range + viewH));
    float t = std::clamp((m_scroll - minScroll) / range, 0.f, 1.f);
    m_barY = m_carouselTop - m_barLength / 2 - t * (viewH - m_barLength);
    m_barWidth.update(dt);
    m_barPull.update(dt);
    float width = SCROLLBAR_WIDTH * k * m_barWidth.get();
    float right = m_win.width - SCROLLBAR_MARGIN * k + m_barPull.get();
    m_bar->setContentSize({width, m_barLength});
    m_bar->setRadius(width / 2);
    m_bar->setPosition({right, m_barY});

    // Label: follows the bar while it's held.
    m_barLabelAlpha.update(dt);
    float labelAlpha = m_barLabelAlpha.get();
    m_barLabel->setVisible(labelAlpha > 0.01f);
    if (m_barDragging) {
        auto text = scrollbarText();
        if (text != m_barText) {
            m_barText = text;
            m_barLabelText->setString(text.c_str());
            float padX = 14 * k;
            float textW = m_barLabelText->getScaledContentSize().width;
            m_barLabelBg->setContentSize({std::max(textW + padX * 2, 44 * k), 44 * k});
            m_barLabelText->setPosition({-padX, 0});
        }
    }
    if (m_barLabel->isVisible()) {
        // Slides out from the bar as it fades in.
        float gap = SCROLLBAR_LABEL_GAP * k * (0.6f + 0.4f * labelAlpha);
        float y = std::clamp(m_barY, m_carouselBottom + 22 * k, m_carouselTop - 22 * k);
        m_barLabel->setPosition({right - width - gap, y});
        m_barLabelBg->setOpacity(static_cast<GLubyte>(labelAlpha * 255));
        m_barLabelText->setOpacity(static_cast<GLubyte>(labelAlpha * 255));
    }

    auto mouse = geode::cocos::getMousePos();
    auto local = m_bar->convertToNodeSpace(mouse);
    auto size = m_bar->getContentSize();
    bool hovered = local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    if (hovered != m_barHovered) {
        m_barHovered = hovered;
        m_barHover.to(hovered ? 1.f : 0.f, 100, Easing::None);
    }
    m_barHover.update(dt);
    m_barHighlight.update(dt);
    auto grey = static_cast<GLubyte>(136 + (255 - 136) * m_barHover.get());
    ccColor4B colour {grey, grey, grey, 255};
    m_bar->setFillColor(theme::lerp(colour, theme::HIGHLIGHT1, m_barHighlight.get()));
}

bool SongSelect::scrollbarHit(CCPoint world) const {
    if (!m_bar->isVisible()) return false;
    return world.x > m_win.width - SCROLLBAR_HIT_WIDTH * m_k
        && world.y > m_carouselBottom && world.y < m_carouselTop;
}

std::string SongSelect::scrollbarText() const {
    if (m_visible.empty()) return "";
    float step = m_panelH + m_spacing;
    float middle = m_scroll + viewHeight() / 2 - m_panelH / 2;
    auto index = static_cast<size_t>(std::clamp(std::round(middle / step), 0.f, float(m_visible.size() - 1)));
    auto const& e = m_entries[m_visible[index]];
    switch (m_sort) {
        case levels::Sort::Title:
            for (char c : e.name) {
                auto u = static_cast<unsigned char>(c);
                if (std::isalpha(u)) return std::string(1, static_cast<char>(std::toupper(u)));
                if (std::isdigit(u)) return "#";
            }
            return "?";
        case levels::Sort::Difficulty: return levels::difficultyName(e.difficulty);
        case levels::Sort::Progress:
            if (e.platformer) return e.normalPercent >= 100 ? "completed" : "not completed";
            return fmt::format("{}%", e.normalPercent);
        default: return fmt::format("{} / {}", index + 1, m_visible.size());
    }
}

// Moves the bar's centre to the touch's y - grab, and the list with it; the
// touch's x pulls the bar sideways.
void SongSelect::dragScrollbar(CCPoint touch) {
    float k = m_k;
    float dx = touch.x - (m_win.width - SCROLLBAR_MARGIN * k - SCROLLBAR_WIDTH * k / 2);
    float max = SCROLLBAR_PULL_MAX * k;
    m_barPull.set(std::clamp(dx * SCROLLBAR_PULL_FOLLOW, -max, 0.f));

    auto [minScroll, maxScroll] = scrollRange();
    float travel = viewHeight() - m_barLength;
    if (travel <= 0) return;
    float centre = std::clamp(touch.y - m_barGrab, m_carouselBottom + m_barLength / 2, m_carouselTop - m_barLength / 2);
    float t = (m_carouselTop - m_barLength / 2 - centre) / travel;
    m_scrollTarget = minScroll + t * (maxScroll - minScroll);
}

} // namespace lazer
