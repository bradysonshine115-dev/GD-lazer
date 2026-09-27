#include "SharpIcon.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace cocos2d;
using geode::cast::typeinfo_cast;

namespace lazer {

namespace {
    constexpr auto PROGRAM_KEY = "lazer.sharp-icon";

    constexpr auto VERT = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
}
)";

    // Textures are premultiplied: un-premultiply, re-cut the alpha edge around
    // 0.5 to u_sharpness (screen pixels per texel), premultiply again.
    constexpr auto FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D CC_Texture0;
uniform float u_sharpness;
void main() {
    vec4 t = texture2D(CC_Texture0, v_texCoord);
    float a = clamp((t.a - 0.5) * u_sharpness + 0.5, 0.0, 1.0);
    vec3 rgb = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);
    gl_FragColor = vec4(rgb * a, a) * v_fragmentColor;
}
)";

    CCGLProgram* program() {
        auto cache = CCShaderCache::sharedShaderCache();
        if (auto p = cache->programForKey(PROGRAM_KEY)) return p;
        auto p = new CCGLProgram();
        p->initWithVertexShaderByteArray(VERT, FRAG);
        p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
        p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        p->link();
        p->updateUniforms();
        cache->addProgram(p, PROGRAM_KEY);
        p->release();
        return p;
    }

    int countNodes(CCNode* node) {
        int n = 1;
        for (auto child : geode::cocos::CCArrayExt<CCNode*>(node->getChildren())) n += countNodes(child);
        return n;
    }
}

SharpIcon* SharpIcon::create(CCNode* content) {
    auto ret = new SharpIcon();
    if (ret->init(content)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SharpIcon::init(CCNode* content) {
    if (!CCNodeRGBA::init()) return false;
    this->setCascadeOpacityEnabled(true);
    this->setCascadeColorEnabled(true);
    m_content = content;
    this->addChild(content);
    return true;
}

void SharpIcon::applyProgram(CCNode* node) {
    if (auto sprite = typeinfo_cast<CCSprite*>(node)) {
        // Sprites drawn through a batch node use the batch's shader: leave those.
        if (!sprite->getBatchNode()) sprite->setShaderProgram(program());
    }
    for (auto child : geode::cocos::CCArrayExt<CCNode*>(node->getChildren())) applyProgram(child);
}

void SharpIcon::visit() {
    if (!this->isVisible() || !m_content) return CCNodeRGBA::visit();
    int count = countNodes(m_content);
    if (count != m_childCount) {
        m_childCount = count;
        applyProgram(m_content);
    }

    // Screen pixels per texel of the icon's textures.
    auto t = m_content->nodeToWorldTransform();
    float worldScale = std::sqrt(t.a * t.a + t.b * t.b);
    float pxPerTexel = worldScale / CC_CONTENT_SCALE_FACTOR() * CCEGLView::sharedOpenGLView()->getScaleX();

    auto p = program();
    p->use();
    static GLuint s_program = 0;
    static GLint s_location = -1;
    if (s_program != p->getProgram()) {
        s_program = p->getProgram();
        s_location = glGetUniformLocation(s_program, "u_sharpness");
    }
    // Shrunk icons keep their normal soft edge.
    p->setUniformLocationWith1f(s_location, std::max(1.f, pxPerTexel));
    CCNodeRGBA::visit();
}

} // namespace lazer
