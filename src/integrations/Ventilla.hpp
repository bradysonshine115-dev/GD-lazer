#pragma once

#include <string>

namespace FMOD { class Channel; }
namespace geode { class Mod; }
namespace cocos2d { class CCTexture2D; }

// JoseII's Ventilla (joseii.ventilla): a live radio streamed in the menus. It
// plays on an FMOD channel of its own over GD's menu loop (which it mutes), and
// has no API: its channel is found by the stream's name, and the track title
// read from the stream's own tags, the way Ventilla itself shows it.
namespace lazer::ventilla {

// The mod, or nullptr when it isn't installed.
geode::Mod* mod();
bool loaded();
// Its radio is switched on (Ventilla's "Enable Radio", and GD's menu music not muted).
bool radioOn();

// The channel the stream plays on, or nullptr while it isn't connected.
FMOD::Channel* channel();
// The stream's current track title, "" while unknown.
std::string title();
// Connected and audible (Ventilla turns its channel down when it steps aside).
bool playing();
bool paused();
void setPaused(bool paused);

// Ventilla's logo, for the music player's card (nullptr if it can't be read).
cocos2d::CCTexture2D* logo();

} // namespace lazer::ventilla
