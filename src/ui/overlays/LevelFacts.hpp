#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <utility>
#include <vector>

// What the level pages (the listing's cards, the level page) show about a
// level or a list, and the small pieces they draw it with: numbers the way
// osu! shows them, GD's difficulty faces, osu!'s status colours for GD's
// ratings, runs of icon + text, player icons.
namespace lazer::facts {

// "1,234" and "1.2k" (osu!'s ToMetric).
std::string withCommas(long long v);
std::string metric(long long n);
// "1:23.456" (a platformer time, in ms).
std::string formatTime(int ms);

// GJDifficultySprite's frame (-1 auto, 0 N/A, 1-5, 6-10 demons) for a level,
// and for a list's difficulty (a list numbers its demons differently).
int difficultyFrame(GJGameLevel* level);
int listFrame(int listDifficulty);
GJFeatureState featureState(GJGameLevel* level);
// The difficulty face at `size` (its feature glow fades with its parent).
cocos2d::CCNode* difficultyFace(int frame, GJFeatureState state, float size);
// The level's length in a word ("long", "platformer").
std::string lengthName(GJGameLevel* level);

// BeatmapSetOnlineStatusPill: a rating and OsuColour's colour for it. A null
// text is no pill (an unrated level: most are, and a pill would be noise).
struct Status {
    char const* text;
    cocos2d::ccColor4B color;
};
Status levelStatus(GJGameLevel* level);
Status listStatus(GJLevelList* list);
// Your own level or list: whether it's on GD's servers yet.
Status localStatus(GJGameLevel* level, GJLevelList* list);

// BeatmapCardStatistic: a run of small icon + text pairs (a null icon is
// text alone). The node's origin is at the left, on the text's middle line.
using Stat = std::pair<char const*, std::string>;
cocos2d::CCNode* statsRow(std::vector<Stat> const& items, float size, cocos2d::ccColor3B iconColor,
                          cocos2d::ccColor3B textColor);

// Shrinks a label to fit `maxWidth`, cutting it with an ellipsis if it would get too small.
void fit(cocos2d::CCLabelBMFont* label, float maxWidth);

// A player's icon in their colours (their main one, whichever kind), `size` units tall.
cocos2d::CCNode* playerIcon(GJUserScore* score, float size);

// osu!'s LoadingSpinner glyph, turning around its own centre (turn the holder).
cocos2d::CCNode* makeSpinner(float size, cocos2d::ccColor3B color);

} // namespace lazer::facts
