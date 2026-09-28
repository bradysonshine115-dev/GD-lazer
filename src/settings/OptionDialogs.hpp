#pragma once

namespace lazer {

// GD's option popups as osu! dialogs, run through hidden copies of GD's own
// layers so every change goes through GD's handlers.

// Display (fullscreen, borderless, resolution, texture quality; applied
// together, it reloads the game) and the advanced video options (vsync, smooth
// fix, FPS). PC only: phones have no video options.
void showGraphicsDialog();

// GD's parental control toggles.
void showParentalDialog();

} // namespace lazer
