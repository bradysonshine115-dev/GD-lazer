#include "LevelListingInternal.hpp"

#include "../../integrations/LevelThumbnails.hpp"
#include "../../levels/LevelLibrary.hpp"
#include "LevelFacts.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

using namespace levellisting;

using facts::Stat;
using facts::Status;

// --- the cards and the footer ---

void LevelListingOverlay::addCard(CCObject* item) {
    float k = m_k, w = m_cardW, h = m_cardH, r = CARD_RADIUS * k;
    auto level = typeinfo_cast<GJGameLevel*>(item);
    auto list = level ? nullptr : typeinfo_cast<GJLevelList*>(item);
    if (!level && !list) return;
    // Your own level or list on this device: what you'd want to know while making it.
    bool local = this->local();

    // Comes in as a whole: its parts follow its opacity.
    auto root = CCNodeRGBA::create();
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(0);
    root->setContentSize({w, h});
    root->setAnchorPoint({0, 1});
    m_list->addChild(root);

    // BeatmapCardContent: the body in Background2, with the level's picture
    // dimmed behind the text once it loads.
    auto bg = RoundedBox::create({w, h}, r, m_scheme.background2());
    bg->setPosition({w / 2, h / 2});
    root->addChild(bg, 0);

    // BeatmapCardThumbnail: at the left, shimmering until the picture comes.
    float thumbW = std::min(h * CARD_THUMB_RATIO, w * 0.4f);
    auto thumb = RoundedBox::create({thumbW, h}, r, m_scheme.background3());
    thumb->setCornerRadii(r, 0, r, 0);
    thumb->setPosition({thumbW / 2, h / 2});
    thumb->setGradient(m_scheme.background4(), 0.4f, 0);
    root->addChild(thumb, 1);
    // When there's none: what it is.
    auto fallback = makeIcon(local ? (level ? icon::PEN : icon::LIST) : (list ? icon::LAYERS : icon::IMAGE), 24 * k);
    fallback->setColor(theme::rgb(m_scheme.foreground1()));
    anchorOnGlyph(fallback);
    fallback->setPosition({thumbW / 2, h / 2});
    fallback->setVisible(false);
    root->addChild(fallback, 2);

    // A rim in the page's colour while hovered.
    auto rim = RoundedBox::create({w, h}, r, CLEAR);
    rim->setBorder(1.5f * k, m_scheme.highlight1());
    rim->setPosition({w / 2, h / 2});
    rim->setOpacity(0);
    root->addChild(rim, 5);

    float textX = thumbW + CARD_TEXT_GAP * k, textW = w - textX - CARD_TEXT_GAP * k;
    std::string name = level ? std::string(level->m_levelName) : std::string(list->m_listName);
    std::string creator = level ? std::string(level->m_creatorName) : std::string(list->m_creatorName);
    if (creator.empty()) creator = "unknown";
    if (name.empty()) name = "unnamed";

    // The rating as a pill at the top right (BeatmapSetOnlineStatusPill); for
    // your own, whether it's uploaded yet.
    float titleW = textW;
    Status status = local ? facts::localStatus(level, list)
        : level ? facts::levelStatus(level) : facts::listStatus(list);
    if (status.text) {
        auto statusLabel = makeText(status.text, Weight::Bold, 10 * k);
        statusLabel->setColor(theme::rgb(m_scheme.background6()));
        float pw = statusLabel->getScaledContentSize().width + 12 * k, ph = 17 * k;
        auto pill = RoundedBox::create({pw, ph}, ph / 2, status.color);
        pill->setCascadeOpacityEnabled(true);
        pill->setAnchorPoint({1, 0.5f});
        pill->setPosition({w - 12 * k, h - 19 * k});
        root->addChild(pill, 3);
        statusLabel->setPosition({pw / 2, ph / 2});
        pill->addChild(statusLabel);
        titleW -= pw + 8 * k;
    }

    // Title, then who made it (osu!'s title, artist and "mapped by"); for your
    // own, the song or how many levels the list has.
    auto title = makeText(name, Weight::SemiBold, 17 * k);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({textX, h - 20 * k});
    facts::fit(title, titleW);
    root->addChild(title, 2);
    std::string subtitle = "by " + creator;
    if (local) subtitle = level ? levels::songTitle(level) : fmt::format("{} levels", list->m_levels.size());
    auto by = makeText(subtitle, Weight::Regular, 13 * k);
    by->setColor(theme::rgb(m_scheme.content2()));
    by->setAnchorPoint({0, 0.5f});
    by->setPosition({textX, h - 41 * k});
    facts::fit(by, textW);
    root->addChild(by, 2);

    // The bottom line: the difficulty face with its stars, then downloads and
    // likes (osu!'s plays and favourites), the length, coins; for your own,
    // the length, objects and folder.
    float ex = textX, ey = 20 * k;
    auto iconColor = theme::rgb(m_scheme.light2());
    auto textColor = theme::rgb(m_scheme.content2());
    if (!local || list) {
        int frame = level ? facts::difficultyFrame(level) : facts::listFrame(list->m_difficulty);
        auto state = level ? facts::featureState(level) : list->m_featured ? GJFeatureState::Featured : GJFeatureState::None;
        float faceSize = 24 * k;
        auto face = facts::difficultyFace(frame, state, faceSize);
        face->setPosition({ex + faceSize / 2, ey});
        root->addChild(face, 2);
        ex += faceSize + 5 * k;
        if (level && level->m_stars.value() > 0) {
            std::vector<Stat> reward {
                {level->isPlatformer() ? icon::MOON : icon::STAR, std::to_string(level->m_stars.value())}};
            auto rewardNode = facts::statsRow(reward, 12 * k, {255, 255, 255}, {255, 255, 255});
            rewardNode->setPosition({ex, ey});
            root->addChild(rewardNode, 2);
            ex += rewardNode->getContentSize().width;
        } else {
            ex += 7 * k;
        }
    }
    std::vector<Stat> stats;
    if (local) {
        if (level) {
            stats.push_back({level->isPlatformer() ? icon::RUNNING : icon::CLOCK, facts::lengthName(level)});
            int objects = level->m_objectCount.value();
            if (objects > 0) stats.push_back({icon::CUBE, facts::withCommas(objects)});
        }
        int folder = level ? level->m_levelFolder : list->m_folder;
        if (folder > 0 && m_folder == 0) stats.push_back({icon::FOLDER, createdFolderName(folder)});
    } else {
        stats.push_back({icon::CLOUD_DOWN, facts::metric(level ? level->m_downloads : list->m_downloads)});
        stats.push_back({icon::THUMBS_UP, facts::metric(level ? level->m_likes : list->m_likes)});
        if (level) {
            stats.push_back({level->isPlatformer() ? icon::RUNNING : icon::CLOCK, facts::lengthName(level)});
            if (level->m_coins > 0) stats.push_back({icon::COINS, std::to_string(level->m_coins)});
        }
        if (list) {
            stats.push_back({icon::LAYERS, fmt::format("{} levels", list->m_levels.size())});
            if (list->m_diamonds > 0) stats.push_back({icon::GEM, facts::metric(list->m_diamonds)});
        }
    }
    auto statsNode = facts::statsRow(stats, 11 * k, iconColor, textColor);
    statsNode->setPosition({ex, ey});
    root->addChild(statsNode, 2);
    // Too much for the line: the run shrinks to fit.
    float statsW = statsNode->getContentSize().width, statsRoom = w - CARD_TEXT_GAP * k - ex;
    if (statsW > statsRoom && statsW > 0) statsNode->setScale(std::max(0.7f, statsRoom / statsW));

    Pill pill;
    pill.kind = Pill::Kind::Card;
    pill.node = root;
    pill.bg = bg;
    pill.rim = rim;
    pill.color = m_scheme.background2();
    pill.hoverColor = m_scheme.background3();
    Ref<CCObject> ref = item;
    pill.action = [this, ref] { this->openItem(ref.data()); };
    // A card pill moving (the list growing) would leave a press pointing nowhere.
    bool moved = m_cardPills.size() == m_cardPills.capacity();
    m_cardPills.push_back(std::move(pill));
    if (moved) m_pressed = nullptr;

    Card card;
    card.item = item;
    card.thumbLevel = level ? level->m_levelID.value() : list->m_levels.empty() ? 0 : list->m_levels[0];
    card.root = root;
    card.bg = bg;
    card.thumb = thumb;
    card.fallback = fallback;
    card.pill = m_cardPills.size() - 1;
    // One after the other, like osu-web's cards.
    size_t index = m_cards.size() - std::min(m_cards.size(), m_pageStart);
    card.delayMs = std::min(CARD_STAGGER_MAX, index * CARD_STAGGER);
    m_cards.push_back(std::move(card));
}

void LevelListingOverlay::addSkeleton(CCNode* parent, int count, float y0) {
    // Cards' shapes where the results will go, shimmering (osu-web's
    // placeholders): a page loading looks like the page it will be.
    float k = m_k, w = m_cardW, h = m_cardH, r = CARD_RADIUS * k;
    float spacing = CARD_SPACING * k, x0 = CARDS_PADDING * k;
    float thumbW = std::min(h * CARD_THUMB_RATIO, w * 0.4f);
    float textX = thumbW + CARD_TEXT_GAP * k, textW = w - textX - CARD_TEXT_GAP * k;
    auto second = theme::lerp(m_scheme.background3(), m_scheme.background4(), 0.5f);
    for (int i = 0; i < count; i++) {
        int col = i % m_columns, row = i / m_columns;
        float x = x0 + col * (w + spacing), y = y0 + row * (h + spacing);
        auto card = CCNode::create();
        card->setContentSize({w, h});
        card->setAnchorPoint({0, 1});
        card->setPosition({x, -y});
        parent->addChild(card);
        auto bg = RoundedBox::create({w, h}, r, m_scheme.background2());
        bg->setPosition({w / 2, h / 2});
        card->addChild(bg);
        auto thumb = RoundedBox::create({thumbW, h}, r, m_scheme.background3());
        thumb->setCornerRadii(r, 0, r, 0);
        thumb->setPosition({thumbW / 2, h / 2});
        thumb->setGradient(second, 0.4f, 0);
        card->addChild(thumb, 1);
        m_shimmer.push_back(thumb);
        auto bar = [&](float width, float height, float cy) {
            auto box = RoundedBox::create({width, height}, height / 2, m_scheme.background3());
            box->setAnchorPoint({0, 0.5f});
            box->setPosition({textX, cy});
            box->setGradient(second, 0.4f, 0);
            card->addChild(box, 1);
            m_shimmer.push_back(box);
        };
        bar(textW * 0.5f, 13 * k, h - 20 * k);
        bar(textW * 0.3f, 10 * k, h - 41 * k);
        bar(textW * 0.65f, 10 * k, 20 * k);
    }
}

float LevelListingOverlay::cardsHeight() const {
    // Your levels still being built already take their room.
    size_t cards = m_cards.size() + (m_toBuild.size() - m_toBuildNext);
    int rows = cards == 0 ? 0 : static_cast<int>((cards + m_columns - 1) / m_columns);
    return rows * (m_cardH + CARD_SPACING * m_k);
}

void LevelListingOverlay::layoutCards() {
    float k = m_k, spacing = CARD_SPACING * k, x0 = CARDS_PADDING * k;
    for (size_t i = 0; i < m_cards.size(); i++) {
        auto& card = m_cards[i];
        int col = static_cast<int>(i % m_columns), row = static_cast<int>(i / m_columns);
        card.x = x0 + col * (m_cardW + spacing);
        card.top = row * (m_cardH + spacing);
        placeCard(card);
    }
}

void LevelListingOverlay::placeCard(Card& card) {
    // Still coming in: a little below where it will sit.
    float rise = (1.f - std::clamp(card.appear.get(), 0.f, 1.f)) * CARD_RISE * m_k;
    card.root->setPosition({card.x, -(m_cardsTop + card.top) - rise});
}

void LevelListingOverlay::clearCards() {
    m_list->removeAllChildren();
    m_cards.clear();
    m_cardPills.clear();
    m_seen.clear();
    m_toBuild.clear();
    m_toBuildNext = 0;
    m_pageStart = 0;
    m_stale = false;
    m_pressed = nullptr;
}

void LevelListingOverlay::relayout() {
    float k = m_k, W = bodySize().width;
    float y = CONTROL_PADDING * k;
    m_searchRow->setPosition({0, -y});
    y += TEXTBOX_HEIGHT * k;
    // The rows under the search box, when there are any.
    if (m_rowsHeight > 0) {
        y += CONTROL_SPACING * k;
        m_rowsHolder->setVisible(true);
        m_rowsHolder->setPosition({0, -y});
        y += m_rowsHeight;
    } else {
        m_rowsHolder->setVisible(false);
    }
    y += CONTROL_PADDING * k;
    m_controlBg->setContentSize({W, y});
    m_controlBg->setPosition({0, -y});
    m_stripHolder->setPosition({0, -y});
    y += STRIP_HEIGHT * k;
    m_cardsTop = y + CARDS_TOP * k;
    layoutCards();
    float footerTop = m_cardsTop + cardsHeight();
    m_footer->setPosition({0, -footerTop});
    m_scroll->setContentHeight(footerTop + m_footerHeight + CARDS_BOTTOM * k);
}

void LevelListingOverlay::rebuildFooter() {
    float k = m_k, W = bodySize().width;
    m_footer->removeAllChildren();
    m_footerPills.clear();
    m_shimmer.clear();
    m_pressed = nullptr;
    m_dirty = false;

    if (m_countLabel) {
        std::string text;
        if (m_total >= 0) text = facts::withCommas(m_total) + (m_lists ? " lists" : " levels");
        m_countLabel->setString(text.c_str());
    }

    // Everything here hangs below the footer's top, which relayout places
    // after the cards.
    float y = 0;
    auto note = [&](std::string const& text, float size, ccColor3B color, float height) {
        auto label = makeText(text, Weight::Regular, size);
        label->setColor(color);
        label->setPosition({W / 2, -(y + height / 2)});
        m_footer->addChild(label);
        return label;
    };

    switch (m_state) {
        case State::Loading: {
            // A new search keeps the old cards (dimmed); otherwise placeholders
            // stand where the cards will go: a full page, or one more row.
            if (!m_stale) {
                int count = m_cards.empty() ? LEVELS_PER_PAGE : m_columns;
                addSkeleton(m_footer, count, 0);
                int rows = (count + m_columns - 1) / m_columns;
                y += rows * (m_cardH + CARD_SPACING * k);
            }
            break;
        }
        case State::Failed: {
            // GD's request failed: no internet, its server busy or rate limiting.
            float h = 110 * k;
            auto glyph = makeIcon(icon::TRIANGLE_EXCLAMATION, 26 * k);
            glyph->setColor(theme::rgb(m_scheme.background1()));
            anchorOnGlyph(glyph);
            glyph->setPosition({W / 2, -(y + 22 * k)});
            m_footer->addChild(glyph);
            auto message = m_cards.empty()
                ? "Couldn't load the levels. Check your connection, or try again in a moment."
                : "Couldn't load more levels. Try again in a moment.";
            y += 38 * k;
            auto label = note(message, 14 * k, theme::rgb(m_scheme.content2()), 24 * k);
            facts::fit(label, W - 2 * m_pad);
            y += 30 * k;
            float bh = TAB_HEIGHT * k + 6 * k;
            auto& retry = addTab(m_footerPills, m_footer, icon::ROTATE, "try again", bh, {W / 2, -(y + bh / 2)}, {0.5f, 0.5f},
                                 [this] {
                if (m_current) this->request(m_current, m_currentFresh);
            });
            retry.color = m_scheme.colour3();
            retry.hoverColor = theme::lerp(m_scheme.colour3(), m_scheme.highlight1(), 0.5f);
            retry.textColor = {255, 255, 255};
            y = h;
            break;
        }
        case State::Loaded: {
            size_t cards = m_cards.size() + (m_toBuild.size() - m_toBuildNext);
            if (cards == 0) {
                // NotFoundDrawable; with nothing made yet, a nudge to the "new" button.
                bool none = local() && m_query.empty() && m_folder == 0;
                std::string text = "... nope, nothing found.";
                if (none) text = m_lists ? "No lists yet. Start one with \"new list\"." : "No levels yet. Start one with \"new level\".";
                else if (!m_query.empty()) text = fmt::format("Nothing called \"{}\" here.", m_query);
                float h = NOT_FOUND_HEIGHT * k;
                auto glyph = makeIcon(none ? (m_lists ? icon::LIST : icon::PEN) : icon::SEARCH, 44 * k);
                glyph->setColor(theme::rgb(m_scheme.background1()));
                anchorOnGlyph(glyph);
                glyph->setPosition({W / 2, -(y + h / 2 - 20 * k)});
                m_footer->addChild(glyph);
                auto label = makeText(text, Weight::Regular, 16 * k);
                label->setColor(theme::rgb(m_scheme.content2()));
                facts::fit(label, W - 2 * m_pad);
                label->setPosition({W / 2, -(y + h / 2 + 24 * k)});
                m_footer->addChild(label);
                y += h;
            } else if (!m_more && !local()) {
                // The end, marked with a line either side.
                float h = 40 * k;
                auto label = note("end of results", 12 * k, theme::rgb(m_scheme.foreground1()), h);
                float lw = label->getScaledContentSize().width, span = std::min(W - 2 * m_pad, 480 * k);
                float lineW = (span - lw - 40 * k) / 2;
                for (int side = -1; side <= 1; side += 2) {
                    auto line = RoundedBox::create({lineW, 1.f * k}, 0, m_scheme.background2());
                    line->setPosition({W / 2 + side * (lw / 2 + 20 * k + lineW / 2), -(y + h / 2)});
                    m_footer->addChild(line);
                }
                y += h;
            }
            break;
        }
    }

    m_footerHeight = y;
    if (m_progressTrack) m_progressTrack->setVisible(m_state == State::Loading);
    relayout();
    // GD's hidden list registered for the wheel when it was built.
    m_scroll->claimWheel();
}

// --- the cards' thumbnails ---

void LevelListingOverlay::requestThumbnail(Card& card) {
    card.thumbRequested = true;
    if (card.thumbLevel <= 0) return showThumbnail(card, nullptr);
    Ref<CCNodeRGBA> root = card.root;
    std::weak_ptr<char> alive = m_alive;
    float top = card.top, h = m_cardH;
    thumbnails::fetch(card.thumbLevel, [alive, this, root](CCTexture2D* texture) {
        if (alive.expired() || !root->getParent()) return;
        for (auto& c : m_cards) {
            if (c.root == root) return this->showThumbnail(c, texture);
        }
    }, [alive, root, this, top, h] {
        // Its card scrolled away (or the page is gone) before its turn came: skip it.
        if (alive.expired() || !root->getParent()) return false;
        return this->nearView(top, h);
    });
}

void LevelListingOverlay::showThumbnail(Card& card, CCTexture2D* texture) {
    card.shimmer = false;
    card.thumb->clearGradient();
    if (!texture) {
        card.thumb->setFillColor(m_scheme.background3());
        card.fallback->setVisible(true);
        return;
    }
    card.thumb->setTexture(texture);
    card.thumb->setFillColor(WHITE);
    // The same picture, dimmed, across the whole card (osu!'s song select panels).
    card.bg->setTexture(texture);
    auto& pill = m_cardPills[card.pill];
    pill.color = ART_DIM;
    pill.hoverColor = ART_DIM_HOVER;
}

bool LevelListingOverlay::nearView(float top, float height) const {
    // Within a screen of the view, in either direction.
    float scroll = m_scroll->scroll(), viewH = m_scroll->getContentSize().height;
    float cardTop = m_cardsTop + top;
    return cardTop + height > scroll - viewH && cardTop < scroll + viewH * 2;
}

} // namespace lazer
