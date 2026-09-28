#pragma once

#include <Geode/Geode.hpp>

namespace lazer {

// Lets go of the osu! cursor and brings back the system one, for good (the
// game is quitting: its last frames stall while it saves, and a drawn cursor
// would freeze with them).
#ifdef GEODE_IS_WINDOWS
void releaseMenuCursor();
#else
inline void releaseMenuCursor() {}
#endif

} // namespace lazer
