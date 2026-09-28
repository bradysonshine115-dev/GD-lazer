#include "BlockedSongsOverlay.hpp"

#include "../../audio/MusicPlayer.hpp"
#include "../../audio/Sfx.hpp"
#include "../core/Text.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr float PADDING = 50;
    constexpr float ROW_HEIGHT = 60;

    // One blocked song: icon, title, artist, and an unblock pill on the right.
    class BlockedSongRow : public SettingsRow {
    public:
        static BlockedSongRow* create(int songID, float width, float k, std::function<void()> unblock) {
            auto ret = new BlockedSongRow();
            if (ret->init(songID, width, k, std::move(unblock))) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void setHovered(bool hovered) override {
            m_hover.to(hovered ? 1.f : 0.f, hovered ? 100 : 500, Easing::OutQuint);
            if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
        }

        void onClick(CCPoint local) override {
            // The whole row unblocks: the pill just says what a tap does.
            if (!m_unblock) return;
            sfx::click(sfx::sound::BUTTON_SELECT);
            auto unblock = std::move(m_unblock);
            m_unblock = nullptr;
            unblock();
        }

        void update(float dt) override {
            m_hover.update(dt);
            m_bg->setOpacity(static_cast<GLubyte>(m_hover.get() * 255));
            m_pill->setFillColor(theme::lerp(theme::COLOUR3, theme::HIGHLIGHT1, m_hover.get()));
        }

    private:
        bool init(int songID, float width, float k, std::function<void()> unblock) {
            if (!SettingsRow::init()) return false;
            m_unblock = std::move(unblock);
            float h = ROW_HEIGHT * k;
            this->setContentSize({width, h});

            m_bg = RoundedBox::create({width, h - 6 * k}, 8 * k, {255, 255, 255, 14});
            m_bg->setAnchorPoint({0, 0.5f});
            m_bg->setPosition({0, h / 2});
            m_bg->setOpacity(0);
            this->addChild(m_bg);

            auto tile = RoundedBox::create({42 * k, 42 * k}, 9 * k, theme::DARK3);
            tile->setAnchorPoint({0, 0.5f});
            tile->setPosition({8 * k, h / 2});
            this->addChild(tile);
            auto note = makeIcon(icon::MUSIC, 18 * k);
            note->setPosition({8 * k + 21 * k, h / 2});
            this->addChild(note);

            auto [title, artist] = MusicPlayer::describe(songID);
            float x = 62 * k;
            float pillW = 96 * k;
            float textW = width - x - pillW - 24 * k;
            auto titleLabel = makeText(title, Weight::SemiBold, 18 * k);
            titleLabel->setAnchorPoint({0, 0.5f});
            titleLabel->setPosition({x, h / 2 + (artist.empty() ? 0 : 9 * k)});
            if (titleLabel->getScaledContentSize().width > textW) titleLabel->setScale(titleLabel->getScale() * textW / titleLabel->getScaledContentSize().width);
            this->addChild(titleLabel);
            if (!artist.empty()) {
                auto artistLabel = makeText(artist, Weight::Regular, 14 * k);
                artistLabel->setColor(theme::CONTENT2);
                artistLabel->setAnchorPoint({0, 0.5f});
                artistLabel->setPosition({x, h / 2 - 11 * k});
                this->addChild(artistLabel);
            }

            m_pill = RoundedBox::create({pillW, 30 * k}, 15 * k, theme::COLOUR3);
            m_pill->setAnchorPoint({1, 0.5f});
            m_pill->setPosition({width - 12 * k, h / 2});
            this->addChild(m_pill);
            auto pillText = makeText("unblock", Weight::SemiBold, 15 * k);
            pillText->setPosition({width - 12 * k - pillW / 2, h / 2});
            this->addChild(pillText);

            m_searchText = title;
            this->scheduleUpdate();
            return true;
        }

        std::function<void()> m_unblock;
        RoundedBox* m_bg = nullptr;
        RoundedBox* m_pill = nullptr;
        Tweened<float> m_hover {0.f};
    };
}

void BlockedSongsOverlay::present() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    auto overlay = new BlockedSongsOverlay();
    if (!overlay->init()) {
        delete overlay;
        return;
    }
    overlay->autorelease();
    // Under GD's own popups (z 105), like the profile page.
    scene->addChild(overlay, 100);
    overlay->open();
}

bool BlockedSongsOverlay::init() {
    if (!WaveOverlay::init(0, theme::Scheme {255}, icon::BAN, "blocked songs", "songs the music player skips", 72.f)) return false;
    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);
    rebuild();
    return true;
}

void BlockedSongsOverlay::rebuild() {
    float k = m_k;
    for (auto row : m_rows) removeInteractive(row);
    m_rows.clear();
    m_scroll->content()->removeAllChildren();

    auto size = bodySize();
    float pad = std::min(PADDING * k, size.width * 0.05f);
    float width = std::min(size.width - pad * 2, 900 * k);
    float x = (size.width - width) / 2;
    float y = 24 * k; // from the top, growing downwards

    auto ids = MusicPlayer::get().blocked();
    auto place = [&](SettingsRow* row) {
        row->setAnchorPoint({0, 1});
        row->setPosition({x, -y});
        m_scroll->content()->addChild(row);
        addInteractive(row);
        m_rows.push_back(row);
        y += row->getContentSize().height;
    };

    if (ids.empty()) {
        auto text = makeText("Nothing blocked. The ban button in the music player blocks a song.", Weight::Regular, 18 * k);
        text->setColor(theme::CONTENT2);
        text->setPosition({size.width / 2, -y - 30 * k});
        if (text->getScaledContentSize().width > width) text->setScale(text->getScale() * width / text->getScaledContentSize().width);
        m_scroll->content()->addChild(text);
        m_scroll->setContentHeight(y + 60 * k);
        return;
    }

    Ref<BlockedSongsOverlay> self = this;
    place(ButtonRow::create(fmt::format("Unblock all {} songs", ids.size()), width, k, [self] {
        MusicPlayer::get().unblockAll();
        // Rebuilding removes the row that's running this: next frame.
        Loader::get()->queueInMainThread([self] { self->rebuild(); });
    }));
    y += 10 * k;
    for (int id : ids) {
        place(BlockedSongRow::create(id, width, k, [self, id] {
            MusicPlayer::get().unblock(id);
            Loader::get()->queueInMainThread([self] { self->rebuild(); });
        }));
    }
    m_scroll->setContentHeight(y + 24 * k);
    m_scroll->claimWheel();
}

void BlockedSongsOverlay::onEnter() {
    WaveOverlay::onEnter();
    CCDirector::get()->getKeypadDispatcher()->addDelegate(this);
}

void BlockedSongsOverlay::onExit() {
    CCDirector::get()->getKeypadDispatcher()->removeDelegate(this);
    WaveOverlay::onExit();
}

void BlockedSongsOverlay::keyBackClicked() {
    close();
}

void BlockedSongsOverlay::onClosed() {
    this->removeFromParent();
}

bool BlockedSongsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    auto loc = touch->getLocation();
    // Rows scrolled up under the header aren't there to tap.
    if (!m_scroll->containsWorldPoint(loc)) {
        cancelPress();
        return true;
    }
    m_drag.began(m_scroll, loc);
    return true;
}

void BlockedSongsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) cancelPress();
}

void BlockedSongsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    if (m_drag.ended()) cancelPress();
    WaveOverlay::ccTouchEnded(touch, e);
}

} // namespace lazer
