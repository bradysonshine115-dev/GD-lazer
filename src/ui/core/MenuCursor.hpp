#pragma once

#include <Geode/Geode.hpp>

namespace lazer {

// Lets go of the osu! cursor and brings back the system one, for good (the
// game is quitting: its last frames stall while it saves, and a drawn cursor
// would freeze with them).
#ifdef GEODE_IS_WINDOWS
void releaseMenuCursor();
// The cursor says something in its speech bubble (nothing without the cursor).
void cursorSay(std::string const& text);
#else
inline void releaseMenuCursor() {}
inline void cursorSay(std::string const&) {}
#endif

} // namespace lazer
