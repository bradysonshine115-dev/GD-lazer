// lazer cursor: the mod's osu! menu cursor (src/ui/core/MenuCursor.cpp) for
// all of Windows. The system cursors are swapped for a blank one, and the
// cursor is drawn in a click-through, always-on-top window that follows the
// mouse: it shrinks and glows pink while a button is held, taps, and the arrow
// (and hand) turn to follow a drag, spring back on release and tilt while moving.
//
// Each system cursor (text, hand, resize, busy...) has its own texture in the
// arrow's style (make_cursors.py); the busy ring spins. The overlay steps aside
// for cursors apps bring themselves, for apps that hide the cursor (games, GD
// with its own osu! cursor, videos) and for shell UI drawn above every normal
// window (Start, search...), where the system cursors come back.
// The system cursors are restored on exit, and by a watchdog process if this
// one is killed.

#include <windows.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "Easing.hpp"
#include "resource.h"

using Microsoft::WRL::ComPtr;
using namespace lazer;

namespace {

constexpr wchar_t APP_NAME[] = L"lazer cursor";
constexpr wchar_t SETTINGS_KEY[] = L"Software\\GD-lazer\\Cursor";
constexpr wchar_t RUN_KEY[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

constexpr float PI = 3.14159265358979f;
constexpr float BASE_SCALE = 0.15f; // Cursor.base_scale
constexpr float TILT_MAX = 30.f;    // degrees, moving tilt
constexpr float PINK[3] {255, 102, 170}; // OsuColour.Pink
constexpr float TEX_SIZE = 512.f; // every texture's canvas
constexpr float SPIN_SPEED = 360.f / 1.2f; // busy ring, degrees per second

enum class Shape { Arrow, Hand, IBeam, SizeNS, SizeWE, SizeNWSE, SizeNESW, SizeAll, Cross, No, Wait, AppStarting };
constexpr int TEXTURE_COUNT = 11; // AppStarting is the arrow and the busy ring

struct ShapeInfo {
    float hotX, hotY; // the click point, in texture pixels
    bool turns;       // drag rotation and tilt
};
constexpr ShapeInfo SHAPES[] {
    {16, 6, true},     // Arrow: the tip
    {200, 44, true},   // Hand: the fingertip
    {256, 256, false}, // IBeam
    {256, 256, false}, // SizeNS
    {256, 256, false}, // SizeWE
    {256, 256, false}, // SizeNWSE
    {256, 256, false}, // SizeNESW
    {256, 256, false}, // SizeAll
    {256, 256, false}, // Cross
    {256, 256, false}, // No
    {256, 256, false}, // Wait
    {16, 6, true},     // AppStarting: the arrow's
};
// AppStarting's ring, off the arrow's tip (texture pixels) and smaller.
constexpr float SPINNER_X = 350 - 16, SPINNER_Y = 390 - 6, SPINNER_SCALE = 0.6f;

// The system cursors taken over, and what they become.
struct SystemCursor {
    int id; // OCR_* / IDC_*
    Shape shape;
};
constexpr SystemCursor SYSTEM_CURSORS[] {
    {32512, Shape::Arrow},       // OCR_NORMAL
    {32651, Shape::Arrow},       // IDC_HELP
    {32649, Shape::Hand},        // OCR_HAND
    {32513, Shape::IBeam},       // OCR_IBEAM
    {32645, Shape::SizeNS},      // OCR_SIZENS
    {32644, Shape::SizeWE},      // OCR_SIZEWE
    {32642, Shape::SizeNWSE},    // OCR_SIZENWSE
    {32643, Shape::SizeNESW},    // OCR_SIZENESW
    {32646, Shape::SizeAll},     // OCR_SIZEALL
    {32515, Shape::Cross},       // OCR_CROSS
    {32648, Shape::No},          // OCR_NO
    {32514, Shape::Wait},        // OCR_WAIT
    {32650, Shape::AppStarting}, // OCR_APPSTARTING
};

// ---------------------------------------------------------------- settings

struct Settings {
    int sizePercent = 60; // the mod's "Cursor size", x100
    bool sounds = true;
    bool rotation = true; // the mod's "cursor-rotation": drag rotation and tilt
} g_settings;

DWORD readDword(HKEY key, wchar_t const* name, DWORD fallback) {
    DWORD value, size = sizeof value;
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS ? value : fallback;
}

void loadSettings() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    g_settings.sizePercent = std::clamp<int>(readDword(key, L"Size", g_settings.sizePercent), 50, 200);
    g_settings.sounds = readDword(key, L"Sounds", g_settings.sounds) != 0;
    g_settings.rotation = readDword(key, L"Rotation", g_settings.rotation) != 0;
    RegCloseKey(key);
}

void saveSettings() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
    auto put = [&](wchar_t const* name, DWORD value) {
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<BYTE const*>(&value), sizeof value);
    };
    put(L"Size", g_settings.sizePercent);
    put(L"Sounds", g_settings.sounds);
    put(L"Rotation", g_settings.rotation);
    RegCloseKey(key);
}

std::wstring exePath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool startsWithWindows() {
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

void setStartsWithWindows(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring command = L"\"" + exePath() + L"\"";
        RegSetValueExW(key, APP_NAME, 0, REG_SZ, reinterpret_cast<BYTE const*>(command.c_str()),
            DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, APP_NAME);
    }
    RegCloseKey(key);
}

// ---------------------------------------------------------------- resources

std::vector<uint8_t> loadResource(int id) {
    HRSRC info = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!info) return {};
    HGLOBAL data = LoadResource(nullptr, info);
    auto bytes = static_cast<uint8_t const*>(LockResource(data));
    return {bytes, bytes + SizeofResource(nullptr, info)};
}

// RGBA, premultiplied (as cocos loads it in the game).
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};

Image decodePng(std::vector<uint8_t> const& data) {
    Image image;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT w, h;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        || FAILED(factory->CreateStream(&stream))
        || FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), DWORD(data.size())))
        || FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))
        || FAILED(decoder->GetFrame(0, &frame))
        || FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0,
            WICBitmapPaletteTypeCustom))
        || FAILED(converter->GetSize(&w, &h)))
        return image;
    image.w = int(w);
    image.h = int(h);
    image.px.resize(size_t(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, UINT(image.px.size()), image.px.data()))) image = {};
    return image;
}

// Halved again and again: drawn at ~10% of its size, one bilinear tap per
// pixel would be jagged (the game mipmaps it for the same reason).
std::vector<Image> mipChain(Image base) {
    std::vector<Image> levels {std::move(base)};
    while (levels.back().w > 1 && levels.back().h > 1) {
        Image const& src = levels.back();
        Image dst {src.w / 2, src.h / 2, {}};
        dst.px.resize(size_t(dst.w) * dst.h * 4);
        for (int y = 0; y < dst.h; y++)
            for (int x = 0; x < dst.w; x++)
                for (int c = 0; c < 4; c++) {
                    auto at = [&](int sx, int sy) { return int(src.px[(size_t(sy) * src.w + sx) * 4 + c]); };
                    dst.px[(size_t(y) * dst.w + x) * 4 + c] =
                        uint8_t((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                }
        levels.push_back(std::move(dst));
    }
    return levels;
}

// Bilinear, (u, v) in the image's pixels.
void sample(Image const& im, float u, float v, float out[4]) {
    u -= 0.5f;
    v -= 0.5f;
    int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    float fx = u - x0, fy = v - y0;
    out[0] = out[1] = out[2] = out[3] = 0;
    auto tap = [&](int x, int y, float weight) {
        if (x < 0 || y < 0 || x >= im.w || y >= im.h) return;
        uint8_t const* p = &im.px[(size_t(y) * im.w + x) * 4];
        for (int c = 0; c < 4; c++) out[c] += p[c] * weight;
    };
    tap(x0, y0, (1 - fx) * (1 - fy));
    tap(x0 + 1, y0, fx * (1 - fy));
    tap(x0, y0 + 1, (1 - fx) * fy);
    tap(x0 + 1, y0 + 1, fx * fy);
}

struct Texture {
    std::vector<Image> base, additive;
};
Texture g_textures[TEXTURE_COUNT];

// Draws a texture into a top-down BGRA (premultiplied) buffer, over what's
// there: its point (hotX, hotY) at (atX, atY), `scale` screen pixels per
// texture pixel, turned `degrees` clockwise. The pink glow is added on top
// (additive, like the game's).
void drawTexture(uint32_t* bits, int width, int height, Texture const& texture, float hotX, float hotY,
    float atX, float atY, float scale, float degrees, float alpha, float glow) {
    if (scale <= 0 || alpha <= 0 || texture.base.empty()) return;
    auto const& baseMips = texture.base;
    auto const& additiveMips = texture.additive;

    float rad = degrees * PI / 180.f;
    float cs = std::cos(rad), sn = std::sin(rad);
    // Where the texture's corners land: only that box is drawn.
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (auto [cx, cy] : {std::pair {0.f, 0.f}, {TEX_SIZE, 0.f}, {0.f, TEX_SIZE}, {TEX_SIZE, TEX_SIZE}}) {
        float dx = (cx - hotX) * scale, dy = (cy - hotY) * scale;
        float x = atX + cs * dx - sn * dy, y = atY + sn * dx + cs * dy;
        minX = std::min(minX, x), maxX = std::max(maxX, x);
        minY = std::min(minY, y), maxY = std::max(maxY, y);
    }
    int x0 = std::max(0, int(std::floor(minX)) - 1), x1 = std::min(width, int(std::ceil(maxX)) + 1);
    int y0 = std::max(0, int(std::floor(minY)) - 1), y1 = std::min(height, int(std::ceil(maxY)) + 1);

    float inv = 1.f / scale;
    int level = std::clamp(int(std::floor(std::log2(inv))), 0, int(baseMips.size()) - 1);
    Image const& base = baseMips[level];
    Image const& additive = additiveMips[std::min(level, int(additiveMips.size()) - 1)];
    float levelScale = 1.f / float(1 << level);
    float glowK = std::clamp(glow * alpha, 0.f, 1.f) / 255.f;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            // Back from the screen to the texture: turned the other way.
            float dx = x + 0.5f - atX, dy = y + 0.5f - atY;
            float tx = hotX + (cs * dx + sn * dy) * inv;
            float ty = hotY + (-sn * dx + cs * dy) * inv;
            if (tx < -1 || ty < -1 || tx > TEX_SIZE + 1 || ty > TEX_SIZE + 1) continue;
            float b[4];
            sample(base, tx * levelScale, ty * levelScale, b);
            float r = b[0] * alpha, g = b[1] * alpha, bl = b[2] * alpha, a = b[3] * alpha;
            if (glowK > 0) {
                float add[4];
                sample(additive, tx * levelScale, ty * levelScale, add);
                r += add[0] * PINK[0] * glowK;
                g += add[1] * PINK[1] * glowK;
                bl += add[2] * PINK[2] * glowK;
            }
            if (a <= 0 && r <= 0 && g <= 0 && bl <= 0) continue;
            // Over what's there (premultiplied source-over).
            uint32_t& px = bits[size_t(y) * width + x];
            float keep = 1 - a / 255.f;
            auto byte = [](float v) { return uint32_t(std::clamp(v + 0.5f, 0.f, 255.f)); };
            px = byte(bl + (px & 0xFF) * keep) | byte(g + (px >> 8 & 0xFF) * keep) << 8
                | byte(r + (px >> 16 & 0xFF) * keep) << 16 | byte(a + (px >> 24) * keep) << 24;
        }
    }
}

// A cursor shape, its hotspot at (atX, atY). `spin` turns the busy ring.
void drawShape(uint32_t* bits, int width, int height, Shape shape, float atX, float atY, float scale, float degrees,
    float spin, float alpha, float glow) {
    std::fill(bits, bits + size_t(width) * height, 0u);
    ShapeInfo const& info = SHAPES[int(shape)];
    if (shape == Shape::AppStarting) {
        drawTexture(bits, width, height, g_textures[int(Shape::Arrow)], info.hotX, info.hotY, atX, atY, scale, degrees, alpha, glow);
        // The ring hangs off the arrow: it turns with it.
        float rad = degrees * PI / 180.f;
        float x = atX + (std::cos(rad) * SPINNER_X - std::sin(rad) * SPINNER_Y) * scale;
        float y = atY + (std::sin(rad) * SPINNER_X + std::cos(rad) * SPINNER_Y) * scale;
        drawTexture(bits, width, height, g_textures[int(Shape::Wait)], 256, 256, x, y, scale * SPINNER_SCALE, spin, alpha, glow);
        return;
    }
    if (shape == Shape::Wait) degrees = spin;
    drawTexture(bits, width, height, g_textures[int(shape)], info.hotX, info.hotY, atX, atY, scale, degrees, alpha, glow);
}

// ---------------------------------------------------------------- sound

class Sound {
public:
    bool init(std::vector<uint8_t> wav) {
        m_data = std::move(wav);
        // RIFF chunks: "fmt " and "data".
        if (m_data.size() < 12 || std::memcmp(m_data.data(), "RIFF", 4) || std::memcmp(m_data.data() + 8, "WAVE", 4)) return false;
        bool gotFormat = false;
        for (size_t at = 12; at + 8 <= m_data.size();) {
            uint32_t size;
            std::memcpy(&size, m_data.data() + at + 4, 4);
            uint8_t const* body = m_data.data() + at + 8;
            if (at + 8 + size > m_data.size()) break;
            if (!std::memcmp(m_data.data() + at, "fmt ", 4)) {
                std::memcpy(&m_format, body, std::min<size_t>(size, sizeof m_format));
                m_format.cbSize = 0;
                gotFormat = true;
            } else if (!std::memcmp(m_data.data() + at, "data", 4)) {
                m_pcm = body;
                m_pcmSize = size;
            }
            at += 8 + size + (size & 1);
        }
        if (!gotFormat || !m_pcm) return false;
        if (FAILED(XAudio2Create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        if (FAILED(m_engine->CreateMasteringVoice(&m_master))) return false;
        // A few voices, reused in turn: taps overlap.
        for (int i = 0; i < 6; i++) {
            IXAudio2SourceVoice* voice;
            if (SUCCEEDED(m_engine->CreateSourceVoice(&voice, &m_format, 0, 2.f))) m_voices.push_back(voice);
        }
        return !m_voices.empty();
    }

    // Like the mod's sfx::play(name, pitchVariation, frequency).
    void play(float pitchVariation, float frequency) {
        if (m_voices.empty()) return;
        auto voice = m_voices[m_next++ % m_voices.size()];
        voice->Stop();
        voice->FlushSourceBuffers();
        std::uniform_real_distribution<float> spread(-pitchVariation, pitchVariation);
        voice->SetFrequencyRatio(frequency * (1 + spread(m_random)));
        XAUDIO2_BUFFER buffer {};
        buffer.AudioBytes = m_pcmSize;
        buffer.pAudioData = m_pcm;
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        voice->SubmitSourceBuffer(&buffer);
        voice->Start();
    }

private:
    std::vector<uint8_t> m_data;
    WAVEFORMATEX m_format {};
    uint8_t const* m_pcm = nullptr;
    uint32_t m_pcmSize = 0;
    ComPtr<IXAudio2> m_engine;
    IXAudio2MasteringVoice* m_master = nullptr;
    std::vector<IXAudio2SourceVoice*> m_voices;
    size_t m_next = 0;
    std::mt19937 m_random {std::random_device {}()};
} g_tap;

// ---------------------------------------------------------------- system cursors

// The shared system cursors' handles (SYSTEM_CURSORS' order): the same after SetSystemCursor.
HCURSOR g_handles[std::size(SYSTEM_CURSORS)];
bool g_systemHidden = false;

// Which of ours a system cursor handle is, if any.
bool shapeOf(HCURSOR cursor, Shape& shape) {
    for (size_t i = 0; i < std::size(SYSTEM_CURSORS); i++) {
        if (cursor && cursor == g_handles[i]) {
            shape = SYSTEM_CURSORS[i].shape;
            return true;
        }
    }
    return false;
}

HCURSOR blankCursor() {
    int w = GetSystemMetrics(SM_CXCURSOR), h = GetSystemMetrics(SM_CYCURSOR);
    std::vector<uint8_t> andMask(size_t(w) * h / 8, 0xFF), xorMask(size_t(w) * h / 8, 0);
    return CreateCursor(GetModuleHandleW(nullptr), 0, 0, w, h, andMask.data(), xorMask.data());
}

void restoreSystemCursors() {
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0); // reloads the user's scheme
    g_systemHidden = false;
}

void setSystemHidden(bool hidden) {
    if (hidden == g_systemHidden) return;
    if (hidden) {
        for (auto const& cursor : SYSTEM_CURSORS) SetSystemCursor(blankCursor(), cursor.id);
        g_systemHidden = true;
    } else {
        restoreSystemCursors();
    }
}

// Shell UI (Start, search, the Alt+Tab switcher...) lives in z-order bands
// above every normal window, so above ours too: the overlay can't be seen
// there, and the system cursors have to come back. It takes the foreground
// while open. Not WindowFromPoint: that hit-tests with a message to the window
// under the cursor every frame, and a busy app (Premiere playing) stutters.
bool shellInFront() {
    using GetWindowBandFn = BOOL(WINAPI*)(HWND, DWORD*);
    static auto getWindowBand = reinterpret_cast<GetWindowBandFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetWindowBand"));
    static HWND lastWindow = nullptr;
    static bool lastResult = false;
    HWND window = GetForegroundWindow();
    if (window == lastWindow) return lastResult;
    lastWindow = window;
    DWORD band = 0;
    if (!window) lastResult = false;
    else if (getWindowBand && getWindowBand(window, &band)) lastResult = band > 1; // 1: ZBID_DESKTOP, normal windows
    else {
        wchar_t cls[64] {};
        GetClassNameW(window, cls, 64);
        lastResult = !wcscmp(cls, L"Windows.UI.Core.CoreWindow") || !wcscmp(cls, L"XamlExplorerHostIslandWindow");
    }
    return lastResult;
}

// ---------------------------------------------------------------- the cursor

struct Point {
    float x = 0, y = 0;
    Point operator-(Point o) const { return {x - o.x, y - o.y}; }
    Point operator+(Point o) const { return {x + o.x, y + o.y}; }
    Point operator*(float k) const { return {x * k, y * k}; }
    bool operator!=(Point o) const { return x != o.x || y != o.y; }
    float length() const { return std::sqrt(x * x + y * y); }
};

enum class Drag { None, Started, Rotating };

// The game's MenuCursor, in screen pixels (y down).
class Cursor {
public:
    HWND window = nullptr;

    void update(float dt) {
        float ms = dt * 1000.f;
        POINT p;
        GetCursorPos(&p);
        Point pos {float(p.x), float(p.y)};

        CURSORINFO info {sizeof info};
        GetCursorInfo(&info);
        bool shown = info.flags & CURSOR_SHOWING;
        Shape shape;
        bool ours = shapeOf(info.hCursor, shape);
        bool shell = shellInFront();
        setSystemHidden(!shell);

        // Visibility: only where Windows would show one of its cursors, and we can be seen.
        bool visible = shown && ours && !shell;
        if (ours && shape != m_shape) {
            // A new shape pops in from a bit smaller.
            if (m_visible && visible) {
                m_scale.set(0.8f);
                m_scale.to(1, 300, Easing::OutQuint);
            }
            m_shape = shape;
        }
        bool turns = SHAPES[int(m_shape)].turns;
        if (visible != m_visible) {
            m_visible = visible;
            if (visible) { // PopIn
                m_alpha.to(1, 250, Easing::OutQuint);
                m_scale.to(1, 400, Easing::OutQuint);
            } else { // PopOut
                m_alpha.to(0, 250, Easing::OutQuint);
                m_scale.to(0.6f, 250, Easing::In);
            }
            if (m_drag == Drag::None) m_rotation.to(0, 400, Easing::OutQuint);
            // Another cursor takes over (text, resize...): gone at once. An app
            // hiding the cursor (a game) lets it fade out.
            if (!visible && shown) m_alpha.set(0);
        }

        // Buttons.
        bool down = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
        if (down && !m_down) onDown(pos);
        else if (!down && m_down) onUp();
        m_down = down;

        // Drag rotation (in pixels, like osu!).
        if (m_drag != Drag::None) {
            if (pos != m_lastMovePx) onMove(pos);
            if ((m_lastMovePx - m_downPx).length() > 60) {
                // Interpolation.ValueAt(0.04, down, last, 0, elapsed): the pivot floats after the cursor.
                float f = ms > 0 ? std::min(1.f, 0.04f / ms) : 0.f;
                m_downPx = m_downPx + (m_lastMovePx - m_downPx) * f;
            }
        }

        // Tilt while moving: the arrow hangs from its tip and its body swings
        // back against the motion, more the faster it goes.
        if (m_hasPos && ms > 0) {
            Point v = (pos - m_lastPos) * (1.f / dt); // px/s, y down
            m_velocity = {damp(m_velocity.x, v.x, 0.9, ms), damp(m_velocity.y, v.y, 0.9, ms)};
        }
        float tiltTarget = 0;
        if (g_settings.rotation && turns && m_drag != Drag::Rotating && m_visible) {
            // Torque from the drag on the body (tip -> middle of the arrow, y down): positive = clockwise.
            constexpr float BODY_X = 0.6f, BODY_Y = 0.8f;
            float torque = BODY_X * -m_velocity.y - BODY_Y * -m_velocity.x;
            tiltTarget = TILT_MAX * std::tanh(torque * 0.015f / TILT_MAX);
        }
        m_tilt = damp(m_tilt, tiltTarget, 0.97, ms);
        m_spin = std::fmod(m_spin + dt * SPIN_SPEED, 360.f);

        for (auto t : {&m_alpha, &m_scale, &m_press, &m_rotation, &m_glow}) t->update(dt);
        m_lastPos = pos;
        m_hasPos = true;

        present(p);
    }

private:
    void onDown(Point pos) {
        if (!m_visible) return;
        m_press.set(1);
        m_press.to(0.9f, 800, Easing::OutQuint);
        m_glow.set(0);
        m_glow.to(1, 800, Easing::OutQuint);
        if (g_settings.rotation && m_drag != Drag::Rotating) {
            m_drag = Drag::Started;
            m_downPx = pos;
            m_lastMovePx = pos;
        }
        if (g_settings.sounds) g_tap.play(0.01f, 1.f);
    }

    void onUp() {
        m_glow.set(1);
        m_glow.to(0, 500, Easing::OutQuint);
        m_press.to(1, 500, Easing::OutElastic);
        if (m_drag != Drag::None) {
            float r = m_rotation.get();
            m_rotation.to(0, 400 * (0.5f + std::abs(r / 960)), Easing::OutElasticQuarter);
            m_drag = Drag::None;
        }
        if (m_visible && g_settings.sounds) g_tap.play(0.01f, 0.8f);
    }

    void onMove(Point px) {
        m_lastMovePx = px;
        Point offset = px - m_downPx;
        float distance = offset.length();
        // Not until it's moved a bit from where the button went down.
        if (m_drag == Drag::Started && distance > 80) m_drag = Drag::Rotating;
        if (m_drag != Drag::Rotating || distance <= 0) return;
        float degrees = std::atan2(-offset.x, offset.y) * 180.f / PI + 24.3f;
        // The shortest way round.
        float current = m_rotation.get();
        float diff = std::fmod(degrees - current, 360.f);
        if (diff < -180) diff += 360;
        if (diff > 180) diff -= 360;
        m_rotation.to(current + diff, 120, Easing::OutQuint);
    }

    void present(POINT p) {
        float alpha = std::clamp(m_alpha.get(), 0.f, 1.f);
        if (alpha <= 0) {
            if (m_shown) ShowWindow(window, SW_HIDE), m_shown = false;
            return;
        }

        UINT dpiX = 96, dpiY = 96;
        GetDpiForMonitor(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        // osu! draws it in screen pixels: texture pixels x base scale x size (x the display's scaling here).
        float size = BASE_SCALE * g_settings.sizePercent / 100.f * dpiX / 96.f;
        float scale = size * m_scale.get() * m_press.get();
        float degrees = SHAPES[int(m_shape)].turns ? m_rotation.get() + m_tilt : 0.f;
        float glow = m_glow.get();
        bool spins = m_shape == Shape::Wait || m_shape == Shape::AppStarting;

        // A square around the hotspot, room for any texture turned any way (and the elastic overshoot).
        int side = int(std::ceil(2 * 1.2f * size * std::hypot(TEX_SIZE, TEX_SIZE))) + 4;
        bool resized = ensureSurface(side);
        // The tilt eases towards its target forever: only a visible turn redraws.
        bool changed = resized || spins || m_shape != m_drawnShape || scale != m_drawn[0]
            || std::abs(degrees - m_drawn[1]) > 0.2f || alpha != m_drawn[2] || glow != m_drawn[3];
        if (changed) {
            drawShape(m_bits, m_side, m_side, m_shape, m_side / 2.f, m_side / 2.f, scale, degrees, m_spin, alpha, glow);
            m_drawn[0] = scale, m_drawn[1] = degrees, m_drawn[2] = alpha, m_drawn[3] = glow;
            m_drawnShape = m_shape;
        }
        POINT at {p.x - m_side / 2, p.y - m_side / 2};
        if (!changed && m_shown) {
            // Only moved: no new image to hand to the compositor.
            if (at.x != m_at.x || at.y != m_at.y)
                SetWindowPos(window, nullptr, at.x, at.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            m_at = at;
            return;
        }
        SIZE extent {m_side, m_side};
        POINT origin {0, 0};
        BLENDFUNCTION blend {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(window, nullptr, &at, &extent, m_dc, &origin, 0, &blend, ULW_ALPHA);
        m_at = at;
        if (!m_shown) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            m_shown = true;
        }
    }

    bool ensureSurface(int side) {
        if (side == m_side && m_dc) return false;
        if (!m_dc) m_dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi {};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), side, -side, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(m_dc, bitmap);
        if (m_bitmap) DeleteObject(old);
        m_bitmap = bitmap;
        m_bits = static_cast<uint32_t*>(bits);
        m_side = side;
        return true;
    }

    Tweened<float> m_alpha {0.f};
    Tweened<float> m_scale {1.f};
    Tweened<float> m_press {1.f};
    Tweened<float> m_rotation {0.f};
    Tweened<float> m_glow {0.f};
    bool m_visible = false;
    Shape m_shape = Shape::Arrow;
    float m_spin = 0;
    bool m_down = false;
    Drag m_drag = Drag::None;
    Point m_downPx, m_lastMovePx, m_lastPos, m_velocity;
    bool m_hasPos = false;
    float m_tilt = 0;

    HDC m_dc = nullptr;
    HBITMAP m_bitmap = nullptr;
    uint32_t* m_bits = nullptr;
    int m_side = 0;
    float m_drawn[4] {-1, -1, -1, -1};
    Shape m_drawnShape = Shape::Arrow;
    POINT m_at {};
    bool m_shown = false;
} g_cursor;

// ---------------------------------------------------------------- tray

constexpr UINT WM_TRAY = WM_APP + 1;
enum MenuId : UINT { ID_SOUNDS = 1, ID_ROTATION, ID_STARTUP, ID_EXIT, ID_SIZE = 100 };
constexpr int SIZES[] {50, 60, 80, 100, 125, 150, 200};

HICON makeIcon() {
    int side = GetSystemMetrics(SM_CXSMICON);
    std::vector<uint32_t> bits(size_t(side) * side);
    // The arrow's 312 x 442 box, fit and centred.
    float scale = side / 442.f;
    drawShape(bits.data(), side, side, Shape::Arrow, (side - 312 * scale) / 2 + 16 * scale, 6 * scale, scale, 0, 0, 1, 0);
    // Icons take straight alpha.
    for (auto& px : bits) {
        uint32_t a = px >> 24;
        if (!a) continue;
        auto un = [&](int shift) { return std::min(255u, ((px >> shift & 0xFF) * 255 + a / 2) / a) << shift; };
        px = un(0) | un(8) | un(16) | a << 24;
    }
    BITMAPV5HEADER header {};
    header.bV5Size = sizeof header;
    header.bV5Width = side;
    header.bV5Height = -side;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    HDC screen = GetDC(nullptr);
    void* dibBits = nullptr;
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &dibBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    std::memcpy(dibBits, bits.data(), bits.size() * 4);
    HBITMAP mask = CreateBitmap(side, side, 1, 1, nullptr);
    ICONINFO info {TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

NOTIFYICONDATAW g_tray {};

void addTray(HWND window) {
    g_tray.cbSize = sizeof g_tray;
    g_tray.hWnd = window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = makeIcon();
    wcscpy_s(g_tray.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_tray);
}

void showMenu(HWND window) {
    HMENU sizes = CreatePopupMenu();
    for (int i = 0; i < int(std::size(SIZES)); i++) {
        wchar_t label[16];
        swprintf_s(label, L"%.2gx", SIZES[i] / 100.0);
        AppendMenuW(sizes, MF_STRING | (SIZES[i] == g_settings.sizePercent ? MF_CHECKED : 0), ID_SIZE + i, label);
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, APP_NAME);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"Size");
    AppendMenuW(menu, MF_STRING | (g_settings.rotation ? MF_CHECKED : 0), ID_ROTATION, L"Rotation and tilt");
    AppendMenuW(menu, MF_STRING | (g_settings.sounds ? MF_CHECKED : 0), ID_SOUNDS, L"Click sounds");
    AppendMenuW(menu, MF_STRING | (startsWithWindows() ? MF_CHECKED : 0), ID_STARTUP, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(window); // or the menu won't close when clicking elsewhere
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, window, nullptr);
    DestroyMenu(menu);
    switch (id) {
        case ID_SOUNDS: g_settings.sounds = !g_settings.sounds; break;
        case ID_ROTATION: g_settings.rotation = !g_settings.rotation; break;
        case ID_STARTUP: setStartsWithWindows(!startsWithWindows()); break;
        case ID_EXIT: PostQuitMessage(0); break;
        default:
            if (id >= ID_SIZE && id < ID_SIZE + std::size(SIZES)) g_settings.sizePercent = SIZES[id - ID_SIZE];
    }
    saveSettings();
}

LRESULT CALLBACK controlProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) { // Explorer restarted
        Shell_NotifyIconW(NIM_ADD, &g_tray);
        return 0;
    }
    switch (msg) {
        case WM_TRAY:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP) showMenu(window);
            return 0;
        case WM_SETTINGCHANGE:
        case WM_DISPLAYCHANGE:
            // The user's scheme was (re)loaded: ours has to go back on.
            g_systemHidden = false;
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) restoreSystemCursors();
            return 0;
    }
    return DefWindowProcW(window, msg, wParam, lParam);
}

// Brings the system cursors back if the main process dies without doing it.
int watchdog(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return 1;
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    restoreSystemCursors();
    return 0;
}

void startWatchdog() {
    std::wstring command = L"\"" + exePath() + L"\" --watchdog " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si {sizeof si};
    PROCESS_INFORMATION pi {};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Waits for the next vertical blank: the cursor moves once per displayed frame.
class VBlank {
public:
    VBlank() {
        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0, &adapter)))
            adapter->EnumOutputs(0, &m_output);
    }
    void wait() {
        if (!m_output || FAILED(m_output->WaitForVBlank())) Sleep(4);
    }

private:
    ComPtr<IDXGIOutput> m_output;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 3 && !wcscmp(argv[1], L"--watchdog")) return watchdog(DWORD(_wtoi(argv[2])));

    HANDLE single = CreateMutexW(nullptr, TRUE, L"GD-lazer.cursor");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    loadSettings();

    for (int i = 0; i < TEXTURE_COUNT; i++) {
        g_textures[i].base = mipChain(decodePng(loadResource(IDR_TEXTURES + 2 * i)));
        g_textures[i].additive = mipChain(decodePng(loadResource(IDR_TEXTURES + 2 * i + 1)));
        if (g_textures[i].base.front().px.empty() || g_textures[i].additive.front().px.empty()) {
            MessageBoxW(nullptr, L"Couldn't load the cursor images.", APP_NAME, MB_ICONERROR);
            return 1;
        }
    }
    g_tap.init(loadResource(IDR_TAP));

    // In case a previous run was killed before its watchdog started.
    restoreSystemCursors();
    for (size_t i = 0; i < std::size(SYSTEM_CURSORS); i++)
        g_handles[i] = LoadCursorW(nullptr, MAKEINTRESOURCEW(SYSTEM_CURSORS[i].id));

    WNDCLASSW overlayClass {};
    overlayClass.lpfnWndProc = DefWindowProcW;
    overlayClass.hInstance = instance;
    overlayClass.lpszClassName = L"LazerCursorOverlay";
    RegisterClassW(&overlayClass);
    // Click-through (layered + transparent: hit tests pass through it), on top,
    // never activated, not in Alt+Tab.
    g_cursor.window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        overlayClass.lpszClassName, APP_NAME, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);

    WNDCLASSW controlClass {};
    controlClass.lpfnWndProc = controlProc;
    controlClass.hInstance = instance;
    controlClass.lpszClassName = L"LazerCursorControl";
    RegisterClassW(&controlClass);
    // A hidden top-level window (not message-only: those miss broadcasts like WM_SETTINGCHANGE).
    HWND control = CreateWindowExW(0, controlClass.lpszClassName, APP_NAME, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_cursor.window || !control) return 1;
    addTray(control);
    startWatchdog();

    VBlank vblank;
    LARGE_INTEGER frequency, last, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&last);
    ULONGLONG lastTopmost = 0;
    for (bool running = true; running;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        QueryPerformanceCounter(&now);
        float dt = std::min(0.1f, float(now.QuadPart - last.QuadPart) / float(frequency.QuadPart));
        last = now;
        g_cursor.update(dt);

        // Other topmost windows (the taskbar...) come up over ours now and then.
        if (GetTickCount64() - lastTopmost > 250) {
            SetWindowPos(g_cursor.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            lastTopmost = GetTickCount64();
        }
        vblank.wait();
    }

    Shell_NotifyIconW(NIM_DELETE, &g_tray);
    restoreSystemCursors();
    CloseHandle(single);
    return 0;
}
