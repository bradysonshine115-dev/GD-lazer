#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// Holds a GD sprite tree (a player icon) drawn much bigger than its texture,
// and keeps its outline sharp: the sprites' alpha is treated as coverage and
// re-cut to a one-screen-pixel edge, like the SDF text. Colour detail inside
// the icon is still the texture's, only the silhouette gets crisper.
class SharpIcon : public cocos2d::CCNodeRGBA {
public:
    static SharpIcon* create(cocos2d::CCNode* content);

    void visit() override;

private:
    bool init(cocos2d::CCNode* content);
    void applyProgram(cocos2d::CCNode* node);

    cocos2d::CCNode* m_content = nullptr;
    int m_childCount = -1; // re-applies the shader when the icon's sprites change
};

} // namespace lazer
