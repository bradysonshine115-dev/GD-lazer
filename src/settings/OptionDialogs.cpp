#include "OptionDialogs.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

#include "../ui/core/Text.hpp"
#include "../ui/core/Theme.hpp"
#include "../ui/overlays/Dialog.hpp"
#include "../ui/overlays/SettingsRows.hpp"

#include <Geode/modify/MoreVideoOptionsLayer.hpp>
#include <Geode/modify/ParentalOptionsLayer.hpp>

#include <algorithm>
#include <memory>
#include <unordered_set>

using namespace geode::prelude;

namespace lazer {

namespace {
    struct Toggle {
        std::string label;
        std::string key;
        std::string description;
        Ref<CCMenuItemToggler> toggler;
    };

    // Non-null only while a hidden layer is being built.
    std::vector<Toggle>* g_capture = nullptr;
    std::unordered_set<CCMenuItemToggler*>* g_seen = nullptr;

    void collectTogglers(CCNode* node, std::vector<CCMenuItemToggler*>& out) {
        if (auto t = typeinfo_cast<CCMenuItemToggler*>(node)) out.push_back(t);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) collectTogglers(child, out);
    }

    // After GD's addToggle: the toggler it just made, with its label and info.
    void captureToggle(CCNode* layer, char const* label, char const* key, char const* description) {
        if (!g_capture) return;
        std::vector<CCMenuItemToggler*> all;
        collectTogglers(layer, all);
        CCMenuItemToggler* created = nullptr;
        for (auto t : all) {
            if (!g_seen->contains(t)) {
                g_seen->insert(t);
                created = t;
            }
        }
        std::string text = label ? label : "";
        std::replace(text.begin(), text.end(), '\n', ' ');
        g_capture->push_back({text, key ? key : "", description ? description : "", created});
    }

    // Builds a hidden GD layer and captures the toggles it adds.
    template <class T>
    std::pair<Ref<T>, std::vector<Toggle>> captureLayer() {
        std::vector<Toggle> toggles;
        std::unordered_set<CCMenuItemToggler*> seen;
        g_capture = &toggles;
        g_seen = &seen;
        Ref<T> layer = T::create();
        g_capture = nullptr;
        g_seen = nullptr;
        return {layer, std::move(toggles)};
    }

    bool isOn(Toggle const& t) {
        // The toggler's own state already accounts for options GD shows inverted.
        if (t.toggler) return t.toggler->isToggled();
        return GameManager::get()->getGameVariable(t.key.c_str());
    }

    bool flip(Toggle const& t) {
        if (t.toggler) t.toggler->activate(); // GD's onToggle: its side effects run (vsync...)
        else GameManager::get()->toggleGameVariable(t.key.c_str());
        return isOn(t);
    }

    ToggleRow* toggleRow(Toggle const& toggle, float width, float k) {
        auto row = ToggleRow::create(toggle.label, width, k, [toggle] { return isOn(toggle); }, [toggle] { return flip(toggle); });
        row->setTooltip(toggle.description);
        return row;
    }

    std::string labelText(CCLabelBMFont* label) {
        return label && label->getString() ? label->getString() : "";
    }

    constexpr float GRAPHICS_WIDTH = 640;
    constexpr int FPS_STEPS[] = {30, 60, 75, 90, 120, 144, 165, 180, 240, 280, 360, 480, 540, 1000};
}

class $modify(LazerCaptureMoreVideo, MoreVideoOptionsLayer) {
    void addToggle(char const* label, char const* key, char const* description) {
        MoreVideoOptionsLayer::addToggle(label, key, description);
        captureToggle(this, label, key, description);
    }
};

class $modify(LazerCaptureParental, ParentalOptionsLayer) {
    void addToggle(char const* label, char const* variable, char const* info) {
        ParentalOptionsLayer::addToggle(label, variable, info);
        captureToggle(this, label, variable, info);
    }
};

void showGraphicsDialog() {
    if (Dialog::isOpen()) return;
    Ref<VideoOptionsLayer> video = VideoOptionsLayer::create();
    auto [more, advanced] = captureLayer<MoreVideoOptionsLayer>();
    if (!video) return;

    float k = unitScale();
    float w = Dialog::listWidth(GRAPHICS_WIDTH);
    // What's applied now: Apply only reloads the game when something changed.
    struct Display {
        bool fullscreen, borderless, fix;
        int resolution, quality;
    };
    auto current = [video] {
        return Display {video->m_fullscreen, video->m_borderless, video->m_fix, video->m_currentResolution, video->m_textureQuality};
    };
    auto initial = current();

    std::vector<CCNode*> items;
    items.push_back(SubsectionHeaderRow::create("Display", w, k));
    auto fullscreen = ToggleRow::create("Fullscreen", w, k,
        [video] { return video->m_fullscreen; },
        [video] { video->onFullscreen(nullptr); return video->m_fullscreen; });
    fullscreen->setTooltip("Fills the whole screen. Windowed lets you pick the window's size.");
    items.push_back(fullscreen);

    auto borderless = ToggleRow::create("Borderless", w, k,
        [video] { return video->m_borderless; },
        [video] { video->onBorderless(nullptr); return video->m_borderless; });
    borderless->setTooltip("Fullscreen as a borderless window: switching to other windows is instant.");
    borderless->setShownIf([video] { return video->m_fullscreen; });
    items.push_back(borderless);

    auto fix = ToggleRow::create("Borderless fix", w, k,
        [video] { return video->m_fix; },
        [video] { video->onBorderlessFix(nullptr); return video->m_fix; });
    fix->setTooltip("Try this if borderless fullscreen shows a black screen or the wrong size.");
    fix->setShownIf([video] { return video->m_fullscreen && video->m_borderless; });
    items.push_back(fix);

    auto resolution = ChoiceRow::create("Window size", w, k,
        [video] { return labelText(video->m_selectedResolutionLabel); },
        [video](int dir) {
            if (dir < 0) video->onResolutionPrev(nullptr);
            else video->onResolutionNext(nullptr);
        });
    resolution->setTooltip("The window's size in windowed mode.");
    resolution->setShownIf([video] { return !video->m_fullscreen; });
    items.push_back(resolution);

    auto quality = ChoiceRow::create("Texture quality", w, k,
        [video] { return labelText(video->m_qualityLabel); },
        [video](int dir) {
            if (dir < 0) video->onTextureQualityPrev(nullptr);
            else video->onTextureQualityNext(nullptr);
        });
    quality->setTooltip("Sharper textures use more memory. Low can help on slow machines.");
    items.push_back(quality);

    // Advanced options wait for Apply too: everything applies together.
    struct Pending {
        std::vector<std::pair<Toggle, bool>> toggles;
        int fps = 60;
    };
    auto pending = std::make_shared<Pending>();
    pending->fps = int(std::round(GameManager::get()->m_customFPSTarget));
    for (auto const& toggle : advanced) pending->toggles.push_back({toggle, isOn(toggle)});

    if (more) {
        items.push_back(SubsectionHeaderRow::create("Advanced", w, k));
        for (size_t i = 0; i < pending->toggles.size(); i++) {
            auto const& toggle = pending->toggles[i].first;
            auto row = ToggleRow::create(toggle.label, w, k,
                [pending, i] { return pending->toggles[i].second; },
                [pending, i] { return pending->toggles[i].second = !pending->toggles[i].second; });
            row->setTooltip(toggle.description);
            items.push_back(row);
        }

        auto fps = ChoiceRow::create("FPS target", w, k,
            [pending] { return fmt::format("{}", pending->fps); },
            [pending](int dir) {
                int now = pending->fps;
                if (dir > 0) {
                    for (int v : FPS_STEPS) if (v > now) { pending->fps = v; break; }
                } else {
                    for (auto it = std::rbegin(FPS_STEPS); it != std::rend(FPS_STEPS); ++it) if (*it < now) { pending->fps = *it; break; }
                }
            });
        fps->setTooltip("Frames per second to aim for, with Unlock FPS on.");
        fps->setShownIf([pending] {
            for (auto const& [toggle, on] : pending->toggles) {
                if (toggle.key == "0116") return on; // Unlock FPS
            }
            return true;
        });
        items.push_back(fps);
    }

    auto apply = [video, more = more, pending, initial, current] {
        for (auto const& [toggle, on] : pending->toggles) {
            if (isOn(toggle) != on) flip(toggle);
        }
        if (more && more->m_fpsInput && pending->fps != int(std::round(GameManager::get()->m_customFPSTarget))) {
            // Through GD's own field and Apply, so it's clamped and saved like GD does.
            more->m_fpsInput->setString(fmt::format("{}", pending->fps));
            more->onApplyFPS(nullptr);
        }
        // Last: display changes reload the game.
        auto now = current();
        bool changed = now.fullscreen != initial.fullscreen || now.borderless != initial.borderless || now.fix != initial.fix
            || now.resolution != initial.resolution || now.quality != initial.quality;
        if (changed) video->onApply(nullptr);
    };

    Dialog::Content content;
    content.icon = icon::DESKTOP;
    content.header = "Graphics";
    content.body = "Display changes reload the game's textures (a few seconds).";
    content.width = GRAPHICS_WIDTH;
    content.listHeight = 420;
    content.items = std::move(items);
    // The hidden layers live as long as these buttons.
    content.buttons = {
        {"Apply changes", Dialog::Kind::Ok, apply},
        {"Cancel", Dialog::Kind::Cancel, [video, more = more] {}},
    };
    Dialog::show(std::move(content));
}

void showParentalDialog() {
    if (Dialog::isOpen()) return;
    auto [layer, toggles] = captureLayer<ParentalOptionsLayer>();
    if (!layer) return;
    float k = unitScale();
    float w = Dialog::listWidth();
    std::vector<CCNode*> items;
    for (auto const& toggle : toggles) items.push_back(toggleRow(toggle, w, k));

    Dialog::Content content;
    content.icon = icon::SHIELD;
    content.header = "Parental control";
    content.body = "Limits what this game shows and lets players share.";
    content.items = std::move(items);
    content.buttons = {{"Done", Dialog::Kind::Cancel, [layer = layer] {}}};
    Dialog::show(std::move(content));
}

} // namespace lazer

#else

namespace lazer {
void showGraphicsDialog() {}
void showParentalDialog() {}
} // namespace lazer

#endif
