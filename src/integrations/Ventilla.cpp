#include "Ventilla.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>

using namespace geode::prelude;

namespace lazer::ventilla {

namespace {
    constexpr auto ID = "joseii.ventilla";
    constexpr int MAX_CHANNELS = 1024; // the scan stops at FMOD's own limit before this
    constexpr double SCAN_INTERVAL_S = 0.5;

    FMOD::Channel* g_channel = nullptr;
    double g_lastScan = -1;

    double nowS() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    // Ventilla opens "http://ventilla.5infin.es:8100/radio.mp3"; FMOD keeps
    // just the last part of that as the sound's name.
    bool isRadioSound(FMOD::Sound* sound) {
        char name[512] = {};
        if (!sound || sound->getName(name, sizeof name) != FMOD_OK) return false;
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
        return n.ends_with("radio.mp3") || n.find("ventilla") != std::string::npos || n.find("5infin.es") != std::string::npos;
    }

    // Channel handles get reused once a sound stops: check it's still the radio.
    bool stillRadio(FMOD::Channel* channel) {
        FMOD::Sound* sound = nullptr;
        return channel && channel->getCurrentSound(&sound) == FMOD_OK && isRadioSound(sound);
    }
}

Mod* mod() {
    return Loader::get()->getLoadedMod(ID);
}

bool loaded() {
    return mod() != nullptr;
}

bool radioOn() {
    auto m = mod();
    return m && m->getSettingValue<bool>("enabled") && !GameManager::get()->getGameVariable("0122");
}

FMOD::Channel* channel() {
    if (!loaded()) return nullptr;
    if (stillRadio(g_channel)) return g_channel;
    g_channel = nullptr;

    double now = nowS();
    if (g_lastScan >= 0 && now - g_lastScan < SCAN_INTERVAL_S) return nullptr;
    g_lastScan = now;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return nullptr;
    // GD's pool is 128 channels; the scan stops where FMOD says so.
    for (int i = 0; i < MAX_CHANNELS; i++) {
        FMOD::Channel* ch = nullptr;
        if (engine->m_system->getChannel(i, &ch) != FMOD_OK) break;
        if (stillRadio(ch)) {
            g_channel = ch;
            log::info("Ventilla's radio found on FMOD channel {}", i);
            break;
        }
    }
    return g_channel;
}

std::string title() {
    auto ch = channel();
    if (!ch) return "";
    FMOD::Sound* sound = nullptr;
    if (ch->getCurrentSound(&sound) != FMOD_OK || !sound) return "";
    int count = 0, updated = 0;
    if (sound->getNumTags(&count, &updated) != FMOD_OK) return "";
    std::string title;
    for (int i = 0; i < count; i++) {
        FMOD_TAG tag {};
        if (sound->getTag(nullptr, i, &tag) != FMOD_OK || !tag.name || !tag.data) continue;
        std::string_view name = tag.name;
        if (name != "StreamTitle" && name != "TITLE") continue;
        title.assign(static_cast<char const*>(tag.data), tag.datalen);
        while (!title.empty() && title.back() == '\0') title.pop_back();
    }
    return title;
}

bool playing() {
    auto ch = channel();
    if (!ch) return false;
    bool isPlaying = false;
    float volume = 0;
    return ch->isPlaying(&isPlaying) == FMOD_OK && isPlaying && ch->getVolume(&volume) == FMOD_OK && volume > 0.001f;
}

bool paused() {
    auto ch = channel();
    bool p = false;
    return ch && ch->getPaused(&p) == FMOD_OK && p;
}

void setPaused(bool paused) {
    if (auto ch = channel()) ch->setPaused(paused);
}

CCTexture2D* logo() {
    if (!loaded()) return nullptr;
    auto path = (dirs::getModRuntimeDir() / ID / "logo.png").string();
    return CCTextureCache::sharedTextureCache()->addImage(path.c_str(), false);
}

} // namespace lazer::ventilla
