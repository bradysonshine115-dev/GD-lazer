#pragma once

namespace lazer {

// GD's daily chests as one of osu!'s dialogs: both chests side by side, and
// any that's ready opens by itself the moment the dialog is up, the way GD
// opens one (it drops in, the lid lifts, it bursts), with what it held popping
// out under it. A hidden RewardsPage does the server work and the timers, and
// a hidden RewardUnlockLayer is what GD hands the reward through.
void showRewards();

} // namespace lazer
