#include "Text.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Must match SPREAD in tools/gen_sdf_fonts.py: atlas pixels of distance on
    // each side of the outline.
    constexpr float SDF_SPREAD = 6.f;
    constexpr auto PROGRAM_KEY = "lazer.sdf-text";

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

    // The atlas alpha is the distance field (0.5 on the outline). u_sharpness
    // turns it into a one-screen-pixel-wide edge. Colours arrive premultiplied.
    constexpr auto FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D CC_Texture0;
uniform float u_sharpness;
void main() {
    float d = texture2D(CC_Texture0, v_texCoord).a;
    float a = clamp((d - 0.5) * u_sharpness + 0.5, 0.0, 1.0);
    gl_FragColor = v_fragmentColor * a;
}
)";

    CCGLProgram* sdfProgram() {
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

    // For labels we don't own (no draw() override to set the sharpness per
    // label): the vertex shader works it out from the MVP matrix instead.
    // u_pxScale = screen pixels per point * half the window width / content scale;
    // the 12 is 2 * SDF_SPREAD.
    constexpr auto HALO_PROGRAM_KEY = "lazer.sdf-text-halo";

    constexpr auto HALO_VERT = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
uniform float u_pxScale;
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
varying float v_sharpness;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
    float pxPerTexel = length(CC_MVPMatrix[0].xy) / gl_Position.w * u_pxScale;
    v_sharpness = max(1.0, 12.0 * pxPerTexel);
}
)";

    // Fill as in FRAG, plus a dark halo up to 3 screen pixels out (as far as
    // the atlas's spread allows: small text gets about one), solid near the
    // letter and fading outwards, under the fill.
    constexpr auto HALO_FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
varying float v_sharpness;
uniform sampler2D CC_Texture0;
void main() {
    float d = texture2D(CC_Texture0, v_texCoord).a;
    float fill = clamp((d - 0.5) * v_sharpness + 0.5, 0.0, 1.0);
    float reach = min(0.48, 3.0 / v_sharpness);
    float halo = smoothstep(0.5 - reach, 0.5 - reach * 0.4, d) * 0.85 * v_fragmentColor.a;
    vec4 c = v_fragmentColor * fill;
    gl_FragColor = c + vec4(0.0, 0.0, 0.0, halo) * (1.0 - c.a);
}
)";

    CCGLProgram* haloProgram() {
        auto cache = CCShaderCache::sharedShaderCache();
        if (auto p = cache->programForKey(HALO_PROGRAM_KEY)) return p;
        auto p = new CCGLProgram();
        p->initWithVertexShaderByteArray(HALO_VERT, HALO_FRAG);
        p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
        p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
        p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
        p->link();
        p->updateUniforms();
        cache->addProgram(p, HALO_PROGRAM_KEY);
        p->release();
        return p;
    }

    // A bitmap-font label drawn from a distance-field atlas: crisp at any scale.
    class SdfLabel : public CCLabelBMFont {
    public:
        static SdfLabel* create(std::string const& text, char const* font) {
            auto ret = new SdfLabel();
            if (ret->initWithString(text.c_str(), font)) {
                ret->setShaderProgram(sdfProgram());
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void draw() override {
            // Screen pixels per atlas pixel, so the edge stays a pixel wide.
            auto t = this->nodeToWorldTransform();
            float worldScale = std::sqrt(t.a * t.a + t.b * t.b);
            float pxPerTexel = worldScale / CC_CONTENT_SCALE_FACTOR() * CCEGLView::sharedOpenGLView()->getScaleX();

            auto program = this->getShaderProgram();
            program->use();
            // Looked up again after a re-link (a GL context reset gives a new program).
            static GLuint s_program = 0;
            static GLint s_location = -1;
            if (s_program != program->getProgram()) {
                s_program = program->getProgram();
                s_location = glGetUniformLocation(s_program, "u_sharpness");
            }
            program->setUniformLocationWith1f(s_location, std::max(1.f, 2.f * SDF_SPREAD * pxPerTexel));
            CCLabelBMFont::draw();
        }
    };

    CCLabelBMFont* makeLabel(std::string const& text, char const* font, float size) {
        CCLabelBMFont* label = SdfLabel::create(text, font);
        // Scale by the font's line height rather than the text's bounds, so
        // every label of the same `size` gets the same scale.
        float lineHeight = label->getConfiguration()->m_nCommonHeight / CC_CONTENT_SCALE_FACTOR();
        if (lineHeight > 0) label->setScale(size / lineHeight);
        return label;
    }
}

CCLabelBMFont* makeText(std::string const& text, Weight weight, float size) {
    switch (weight) {
        case Weight::Regular: return makeLabel(text, "outfit-regular-sdf.fnt"_spr, size);
        case Weight::SemiBold: return makeLabel(text, "outfit-semibold-sdf.fnt"_spr, size);
        case Weight::Bold: return makeLabel(text, "outfit-bold-sdf.fnt"_spr, size);
    }
    return nullptr;
}

char const* sdfFont(Weight weight) {
    switch (weight) {
        case Weight::Regular: return "outfit-regular-sdf.fnt"_spr;
        case Weight::SemiBold: return "outfit-semibold-sdf.fnt"_spr;
        case Weight::Bold: return "outfit-bold-sdf.fnt"_spr;
    }
    return nullptr;
}

void useHaloShader(CCLabelBMFont* label) {
    auto program = haloProgram();
    label->setShaderProgram(program);
    // Set on every use: the window (and so the pixels per point) can change.
    program->use();
    float pxScale = CCDirector::get()->getWinSize().width * CCEGLView::sharedOpenGLView()->getScaleX()
        / (2.f * CC_CONTENT_SCALE_FACTOR());
    program->setUniformLocationWith1f(glGetUniformLocation(program->getProgram(), "u_pxScale"), pxScale);
}

CCLabelBMFont* makeIcon(char const* glyph, float size) {
    return makeLabel(glyph, "icons-sdf.fnt"_spr, size);
}

// Greedy word wrap using the label's own measurements.
CCNode* makeWrappedText(std::string const& text, float size, float maxWidth, ccColor3B color) {
    auto holder = CCNode::create();
    std::vector<std::string> lines;
    std::string line, word;
    auto measure = [&](std::string const& s) {
        auto l = makeText(s, Weight::Regular, size);
        return l->getScaledContentSize().width;
    };
    auto flushWord = [&] {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            flushWord();
            if (c == '\n') { lines.push_back(line); line.clear(); }
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);

    float lineHeight = size * 1.15f;
    float width = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        auto l = makeText(lines[i], Weight::Regular, size);
        l->setColor(color);
        l->setAnchorPoint({0, 1});
        l->setPosition({0, -lineHeight * i});
        holder->addChild(l);
        width = std::max(width, l->getScaledContentSize().width);
    }
    holder->setContentSize({width, lineHeight * lines.size()});
    return holder;
}

} // namespace lazer
