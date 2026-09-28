#include "PauseMenu.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "Dialog.hpp"
#include "GameplayButtons.hpp"
#include "SettingsRows.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr float TRANSITION_MS = 200;     // GameplayMenuOverlay.TRANSITION_DURATION
    constexpr float BUTTON_HEIGHT = 80;      // button_height
    constexpr float BUTTON_SPACING = 2;
    constexpr float BUTTON_PADDING = 50;     // the button column's horizontal padding
    constexpr float BACKGROUND_ALPHA = 0.75f;
    constexpr float TITLE_SIZE = 48, TITLE_SPACING = 5, INFO_SIZE = 18;
    constexpr float FOOTER_HEIGHT = 36, FOOTER_GAP = 8, FOOTER_MARGIN = 24;

    // OsuColour
    constexpr ccColor4B YELLOW {0xff, 0xcc, 0x22, 255};
    constexpr ccColor4B YELLOW_DARK {0xee, 0xaa, 0x00, 255};
    constexpr ccColor4B GREEN {0x88, 0xb3, 0x00, 255};
    constexpr ccColor4B QUIT_RED {170, 27, 39, 255};
    constexpr ccColor4B BLUE_LIGHT {0x66, 0xcc, 0xff, 255};
    constexpr ccColor4B GRAY4 {0x44, 0x44, 0x44, 255};

    CCNodeRGBA* group() {
        auto node = CCNodeRGBA::create();
        node->setCascadeOpacityEnabled(true);
        return node;
    }

    // osu!'s title Spacing: letters pushed apart by `spacing` (GD units).
    void spaceLetters(CCLabelBMFont* label, float spacing) {
        float scale = label->getScaleX();
        if (scale <= 0) return;
        float local = spacing / scale;
        int i = 0;
        for (auto letter : CCArrayExt<CCNode*>(label->getChildren())) {
            letter->setPositionX(letter->getPositionX() + local * i);
            i++;
        }
        if (i > 1) {
            auto size = label->getContentSize();
            label->setContentSize({size.width + local * (i - 1), size.height});
        }
    }

    // One line of osu!'s play info: plain text, then the value in bold.
    CCNode* infoLine(std::string const& text, std::string const& value, float size) {
        auto node = group();
        auto a = makeText(text, Weight::Regular, size);
        auto b = makeText(value, Weight::Bold, size);
        float wa = a->getScaledContentSize().width, wb = b->getScaledContentSize().width;
        a->setAnchorPoint({0, 0.5f});
        a->setPosition({-(wa + wb) / 2, 0});
        b->setAnchorPoint({0, 0.5f});
        b->setPosition({-(wa + wb) / 2 + wa, 0});
        node->addChild(a);
        node->addChild(b);
        node->setContentSize({wa + wb, size});
        return node;
    }

    // GD's normal / practice progress bars, as a slim osu! bar with its value.
    CCNode* progressBar(std::string const& name, int percent, ccColor4B colour, float k) {
        auto node = group();
        float size = 14 * k, track = 110 * k, h = 6 * k, gap = 8 * k;
        auto label = makeText(name, Weight::Regular, size);
        label->setColor({200, 200, 200});
        auto value = makeText(fmt::format("{}%", percent), Weight::Bold, size);
        float wl = label->getScaledContentSize().width, wv = value->getScaledContentSize().width;
        float total = wl + gap + track + gap + wv;
        float x = -total / 2;
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, 0});
        node->addChild(label);
        x += wl + gap;
        auto back = RoundedBox::create({track, h}, h / 2, {255, 255, 255, 40});
        back->setAnchorPoint({0, 0.5f});
        back->setPosition({x, 0});
        node->addChild(back);
        float fill = track * std::clamp(percent, 0, 100) / 100.f;
        if (fill > 0) {
            auto bar = RoundedBox::create({std::max(h, fill), h}, h / 2, colour);
            bar->setAnchorPoint({0, 0.5f});
            bar->setPosition({x, 0});
            node->addChild(bar);
        }
        x += track + gap;
        value->setAnchorPoint({0, 0.5f});
        value->setPosition({x, 0});
        node->addChild(value);
        node->setContentSize({total, size});
        return node;
    }

    std::string formatTime(double seconds) {
        if (seconds < 0) seconds = 0;
        int ms = static_cast<int>(std::round(seconds * 1000));
        return fmt::format("{}:{:02}.{:03}", ms / 60000, (ms / 1000) % 60, ms % 1000);
    }

    void findSliders(CCNode* node, std::vector<Slider*>& out) {
        if (auto slider = typeinfo_cast<Slider*>(node)) {
            out.push_back(slider);
            return;
        }
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) findSliders(child, out);
    }

    // Lays a row of buttons out centred at `y`, shrunk to fit `maxWidth`.
    void layoutRow(std::vector<AnimatedButtonItem*> const& row, float y, float centreX, float gap, float maxWidth) {
        float total = 0;
        for (auto item : row) total += item->getContentSize().width;
        total += gap * (row.size() > 0 ? row.size() - 1 : 0);
        float scale = total > maxWidth && total > 0 ? maxWidth / total : 1.f;
        float x = centreX - total * scale / 2;
        for (auto item : row) {
            float w = item->getContentSize().width * scale;
            item->setScale(scale);
            item->setPosition({x + w / 2, y});
            x += w + gap * scale;
        }
    }
}

PauseMenu* PauseMenu::create(PauseLayer* layer) {
    auto ret = new PauseMenu();
    if (ret->init(layer)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PauseMenu::init(PauseLayer* layer) {
    if (!CCNodeRGBA::init()) return false;
    m_layer = layer;
    m_k = unitScale();
    this->setID("pause-menu"_spr);
    this->setCascadeOpacityEnabled(true);

    // GD's pause menu, hidden (never removed: other mods find their nodes).
    std::vector<Slider*> sliders;
    for (auto child : CCArrayExt<CCNode*>(layer->getChildren())) {
        findSliders(child, sliders);
        child->setVisible(false);
    }
    // Hidden sliders would still take drags: ours drive them instead.
    for (auto slider : sliders) {
        slider->setTouchEnabled(false);
        if (slider->m_touchLogic) slider->m_touchLogic->setTouchEnabled(false);
    }
    // GD makes the music slider first, then the effects one.
    if (sliders.size() == 2) {
        m_musicSlider = sliders[0];
        m_sfxSlider = sliders[1];
    }

    build();
    layout();
    m_alpha.to(1, TRANSITION_MS, Easing::In);
    this->setOpacity(0);
    this->scheduleUpdate();
    return true;
}

void PauseMenu::build() {
    float k = m_k;
    auto win = CCDirector::get()->getWinSize();
    auto play = PlayLayer::get();
    auto level = play ? play->m_level : nullptr;
    bool practice = play && play->m_isPracticeMode;

    auto dim = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(BACKGROUND_ALPHA * 255)});
    dim->setContentSize(win);
    this->addChild(dim, -1);

    // "paused", and the level underneath it.
    m_titleBlock = group();
    auto title = makeText("paused", Weight::SemiBold, TITLE_SIZE * k);
    spaceLetters(title, TITLE_SPACING * k);
    title->setColor(theme::rgb(YELLOW));
    m_titleBlock->addChild(title);
    float titleH = TITLE_SIZE * k;
    float blockH = titleH;
    if (level) {
        std::string text = level->m_levelName;
        std::string creator = level->m_creatorName;
        if (!creator.empty() && level->m_levelType != GJLevelType::Editor) text += " by " + creator;
        if (practice) text += " (practice)";
        auto desc = makeText(text, Weight::Regular, INFO_SIZE * k);
        desc->setColor({200, 200, 200});
        float maxW = win.width - 2 * BUTTON_PADDING * k;
        if (desc->getScaledContentSize().width > maxW) desc->setScale(desc->getScale() * maxW / desc->getScaledContentSize().width);
        blockH += 6 * k + INFO_SIZE * k;
        desc->setPosition({0, -blockH / 2 + INFO_SIZE * k / 2});
        m_titleBlock->addChild(desc);
    }
    title->setPosition({0, blockH / 2 - titleH / 2});
    m_titleBlock->setContentSize({0, blockH});
    this->addChild(m_titleBlock);

    // Retry count and progress (GameplayMenuOverlay.updateInfoText).
    m_infoBlock = group();
    std::vector<CCNode*> lines;
    if (play) {
        lines.push_back(infoLine("Retry count: ", std::to_string(std::max(0, play->m_attempts - 1)), INFO_SIZE * k));
        bool platformer = level && level->isPlatformer();
        if (platformer) {
            lines.push_back(infoLine("Time: ", formatTime(play->m_attemptTime), INFO_SIZE * k));
            int best = level->m_bestTime;
            lines.push_back(infoLine("Best time: ", best > 0 ? formatTime(best / 1000.0) : "none", INFO_SIZE * k));
        } else {
            lines.push_back(infoLine("Song progress: ", fmt::format("{}%", play->getCurrentPercentInt()), INFO_SIZE * k));
            if (level) {
                auto bars = group();
                auto normal = progressBar("normal", level->m_normalPercent.value(), GREEN, k);
                auto practiceBar = progressBar("practice", level->m_practicePercent, BLUE_LIGHT, k);
                float gap = 30 * k;
                float w1 = normal->getContentSize().width, w2 = practiceBar->getContentSize().width;
                if (w1 + gap + w2 <= win.width - 40 * k) {
                    normal->setPosition({-(w1 + gap + w2) / 2 + w1 / 2, 0});
                    practiceBar->setPosition({(w1 + gap + w2) / 2 - w2 / 2, 0});
                    bars->setContentSize({w1 + gap + w2, 14 * k});
                } else {
                    normal->setPosition({0, 10 * k});
                    practiceBar->setPosition({0, -10 * k});
                    bars->setContentSize({std::max(w1, w2), 34 * k});
                }
                bars->addChild(normal);
                bars->addChild(practiceBar);
                lines.push_back(bars);
            }
        }
    }
    float lineGap = 6 * k;
    float infoH = 0;
    for (auto line : lines) infoH += line->getContentSize().height;
    if (!lines.empty()) infoH += lineGap * (lines.size() - 1);
    float y = infoH / 2;
    for (auto line : lines) {
        float h = line->getContentSize().height;
        line->setPosition({0, y - h / 2});
        y -= h + lineGap;
        m_infoBlock->addChild(line);
    }
    m_infoBlock->setContentSize({0, infoH});
    this->addChild(m_infoBlock);

    // Everything clickable is in one menu, like GD's own buttons here.
    m_menu = CCMenu::create();
    m_menu->setID("pause-buttons"_spr);
    m_menu->setPosition({0, 0});
    m_menu->setCascadeOpacityEnabled(true);
    this->addChild(m_menu, 1);

    PauseLayer* layer = m_layer;
    float buttonW = win.width - 2 * BUTTON_PADDING * k;
    struct Def { char const* text; ccColor4B colour; std::function<void()> action; };
    std::vector<Def> defs {
        {"Continue", GREEN, [layer] { layer->onResume(nullptr); }},
        {"Retry", YELLOW_DARK, [layer] { layer->onRestart(nullptr); }},
        // Through tryQuit: GD's "confirm exit" option asks first.
        {"Quit", QUIT_RED, [layer] { layer->tryQuit(nullptr); }},
    };
    for (auto& def : defs) {
        auto button = DialogButtonItem::create(def.text, def.colour, buttonW, BUTTON_HEIGHT * k, k, def.action);
        m_menu->addChild(button);
        m_buttons.push_back(button);
    }

    // GD's extras.
    float h = FOOTER_HEIGHT * k, text = 15 * k;
    auto extra = [&](char const* glyph, std::string const& label, std::function<void()> action) {
        auto content = iconLabel(glyph, label, text);
        float w = content->getContentSize().width + 28 * k;
        auto item = AnimatedButtonItem::create({w, h}, 10 * k, GRAY4, content, std::move(action));
        m_menu->addChild(item);
        m_extras.push_back(item);
        m_footer.push_back(item);
    };
    if (practice) extra(icon::PLAY, "normal mode", [layer] { layer->onNormalMode(nullptr); });
    else extra(icon::GEM, "practice", [layer] { layer->onPracticeMode(nullptr); });
    if (play && (practice || play->m_isTestMode || play->m_startPosObject)) {
        extra(icon::STEP_BACKWARD, "from the start", [layer] { layer->onRestartFull(nullptr); });
    }
    if (level && level->m_levelType == GJLevelType::Editor) {
        extra(icon::PEN, "edit", [layer] { layer->onEdit(nullptr); });
    }
    extra(icon::GEAR, "options", [layer] { layer->onSettings(nullptr); });
    extra(icon::VOLUME, "volume", [this] { this->openVolume(); });
}

void PauseMenu::layout() {
    float k = m_k;
    auto win = CCDirector::get()->getWinSize();
    float H = win.height;
    float gap = FOOTER_GAP * k;

    int rows = (m_extras.empty() ? 0 : 1) + (m_mods.empty() ? 0 : 1);
    float footerH = FOOTER_MARGIN * k + rows * FOOTER_HEIGHT * k + (rows > 1 ? gap : 0);
    float y = FOOTER_MARGIN * k + FOOTER_HEIGHT * k / 2;
    float maxW = win.width - 40 * k;
    if (!m_mods.empty()) {
        layoutRow(m_mods, y, win.width / 2, gap, maxW);
        y += FOOTER_HEIGHT * k + gap;
    }
    if (!m_extras.empty()) layoutRow(m_extras, y, win.width / 2, gap, maxW);

    // GameplayMenuOverlay's grid: title and info centred in the space left
    // above and below the buttons. Short screens get shorter buttons.
    int n = static_cast<int>(m_buttons.size());
    float titleH = m_titleBlock->getContentSize().height;
    float infoH = m_infoBlock->getContentSize().height;
    float needed = titleH + infoH + 40 * k;
    float spacing = BUTTON_SPACING * k;
    float buttonH = BUTTON_HEIGHT * k;
    float room = H - footerH - needed - spacing * std::max(0, n - 1);
    if (n > 0 && buttonH * n > room) buttonH = std::max(40 * k, room / n);
    float buttonsH = n * buttonH + spacing * std::max(0, n - 1);
    float flex = std::max(0.f, (H - footerH - buttonsH) / 2);

    float top = H - flex;
    for (int i = 0; i < n; i++) {
        auto button = m_buttons[i];
        button->setHeight(buttonH);
        button->setPosition({win.width / 2, top - buttonH / 2 - i * (buttonH + spacing)});
    }
    m_titleBlock->setPosition({win.width / 2, H - flex / 2});
    m_infoBlock->setPosition({win.width / 2, footerH + flex / 2});
}

void PauseMenu::takeModButtons() {
    auto found = collectModButtons(m_layer);
    if (found.empty()) return;
    float k = m_k;
    float size = FOOTER_HEIGHT * k;
    for (auto item : found) {
        log::debug("Pause menu mod button: {}", item->getID().view());
        Ref<CCMenuItem> target = item;
        auto button = AnimatedButtonItem::create({size, size}, 10 * k, GRAY4, modButtonImage(item, size * 0.72f),
                                                 [target] { target->activate(); });
        m_menu->addChild(button);
        m_mods.push_back(button);
        m_footer.push_back(button);
        // Its own menu (if a mod made one) goes too; GD's are hidden already.
        if (auto parent = item->getParent()) parent->setVisible(false);
    }
    layout();
}

void PauseMenu::select(int index) {
    if (index == m_selected) return;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_buttons.size())) m_buttons[m_selected]->setSelectedState(false);
    m_selected = index;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_buttons.size())) m_buttons[m_selected]->setSelectedState(true);
}

bool PauseMenu::handleKey(enumKeyCodes key) {
    int n = static_cast<int>(m_buttons.size());
    if (n == 0 || gameplayPopupOnTop()) return false;
    switch (key) {
        // SelectionCycleFillFlowContainer: from nothing, down starts at the
        // top and up at the bottom.
        case KEY_Up:
            select(m_selected < 0 ? n - 1 : (m_selected + n - 1) % n);
            return true;
        case KEY_Down:
            select(m_selected < 0 ? 0 : (m_selected + 1) % n);
            return true;
        case KEY_Enter:
            if (m_selected < 0) return false;
            // May close the pause menu: nothing after it.
            m_buttons[m_selected]->activate();
            return true;
        default:
            return false;
    }
}

void PauseMenu::openVolume() {
    if (Dialog::isOpen()) return;
    float k = m_k;
    float w = Dialog::listWidth();
    auto percent = [](float v) { return fmt::format("{}%", static_cast<int>(std::round(v * 100))); };
    Ref<PauseLayer> layer = m_layer;
    Ref<Slider> music = m_musicSlider;
    Ref<Slider> effects = m_sfxSlider;

    // Through GD's own (hidden) sliders and handlers when they're there.
    std::function<float()> getMusic = [] { return GameManager::get()->m_bgVolume; };
    std::function<void(float)> setMusic = [](float v) {
        GameManager::get()->m_bgVolume = v;
        FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(v);
    };
    std::function<float()> getSfx = [] { return GameManager::get()->m_sfxVolume; };
    std::function<void(float)> setSfx = [](float v) {
        GameManager::get()->m_sfxVolume = v;
        FMODAudioEngine::sharedEngine()->setEffectsVolume(v);
    };
    if (music && effects) {
        getMusic = [music] { return music->getValue(); };
        setMusic = [layer, music](float v) {
            music->setValue(v);
            layer->musicSliderChanged(music->getThumb());
        };
        getSfx = [effects] { return effects->getValue(); };
        setSfx = [layer, effects](float v) {
            effects->setValue(v);
            layer->sfxSliderChanged(effects->getThumb());
        };
    }

    Dialog::Content content;
    content.icon = icon::VOLUME;
    content.header = "Volume";
    content.items = {
        SliderRow::create("Music", w, k, getMusic, setMusic, percent),
        SliderRow::create("Effects", w, k, getSfx, setSfx, percent),
    };
    content.buttons = {{"Done", Dialog::Kind::Cancel, nullptr}};
    Dialog::show(std::move(content));
}

void PauseMenu::update(float dt) {
    // Once every mod has had its customSetup: gather their buttons.
    if (!m_scanned) {
        m_scanned = true;
        takeModButtons();
    }
    // GD's own dim (the layer's colour) gives way to osu!'s.
    if (!m_layer->isCascadeOpacityEnabled() && m_layer->getOpacity() != 0) m_layer->setOpacity(0);

    if (m_alpha.update(dt) || this->getOpacity() != 255) {
        this->setOpacity(static_cast<GLubyte>(std::clamp(m_alpha.get(), 0.f, 1.f) * 255.f));
    }

#ifdef GEODE_IS_DESKTOP
    if (gameplayPopupOnTop()) return;
    auto mouse = geode::cocos::getMousePos();
    bool opening = m_lastMouse.x == -1 && m_lastMouse.y == -1;
    bool moved = mouse.x != m_lastMouse.x || mouse.y != m_lastMouse.y;
    m_lastMouse = mouse;
    for (auto item : m_footer) item->setHovered(item->isVisible() && item->containsWorldPoint(mouse));
    // Hovering a button selects it; leaving it deselects (keyboard picks
    // stay until the mouse moves).
    if (moved) {
        int hovered = -1;
        for (int i = 0; i < static_cast<int>(m_buttons.size()); i++) {
            if (m_buttons[i]->containsWorldPoint(mouse)) hovered = i;
        }
        if (hovered != m_selected) {
            if (hovered >= 0 && !opening) sfx::hover(sfx::sound::BUTTON_HOVER);
            select(hovered);
        }
    }
#endif
}

} // namespace lazer
