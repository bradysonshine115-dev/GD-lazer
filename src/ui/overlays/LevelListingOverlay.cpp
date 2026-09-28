#include "LevelListingOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../../integrations/LevelThumbnails.hpp"
#include "../../levels/LevelLibrary.hpp"
#include "../core/Text.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Blue overlay scheme: the beatmap listing's.
    constexpr theme::Scheme SCHEME {200};

    // osu! sizes (768 px tall screen), scaled by m_k.
    constexpr float HORIZONTAL_PADDING = 50.f;  // WaveOverlayContainer
    constexpr float CONTROL_PADDING = 20.f;     // BeatmapListingSearchControl, top and bottom
    constexpr float CONTROL_SPACING = 20.f;     // between the search box and the filter rows
    constexpr float TEXTBOX_HEIGHT = 35.f;      // SearchTextBox
    constexpr float TEXTBOX_RADIUS = 5.f;       // OsuTextBox
    constexpr float TEXTBOX_SIDE = 10.f;        // OsuTextBox.LeftRightPadding
    constexpr float SEARCH_ICON = 16.f;         // BasicSearchTextBox's magnifier (20 px box)
    constexpr float ROWS_PADDING = 10.f;        // the filter rows' horizontal padding
    constexpr float ROW_LABEL_WIDTH = 100.f;    // BeatmapSearchFilterRow's label column
    constexpr float ROW_LINE = 20.f;            // one line of 13 px tabs plus their 5 px spacing
    constexpr float ROW_SPACING = 5.f;
    constexpr float TAB_SPACING = 10.f;         // between filter tabs
    constexpr float TAB_PILL_LEFT = 16.f;       // MultipleSelectionFilterTabItem: room for the "x"
    constexpr float TAB_PILL_RIGHT = 4.f;
    constexpr float STRIP_HEIGHT = 40.f;        // the sort strip
    constexpr float STRIP_MARGIN = 20.f;
    constexpr float HEADER_BUTTON = 20.f;       // HeaderButton (sort tabs, refresh)
    constexpr float CARD_WIDTH = 345.f;         // BeatmapCard.WIDTH
    constexpr float CARD_HEIGHT = 80.f;         // BeatmapCardNormal.HEIGHT
    constexpr float CARD_RADIUS = 8.f;          // BeatmapCard.CORNER_RADIUS
    constexpr float CARD_SPACING = 10.f;
    constexpr float CARDS_PADDING = 20.f;       // panelTarget's horizontal padding
    constexpr float CARDS_TOP = 15.f;
    constexpr float CARDS_BOTTOM = 20.f;
    constexpr float NOT_FOUND_HEIGHT = 160.f;   // NotFoundDrawable (250 with osu!'s picture)
    constexpr float CARD_TRANSITION = 360.f;    // BeatmapCard.TRANSITION_DURATION
    constexpr float CARD_FADE = 200.f;          // new cards fade in
    constexpr float QUERY_DEBOUNCE = 500.f;     // BeatmapListingFilterControl's
    constexpr float FILTER_DEBOUNCE = 100.f;
    // GD reports its own request failures; this catches one that never reports.
    constexpr float LOAD_TIMEOUT_MS = 30000.f;
    constexpr float SPIN_SPEED = 300.f;         // spinner, degrees per second
    constexpr int PAGE_SIZE = 10;               // levels per GD page
    constexpr int QUERY_LIMIT = 20;             // GD's search box

    constexpr ccColor4B CLEAR {0, 0, 0, 0};
    constexpr ccColor4B WHITE {255, 255, 255, 255};
    constexpr ccColor4B BLACK {0, 0, 0, 255};
    // OsuColour's status colours (ForBeatmapSetOnlineStatus), for GD's ratings.
    constexpr ccColor4B LIME1 {0xb3, 0xd9, 0x44, 255};
    constexpr ccColor4B BLUE1 {0x66, 0xcc, 0xff, 255};
    constexpr ccColor4B ORANGE1 {0xff, 0xcc, 0x22, 255};
    constexpr ccColor4B PURPLE1 {0x88, 0x66, 0xee, 255};
    constexpr ccColor4B PINK1 {0xff, 0x66, 0xaa, 255};
    constexpr ccColor4B GRAY {0x99, 0x99, 0x99, 255};

    // The filter rows (GD's search screen's options, as osu!'s rows).
    struct RowDef {
        char const* label;
        std::vector<char const*> options;
        bool multi;
    };
    RowDef const ROWS[] = {
        // In GameLevelManager::getDifficultyStr's order.
        {"Difficulty", {"N/A", "easy", "normal", "hard", "harder", "insane", "demon", "auto"}, true},
        {"Demon", {"any", "easy", "medium", "hard", "insane", "extreme"}, false},
        // In getLengthStr's order.
        {"Length", {"tiny", "short", "medium", "long", "XL", "platformer"}, true},
        {"General", {"rated", "unrated", "featured", "epic", "legendary", "mythic", "original", "coins", "two player"}, true},
        {"Played", {"any", "uncompleted", "completed"}, false},
        {"Type", {"levels", "lists"}, false},
    };
    constexpr int DIFF_DEMON = 6;
    enum General { RATED, UNRATED, FEATURED, EPIC, LEGENDARY, MYTHIC, ORIGINAL, COINS, TWO_PLAYER };

    // The quick searches GD's search screen offers: the sort tabs of the search page.
    struct SortDef {
        char const* label;
        SearchType type;
    };
    constexpr SortDef SORTS[] = {
        {"most downloaded", SearchType::Downloaded},
        {"most liked", SearchType::MostLiked},
        {"trending", SearchType::Trending},
        {"recent", SearchType::Recent},
        {"magic", SearchType::Magic},
        {"awarded", SearchType::Awarded},
    };

    bool quickType(SearchType type) {
        if (type == SearchType::Search) return true;
        for (auto const& sort : SORTS) if (sort.type == type) return true;
        return false;
    }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    bool nodeShown(CCNode* node) {
        for (auto n = node; n; n = n->getParent()) {
            if (!n->isVisible()) return false;
        }
        return true;
    }

    std::string withCommas(long long v) {
        auto s = fmt::format("{}", v);
        for (int i = int(s.size()) - 3; i > (s[0] == '-' ? 1 : 0); i -= 3) s.insert(size_t(i), ",");
        return s;
    }

    // 1234 -> "1.2k", like osu!'s ToMetric.
    std::string metric(long long n) {
        std::string sign = n < 0 ? "-" : "";
        if (n < 0) n = -n;
        auto one = [](double v, char suffix) {
            auto s = fmt::format("{:.1f}", v);
            if (s.ends_with(".0")) s.resize(s.size() - 2);
            return s + suffix;
        };
        if (n < 1000) return sign + std::to_string(n);
        if (n < 1000000) return sign + one(n / 1000.0, 'k');
        return sign + one(n / 1000000.0, 'M');
    }

    std::string trim(std::string s) {
        auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (!s.empty() && space(s.back())) s.pop_back();
        size_t start = 0;
        while (start < s.size() && space(s[start])) start++;
        return s.substr(start);
    }

    std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Shrinks a label to fit `maxWidth`, cutting it with an ellipsis if it would get too small.
    void fit(CCLabelBMFont* label, float maxWidth) {
        float base = label->getScale();
        float w = label->getScaledContentSize().width;
        if (w <= maxWidth) return;
        if (w * 0.85f <= maxWidth) {
            label->setScale(base * maxWidth / w);
            return;
        }
        std::string text = label->getString();
        while (text.size() > 1 && label->getScaledContentSize().width > maxWidth) {
            text.pop_back();
            label->setString((text + "...").c_str());
        }
    }

    // osu!'s LoadingSpinner glyph. The glyph doesn't sit in the middle of its
    // label's line box: the holder turns around the glyph's own centre.
    CCNode* makeSpinner(float size, ccColor3B color) {
        auto holder = CCNode::create();
        auto glyph = makeIcon(icon::CIRCLE_NOTCH, size);
        glyph->setColor(color);
        glyph->setAnchorPoint({0, 0});
        if (auto letter = glyph->getChildByType<CCSprite>(0)) {
            glyph->setPosition(-letter->getPosition() * glyph->getScale());
        }
        holder->addChild(glyph);
        return holder;
    }

    // BeatmapCardStatistic: a run of small icon + text pairs (8 px icon, 11 px text).
    CCNode* statsRow(std::vector<std::pair<char const*, std::string>> const& items, float size, ccColor3B iconColor) {
        auto row = CCNodeRGBA::create();
        row->setCascadeOpacityEnabled(true);
        float x = 0;
        for (auto const& [glyph, text] : items) {
            if (glyph) {
                auto icon = makeIcon(glyph, size * 0.75f);
                icon->setColor(iconColor);
                icon->setAnchorPoint({0, 0.5f});
                icon->setPosition({x, 0});
                row->addChild(icon);
                x += icon->getScaledContentSize().width + size * 0.35f;
            }
            auto label = makeText(text, Weight::Regular, size);
            label->setAnchorPoint({0, 0.5f});
            label->setPosition({x, 0});
            row->addChild(label);
            x += label->getScaledContentSize().width + size * 0.75f;
        }
        row->setContentSize({x, size});
        return row;
    }

    // GJDifficultySprite's frame for a level (-1 auto, 0 N/A, 1-5, 6-10 demons).
    int difficultyFrame(GJGameLevel* level) {
        if (level->m_autoLevel) return -1;
        if (level->m_demon.value() > 0) {
            switch (level->m_demonDifficulty) {
                case 3: return 7;  // easy demon
                case 4: return 8;  // medium
                case 5: return 9;  // insane
                case 6: return 10; // extreme
                default: return 6; // hard
            }
        }
        return std::clamp(level->getAverageDifficulty(), 0, 5);
    }

    // A list's difficulty numbers its demons differently (GJLevelList::frameForListDifficulty).
    int listFrame(int diff) {
        if (diff == 0) return -1; // auto
        switch (diff) {
            case 6: return 7;
            case 7: return 8;
            case 8: return 6;
            default: return diff >= 1 && diff <= 10 ? diff : 0;
        }
    }

    GJFeatureState featureState(GJGameLevel* level) {
        switch (level->m_isEpic) {
            case 1: return GJFeatureState::Epic;
            case 2: return GJFeatureState::Legendary;
            case 3: return GJFeatureState::Mythic;
            default: return level->m_featured > 0 ? GJFeatureState::Featured : GJFeatureState::None;
        }
    }

    CCNode* difficultyFace(int frame, GJFeatureState state, float size) {
        auto face = GJDifficultySprite::create(frame, GJDifficultyName::Short);
        face->updateFeatureState(state);
        face->setCascadeOpacityEnabled(true); // the feature glow fades with it
        auto s = face->getContentSize();
        face->setScale(size / std::max(1.f, std::max(s.width, s.height)));
        return face;
    }

    // BeatmapSetOnlineStatusPill: the rating as a small coloured pill.
    struct Status {
        char const* text;
        ccColor4B color;
    };
    Status levelStatus(GJGameLevel* level) {
        switch (level->m_isEpic) {
            case 3: return {"mythic", PINK1};
            case 2: return {"legendary", PURPLE1};
            case 1: return {"epic", ORANGE1};
            default: break;
        }
        if (level->m_featured > 0) return {"featured", LIME1};
        if (level->m_stars.value() > 0) return {"rated", BLUE1};
        return {"unrated", GRAY};
    }

    struct PageText {
        char const* icon;
        std::string title;
        std::string description;
    };
    // OverlayTitle: the page's icon, title and description.
    PageText pageText(LevelBrowserLayer* owner, GJSearchObject* search, bool searchPage) {
        bool lists = search->m_searchMode == 1;
        if (searchPage) return {icon::SEARCH, "search", "browse for new levels and lists"};
        switch (search->m_searchType) {
            case SearchType::Featured:
                if (lists) return {icon::LAYERS, "lists", "level lists RobTop featured"};
                return {icon::STAR, "featured", "levels RobTop picked out"};
            case SearchType::HallOfFame: return {icon::AWARD, "hall of fame", "the epic, legendary and mythic levels"};
            case SearchType::Magic: return {icon::WAND_MAGIC, "magic", "good levels that haven't been found yet"};
            case SearchType::Recent: return {icon::CLOCK, "recent", "the newest uploads"};
            case SearchType::Sent: return {icon::PAPER_PLANE, "sent", "levels sent to RobTop for a rating"};
            case SearchType::Followed: return {icon::USER_CHECK, "followed", "levels by creators you follow"};
            case SearchType::Friends: return {icon::USERS, "friends", "levels by your friends"};
            case SearchType::Downloaded: return {icon::CLOUD_DOWN, "most downloaded", "the most downloaded levels"};
            case SearchType::MostLiked: return {icon::THUMBS_UP, "most liked", "the most liked levels"};
            case SearchType::Trending: return {icon::BOLT, "trending", "what's popular right now"};
            case SearchType::Awarded: return {icon::MEDAL, "awarded", "the latest rated levels"};
            case SearchType::Search: return {icon::SEARCH, "search results", "levels matching your search"};
            default: break;
        }
        // GD's own title for anything else.
        std::string title = lower(std::string(owner->getSearchTitle()));
        if (title.empty()) title = lists ? "lists" : "levels";
        return {lists ? icon::LAYERS : icon::LIST, title, lists ? "level lists" : "online levels"};
    }

    int itemID(CCObject* item) {
        if (auto level = typeinfo_cast<GJGameLevel*>(item)) return level->m_levelID.value();
        if (auto list = typeinfo_cast<GJLevelList*>(item)) return list->m_listID;
        return 0;
    }
}

bool LevelListingOverlay::wants(GJSearchObject* search) {
    if (!search || !Mod::get()->getSettingValue<bool>("enabled")) return false;
    switch (search->m_searchType) {
        case SearchType::Search:
        case SearchType::Downloaded:
        case SearchType::MostLiked:
        case SearchType::Trending:
        case SearchType::Recent:
        case SearchType::Featured:
        case SearchType::Magic:
        case SearchType::Awarded:
        case SearchType::Followed:
        case SearchType::Friends:
        case SearchType::HallOfFame:
        case SearchType::Sent:
        case SearchType::StarAward:
            return true;
        default:
            return false;
    }
}

CCScene* LevelListingOverlay::pageScene(SearchType type) {
    auto search = GJSearchObject::create(type);
    // Magic and recent are quick searches too: this keeps them their own page.
    search->setUserObject("plain"_spr, CCBool::create(true));
    return LevelBrowserLayer::scene(search);
}

CCScene* LevelListingOverlay::searchScene(std::string const& query) {
    auto search = query.empty()
        ? GJSearchObject::create(SearchType::Downloaded)
        : GJSearchObject::create(SearchType::Search, query);
    return LevelBrowserLayer::scene(search);
}

LevelListingOverlay* LevelListingOverlay::create(LevelBrowserLayer* owner, GJSearchObject* search) {
    auto ret = new LevelListingOverlay();
    if (ret->init(owner, search)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool LevelListingOverlay::init(LevelBrowserLayer* owner, GJSearchObject* search) {
    m_owner = owner;
    // The quick searches share the search page; a marked one is shown plain.
    bool searchPage = quickType(search->m_searchType) && !search->getUserObject("plain"_spr);
    m_mode = searchPage ? Mode::Search : Mode::Plain;
    m_lists = search->m_searchMode == 1;
    auto text = pageText(owner, search, searchPage);
    if (!WaveOverlay::init(0, SCHEME, text.icon, text.title, text.description)) return false;
    m_alive = std::make_shared<char>(0);
    m_pad = HORIZONTAL_PADDING * m_k;
    m_rowPills.resize(static_cast<size_t>(Row::Count));
    m_rowX.resize(m_rowPills.size());
    m_rowY.resize(m_rowPills.size());
    m_rowW.resize(m_rowPills.size());
    if (m_mode == Mode::Search) readSearch(search);

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);

    // The controls are built once; the cards and the footer under them change.
    float k = m_k;
    float y = 0;
    if (m_mode == Mode::Search) y = buildSearchControl(y) + 10 * k; // BeatmapListingFilterControl's spacing
    y = buildStrip(y);
    m_cardsTop = y + CARDS_TOP * k;

    // As many osu!-width columns as fit, stretched to fill the row.
    float avail = bodySize().width - 2 * CARDS_PADDING * k, spacing = CARD_SPACING * k;
    m_columns = std::max(1, static_cast<int>((avail + spacing) / (CARD_WIDTH * k + spacing)));
    m_cardW = (avail - (m_columns - 1) * spacing) / m_columns;
    m_cardH = CARD_HEIGHT * k;

    m_list = CCNode::create();
    m_scroll->content()->addChild(m_list);
    m_footer = CCNode::create();
    m_scroll->content()->addChild(m_footer);
    rebuildSort();

    // GD's browser asked for the first page while it was built: that request is ours now.
    m_current = search;
    m_key = search->getKey();
    m_state = State::Loading;
    rebuildFooter();
    return true;
}

// --- GD's search object <-> the controls ---

void LevelListingOverlay::readSearch(GJSearchObject* search) {
    m_query = trim(std::string(search->m_searchQuery));
    if (search->m_searchType != SearchType::Search) m_sort = search->m_searchType;
    // "1,2,-2": the server's difficulty numbers (-1 N/A, 1-5, -2 demon, -3 auto).
    for (auto const& part : utils::string::split(std::string(search->m_difficulty), ",")) {
        int v = utils::numFromString<int>(part).unwrapOr(99);
        int bit = v == -1 ? 0 : v >= 1 && v <= 5 ? v : v == -2 ? DIFF_DEMON : v == -3 ? 7 : -1;
        if (bit >= 0) m_difficulty |= 1u << bit;
    }
    for (auto const& part : utils::string::split(std::string(search->m_length), ",")) {
        int v = utils::numFromString<int>(part).unwrapOr(-1);
        if (v >= 0 && v <= 5) m_length |= 1u << v;
    }
    m_demon = std::clamp(static_cast<int>(search->m_demonFilter), 0, 5);
    if (m_demon > 0) m_difficulty = 1u << DIFF_DEMON;
    auto flag = [this](bool on, General bit) { if (on) m_general |= 1u << bit; };
    flag(search->m_starFilter, RATED);
    flag(search->m_noStarFilter, UNRATED);
    flag(search->m_featuredFilter, FEATURED);
    flag(search->m_epicFilter, EPIC);
    flag(search->m_legendaryFilter, LEGENDARY);
    flag(search->m_mythicFilter, MYTHIC);
    flag(search->m_originalFilter, ORIGINAL);
    flag(search->m_coinsFilter, COINS);
    flag(search->m_twoPlayerFilter, TWO_PLAYER);
    m_played = search->m_uncompletedFilter ? 1 : search->m_completedFilter ? 2 : 0;
}

GJSearchObject* LevelListingOverlay::makeSearch(int page) {
    auto glm = GameLevelManager::sharedState();
    auto diff = [this](int i) { return ((m_difficulty >> i) & 1) != 0; };
    auto len = [this](int i) { return ((m_length >> i) & 1) != 0; };
    auto general = [this](General bit) { return ((m_general >> bit) & 1) != 0; };
    std::string difficulty = glm->getDifficultyStr(diff(0), diff(1), diff(2), diff(3), diff(4), diff(5), diff(6), diff(7));
    std::string length = glm->getLengthStr(len(0), len(1), len(2), len(3), len(4), len(5));
    // A query searches by name (or ID); without one, the sort tab picks GD's quick search.
    SearchType type = m_query.empty() ? m_sort : SearchType::Search;
    return GJSearchObject::create(type, m_query, difficulty, length, page,
                                  general(RATED), m_played == 1, general(FEATURED), 0, general(ORIGINAL),
                                  general(TWO_PLAYER), false, false, general(UNRATED), general(COINS),
                                  general(EPIC), general(LEGENDARY), general(MYTHIC), m_played == 2,
                                  m_demon, 0, m_lists ? 1 : 0);
}

// --- loading (through GD's browser) ---

void LevelListingOverlay::request(GJSearchObject* search, bool fresh) {
    if (!search || m_leaving) return;
    if (fresh) {
        clearCards();
        m_total = -1;
        m_more = true;
        m_scroll->scrollTo(0);
    }
    m_current = search;
    m_key = search->getKey();
    m_pendingTotal = m_pendingEnd = -1;
    m_state = State::Loading;
    m_loadingMs = 0;
    // GD's browser fetches the page and gets the result (see the hooks below).
    // A cached page comes back before this returns.
    m_owner->loadPage(search);
    rebuildFooter();
}

void LevelListingOverlay::startSearch() {
    m_searchDelay = -1;
    rebuildSort();
    request(makeSearch(0), true);
}

void LevelListingOverlay::queueSearch(float delayMs) {
    m_searchDelay = delayMs;
}

void LevelListingOverlay::loadMore() {
    if (m_state != State::Loaded || !m_more || !m_current) return;
    request(m_current->getNextPageObject(), false);
}

void LevelListingOverlay::refresh() {
    if (m_state == State::Loading) return;
    GJSearchObject* search = nullptr;
    if (m_mode == Mode::Search) search = makeSearch(0);
    else if (m_current) search = m_current->getPageObject(0);
    if (!search) return;
    // GD keeps pages for a while; a refresh wants fresh ones.
    GameLevelManager::sharedState()->resetTimerForKey(search->getKey());
    request(search, true);
}

void LevelListingOverlay::levelsLoaded(CCArray* items, char const* key) {
    // Only the page asked for: a stale one (the filters changed mid-load) is dropped.
    if (!key || m_key != key) return;
    m_lastKey = m_key;
    m_key.clear();

    int count = items ? static_cast<int>(items->count()) : 0;
    int added = 0;
    if (items) {
        for (auto item : CCArrayExt<CCObject*>(items)) {
            int id = itemID(item);
            if (id && m_seen.contains(id)) continue; // pages can overlap, like osu-web's
            if (id) m_seen.insert(id);
            addCard(item);
            added++;
        }
    }
    if (m_pendingTotal >= 0) m_total = m_pendingTotal;
    // Another page? GD says how far this one reached; failing that, a short page is the last.
    if (count == 0 || added == 0) m_more = false;
    else if (m_pendingTotal >= 0 && m_pendingEnd >= 0) m_more = m_pendingEnd < m_pendingTotal;
    else m_more = count >= PAGE_SIZE;
    m_pendingTotal = m_pendingEnd = -1;
    m_state = State::Loaded;
    layoutCards();
    rebuildFooter();
}

void LevelListingOverlay::levelsFailed(char const* key) {
    if (!key || m_key != key) return;
    m_key.clear();
    m_state = State::Failed;
    rebuildFooter();
}

void LevelListingOverlay::pageInfo(std::string const& info, char const* key) {
    if (!key) return;
    // "total:start:count" (GameLevelManager::createPageInfo). GD sends it with
    // the page; whether before or after it, the numbers land.
    auto parts = utils::string::split(info, ":");
    if (parts.size() < 3) return;
    int total = utils::numFromString<int>(parts[0]).unwrapOr(-1);
    int start = utils::numFromString<int>(parts[1]).unwrapOr(-1);
    int count = utils::numFromString<int>(parts[2]).unwrapOr(-1);
    if (total < 0 || start < 0 || count < 0) return;
    if (m_key == key) {
        m_pendingTotal = total;
        m_pendingEnd = start + count;
    } else if (m_lastKey == key) {
        m_total = total;
        if (m_state == State::Loaded && !m_cards.empty()) m_more = start + count < total;
        m_dirty = true;
    }
}

void LevelListingOverlay::openItem(CCObject* item) {
    if (m_leaving) return;
    // GD's own cells push the page, so its back button returns to this list.
    CCScene* scene = nullptr;
    if (auto level = typeinfo_cast<GJGameLevel*>(item)) scene = LevelInfoLayer::scene(level, false);
    else if (auto list = typeinfo_cast<GJLevelList*>(item)) scene = LevelListLayer::scene(list);
    if (!scene) return;
    if (m_input) m_input->defocus();
    CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
}

void LevelListingOverlay::goBack() {
    if (m_leaving) return;
    m_leaving = true;
    if (m_input) m_input->defocus();
    sfx::play(sfx::sound::WAVE_POP_OUT);
    // GD's creator hub, which the menu hook turns into wherever the player came from.
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, CreatorLayer::scene()));
}

// --- building ---

LevelListingOverlay::Pill& LevelListingOverlay::addPill(std::vector<Pill>& list, CCNode* parent, CCSize size, float radius,
                                                        CCPoint pos, CCPoint anchor, ccColor4B color, ccColor4B hoverColor,
                                                        std::function<void()> action) {
    auto node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint(anchor);
    node->setPosition(pos);
    parent->addChild(node, 1);
    auto bg = RoundedBox::create(size, radius, color);
    bg->setPosition({size.width / 2, size.height / 2});
    node->addChild(bg);

    Pill pill;
    pill.node = node;
    pill.bg = bg;
    pill.color = color;
    pill.hoverColor = hoverColor;
    pill.action = std::move(action);
    list.push_back(std::move(pill));
    m_pressed = nullptr; // the list may have moved
    return list.back();
}

float LevelListingOverlay::buildSearchControl(float y) {
    float k = m_k, W = bodySize().width;
    auto content = m_scroll->content();
    float top = y;
    y += CONTROL_PADDING * k;

    // The search box (BasicSearchTextBox): a rounded field with a magnifier at the right.
    float boxX = m_pad, boxW = W - 2 * m_pad, boxH = TEXTBOX_HEIGHT * k;
    float boxCy = -(y + boxH / 2);
    auto field = RoundedBox::create({boxW, boxH}, TEXTBOX_RADIUS * k, m_scheme.background5());
    field->setAnchorPoint({0, 0.5f});
    field->setPosition({boxX, boxCy});
    content->addChild(field);
    auto magnifier = makeIcon(icon::SEARCH, SEARCH_ICON * k);
    magnifier->setColor(theme::rgb(m_scheme.content2()));
    magnifier->setAnchorPoint({1, 0.5f});
    magnifier->setPosition({boxX + boxW - TEXTBOX_SIDE * k, boxCy});
    content->addChild(magnifier, 1);

    // GD's text input (Geode's box is 30 tall: scaled so its text suits the page).
    float scale = k;
    float inputW = boxW - TEXTBOX_SIDE * 2 * k - 35 * k; // TextFlow.Padding.Right = 35
    m_input = TextInput::create(inputW / scale, "type in keywords...", "outfit-regular.fnt"_spr);
    m_input->hideBG();
    m_input->setTextAlign(TextInputAlign::Left);
    m_input->setScale(scale);
    m_input->setAnchorPoint({0, 0.5f});
    m_input->setPosition({boxX + TEXTBOX_SIDE * k, boxCy});
    m_input->setMaxCharCount(QUERY_LIMIT);
    if (!m_query.empty()) m_input->setString(m_query);
    m_input->setDelegate(this);
    content->addChild(m_input, 2);
    y += boxH + CONTROL_SPACING * k;

    // The filter rows: a label column, then the tabs flowing to the right.
    float rowX = m_pad + ROWS_PADDING * k;
    float rowW = W - rowX - m_pad - ROWS_PADDING * k;
    for (int r = 0; r < static_cast<int>(Row::Count); r++) {
        if (r > 0) y += ROW_SPACING * k;
        y = buildFilterRow(static_cast<Row>(r), y, rowX, rowW);
    }
    y += CONTROL_PADDING * k;

    // Its background (Dark6), under everything built above.
    auto bg = CCLayerColor::create(m_scheme.dark6());
    bg->setContentSize({W, y - top});
    bg->setPosition({0, -y});
    content->addChild(bg, -1);
    return y;
}

float LevelListingOverlay::buildFilterRow(Row row, float y, float x, float width) {
    float k = m_k;
    auto content = m_scroll->content();
    auto const& def = ROWS[static_cast<int>(row)];
    size_t r = static_cast<size_t>(row);

    auto label = makeText(def.label, Weight::Regular, 13 * k);
    label->setAnchorPoint({0, 0.5f});
    label->setPosition({x, -(y + ROW_LINE * k / 2)});
    content->addChild(label, 1);

    m_rowX[r] = x + ROW_LABEL_WIDTH * k;
    m_rowY[r] = y;
    m_rowW[r] = width - ROW_LABEL_WIDTH * k;

    // FilterTabItem: the name in Light2, bold (or on a white pill, for the
    // multi-select rows) while chosen. Placed by layoutRow.
    float widest = 0; // the row with every tab chosen, for the lines it needs
    int lines = 1;
    float lineX = 0;
    for (size_t i = 0; i < def.options.size(); i++) {
        auto normal = makeText(def.options[i], Weight::Regular, 13 * k);
        auto bold = makeText(def.options[i], def.multi ? Weight::SemiBold : Weight::Bold, 13 * k);
        float textW = std::max(normal->getScaledContentSize().width, bold->getScaledContentSize().width);
        auto& tab = addPill(m_fixedPills, content, {textW, ROW_LINE * k}, 0, {m_rowX[r], -(y + ROW_LINE * k / 2)}, {0, 0.5f},
                            CLEAR, CLEAR, [this, row, i] { this->toggleOption(row, static_cast<int>(i)); });
        tab.kind = Pill::Kind::FilterTab;
        tab.bg->setVisible(false);
        tab.label = normal;
        tab.boldLabel = bold;
        for (auto l : {normal, bold}) {
            l->setAnchorPoint({0, 0.5f});
            tab.node->addChild(l, 2);
        }
        if (def.multi) {
            float pillH = (ROW_LINE - 3) * k;
            tab.activeBg = RoundedBox::create({textW, pillH}, pillH / 2, WHITE);
            tab.node->addChild(tab.activeBg, 1);
            tab.activeIcon = makeIcon(icon::CIRCLE_XMARK, 10 * k);
            tab.activeIcon->setColor(theme::rgb(m_scheme.background4()));
            tab.node->addChild(tab.activeIcon, 3);
        }
        tab.active = [this, row, i] { return this->optionActive(row, static_cast<int>(i)); };
        tab.usable = [this, row] { return this->rowUsable(row); };
        m_rowPills[r].push_back(m_fixedPills.size() - 1);

        widest = def.multi ? textW + (TAB_PILL_LEFT + TAB_PILL_RIGHT) * k : textW;
        if (lineX > 0 && lineX + widest > m_rowW[r]) {
            lineX = 0;
            lines++;
        }
        lineX += widest + TAB_SPACING * k;
    }
    layoutRow(row);
    return y + lines * ROW_LINE * k;
}

float LevelListingOverlay::buildStrip(float y) {
    float k = m_k, W = bodySize().width, h = STRIP_HEIGHT * k;
    auto content = m_scroll->content();
    auto bg = CCLayerColor::create(m_scheme.background4());
    bg->setContentSize({W, h});
    bg->setPosition({0, -(y + h)});
    content->addChild(bg);
    float cy = -(y + h / 2);

    if (m_mode == Mode::Search) {
        // OverlaySortTabControl: "Sort by" and the tabs (rebuilt by rebuildSort).
        auto sortLabel = makeText("Sort by", Weight::SemiBold, 12 * k);
        sortLabel->setAnchorPoint({0, 0.5f});
        sortLabel->setPosition({STRIP_MARGIN * k, cy});
        content->addChild(sortLabel, 1);
        m_sortHolder = CCNode::create();
        m_sortHolder->setPosition({STRIP_MARGIN * k + sortLabel->getScaledContentSize().width + 10 * k, cy});
        content->addChild(m_sortHolder, 1);
    }

    // Refresh at the right (GD's lists have one), and how many there are.
    auto refreshIcon = makeIcon(icon::ROTATE, 10 * k);
    auto refreshLabel = makeText("refresh", Weight::SemiBold, 12 * k);
    float iconW = refreshIcon->getScaledContentSize().width;
    float bh = HEADER_BUTTON * k;
    float w = iconW + 5 * k + refreshLabel->getScaledContentSize().width + 20 * k;
    auto& refresh = addPill(m_fixedPills, content, {w, bh}, 3 * k, {W - STRIP_MARGIN * k, cy}, {1, 0.5f}, CLEAR,
                            m_scheme.background3(), [this] { this->refresh(); });
    refreshIcon->setPosition({10 * k + iconW / 2, bh / 2});
    refresh.node->addChild(refreshIcon, 1);
    refreshLabel->setAnchorPoint({0, 0.5f});
    refreshLabel->setPosition({10 * k + iconW + 5 * k, bh / 2});
    refresh.node->addChild(refreshLabel, 1);

    m_countLabel = makeText("", Weight::SemiBold, 12 * k);
    m_countLabel->setColor(theme::rgb(m_scheme.content2()));
    m_countLabel->setAnchorPoint({1, 0.5f});
    m_countLabel->setPosition({W - STRIP_MARGIN * k - w - 10 * k, cy});
    content->addChild(m_countLabel, 1);
    return y + h;
}

void LevelListingOverlay::rebuildSort() {
    if (m_mode != Mode::Search || !m_sortHolder) return;
    m_sortHolder->removeAllChildren();
    m_sortPills.clear();
    m_pressed = nullptr;
    float k = m_k, h = HEADER_BUTTON * k;

    // With a query GD sorts by relevance only; without one the quick searches
    // are the sorts (like osu!'s Relevance tab appearing with a query).
    std::vector<SortDef> tabs;
    if (!m_query.empty()) tabs.push_back({"relevance", SearchType::Search});
    else tabs.assign(std::begin(SORTS), std::end(SORTS));

    float x = 0;
    for (auto const& def : tabs) {
        auto type = def.type;
        auto normal = makeText(def.label, Weight::SemiBold, 12 * k);
        auto bold = makeText(def.label, Weight::Bold, 12 * k);
        float w = std::max(normal->getScaledContentSize().width, bold->getScaledContentSize().width) + 20 * k;
        auto& tab = addPill(m_sortPills, m_sortHolder, {w, h}, 3 * k, {x, 0}, {0, 0.5f}, CLEAR, m_scheme.background3(),
                            [this, type] {
            if (!m_query.empty() || m_sort == type) return;
            m_sort = type;
            queueSearch(FILTER_DEBOUNCE);
        });
        tab.kind = Pill::Kind::SortTab;
        tab.label = normal;
        tab.boldLabel = bold;
        tab.active = [this, type] { return m_query.empty() ? m_sort == type : type == SearchType::Search; };
        for (auto l : {normal, bold}) {
            l->setPosition({w / 2, h / 2});
            tab.node->addChild(l, 1);
        }
        x += w + 5 * k;
    }
}

void LevelListingOverlay::addCard(CCObject* item) {
    float k = m_k, w = m_cardW, h = m_cardH, r = CARD_RADIUS * k;
    auto level = typeinfo_cast<GJGameLevel*>(item);
    auto list = level ? nullptr : typeinfo_cast<GJLevelList*>(item);
    if (!level && !list) return;

    // Fades in as a whole: its parts follow its opacity.
    auto root = CCNodeRGBA::create();
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(0);
    root->setContentSize({w, h});
    root->setAnchorPoint({0, 1});
    m_list->addChild(root);

    // BeatmapCardContent: the body in Background2 (Background4 while hovered).
    auto bg = RoundedBox::create({w, h}, r, m_scheme.background2());
    bg->setPosition({w / 2, h / 2});
    root->addChild(bg, 0);

    // BeatmapCardThumbnail: a square at the left, the cover once it loads.
    auto thumb = RoundedBox::create({h, h}, r, m_scheme.background3());
    thumb->setCornerRadii(r, 0, r, 0);
    thumb->setPosition({h / 2, h / 2});
    root->addChild(thumb, 1);

    // Title, then who made it (osu!'s title, artist and "mapped by").
    float textX = h + 10 * k, textW = w - textX - 10 * k;
    std::string name = level ? std::string(level->m_levelName) : std::string(list->m_listName);
    std::string creator = level ? std::string(level->m_creatorName) : std::string(list->m_creatorName);
    if (creator.empty()) creator = "unknown";
    auto title = makeText(name, Weight::SemiBold, 18 * k);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({textX, h - 15 * k});
    fit(title, textW);
    root->addChild(title, 2);
    auto by = makeText("by " + creator, Weight::SemiBold, 14 * k);
    by->setAnchorPoint({0, 0.5f});
    by->setPosition({textX, h - 32 * k});
    fit(by, textW);
    root->addChild(by, 2);

    // Statistics: downloads and likes (osu!'s plays and favourites), the length.
    auto iconColor = theme::rgb(m_scheme.content2());
    std::vector<std::pair<char const*, std::string>> stats;
    stats.push_back({icon::CLOUD_DOWN, metric(level ? level->m_downloads : list->m_downloads)});
    stats.push_back({icon::THUMBS_UP, metric(level ? level->m_likes : list->m_likes)});
    if (level && !level->isPlatformer()) stats.push_back({icon::CLOCK, levels::lengthName(level->m_levelLength)});
    if (list) {
        stats.push_back({icon::LAYERS, fmt::format("{} levels", list->m_levels.size())});
        if (list->m_diamonds > 0) stats.push_back({icon::GEM, metric(list->m_diamonds)});
    }
    auto statsNode = statsRow(stats, 11 * k, iconColor);
    statsNode->setPosition({textX, 30.5f * k});
    root->addChild(statsNode, 2);

    // BeatmapCardExtraInfoRow: the rating pill, then the difficulty (osu!'s spectrum).
    float ex = textX, ey = 14 * k;
    Status status = level ? levelStatus(level) : list->m_featured ? Status {"featured", LIME1} : Status {nullptr, GRAY};
    if (status.text) {
        auto statusLabel = makeText(status.text, Weight::Bold, 10 * k);
        statusLabel->setColor(theme::rgb(m_scheme.background6()));
        float pw = statusLabel->getScaledContentSize().width + 10 * k, ph = 16 * k;
        auto pill = RoundedBox::create({pw, ph}, ph / 2, status.color);
        pill->setCascadeOpacityEnabled(true);
        pill->setAnchorPoint({0, 0.5f});
        pill->setPosition({ex, ey});
        root->addChild(pill, 2);
        statusLabel->setPosition({pw / 2, ph / 2});
        pill->addChild(statusLabel);
        ex += pw + 6 * k;
    }
    int frame = level ? difficultyFrame(level) : listFrame(list->m_difficulty);
    auto state = level ? featureState(level) : list->m_featured ? GJFeatureState::Featured : GJFeatureState::None;
    auto face = difficultyFace(frame, state, 18 * k);
    face->setPosition({ex + 9 * k, ey});
    root->addChild(face, 2);
    ex += 18 * k + 5 * k;
    std::vector<std::pair<char const*, std::string>> reward;
    if (level) {
        if (level->m_stars.value() > 0) reward.push_back({level->isPlatformer() ? icon::MOON : icon::STAR, std::to_string(level->m_stars.value())});
        if (level->m_coins > 0) reward.push_back({icon::COINS, std::to_string(level->m_coins)});
    }
    if (!reward.empty()) {
        auto rewardNode = statsRow(reward, 11 * k, iconColor);
        rewardNode->setPosition({ex, ey});
        root->addChild(rewardNode, 2);
    }

    Pill pill;
    pill.kind = Pill::Kind::Card;
    pill.node = root;
    pill.bg = bg;
    pill.color = m_scheme.background2();
    pill.hoverColor = m_scheme.background4();
    Ref<CCObject> ref = item;
    pill.action = [this, ref] { this->openItem(ref.data()); };
    m_cardPills.push_back(std::move(pill));
    m_pressed = nullptr;

    Card card;
    card.item = item;
    card.thumbLevel = level ? level->m_levelID.value() : list->m_levels.empty() ? 0 : list->m_levels[0];
    card.root = root;
    card.thumb = thumb;
    card.pill = m_cardPills.size() - 1;
    card.appear.to(1.f, CARD_FADE, Easing::OutQuint);
    m_cards.push_back(std::move(card));
}

void LevelListingOverlay::layoutCards() {
    float k = m_k, spacing = CARD_SPACING * k, x0 = CARDS_PADDING * k;
    for (size_t i = 0; i < m_cards.size(); i++) {
        auto& card = m_cards[i];
        int col = static_cast<int>(i % m_columns), row = static_cast<int>(i / m_columns);
        card.top = row * (m_cardH + spacing);
        card.root->setPosition({x0 + col * (m_cardW + spacing), -(m_cardsTop + card.top)});
    }
}

void LevelListingOverlay::clearCards() {
    m_list->removeAllChildren();
    m_cards.clear();
    m_cardPills.clear();
    m_seen.clear();
    m_pressed = nullptr;
}

void LevelListingOverlay::rebuildFooter() {
    float k = m_k, W = bodySize().width;
    m_footer->removeAllChildren();
    m_footerPills.clear();
    m_spinners.clear();
    m_pressed = nullptr;
    m_dirty = false;

    if (m_countLabel) {
        std::string text;
        if (m_total >= 0) text = withCommas(m_total) + (m_lists ? " lists" : " levels");
        m_countLabel->setString(text.c_str());
    }

    float spacing = CARD_SPACING * k;
    int rows = m_cards.empty() ? 0 : static_cast<int>((m_cards.size() + m_columns - 1) / m_columns);
    float y = m_cardsTop + (rows > 0 ? rows * (m_cardH + spacing) : 0);
    auto note = [&](std::string const& text, float size, ccColor3B color, float height) {
        auto label = makeText(text, Weight::Regular, size);
        label->setColor(color);
        label->setPosition({W / 2, -(y + height / 2)});
        m_footer->addChild(label);
        return label;
    };

    switch (m_state) {
        case State::Loading: {
            // LoadingLayer's spinner, where the next cards will go.
            float h = m_cards.empty() ? 120 * k : 60 * k;
            auto spinner = makeSpinner(30 * k, theme::rgb(m_scheme.content2()));
            spinner->setPosition({W / 2, -(y + h / 2)});
            m_footer->addChild(spinner);
            m_spinners.push_back(spinner);
            y += h;
            break;
        }
        case State::Failed: {
            // GD's request failed: no internet, its server busy or rate limiting.
            float h = 90 * k;
            auto message = m_cards.empty()
                ? "Couldn't load the levels. Check your connection, or try again in a moment."
                : "Couldn't load more levels. Try again in a moment.";
            auto label = note(message, 14 * k, theme::rgb(m_scheme.content2()), 40 * k);
            fit(label, W - 2 * m_pad);
            float bw = 120 * k, bh = 24 * k;
            auto& retry = addPill(m_footerPills, m_footer, {bw, bh}, bh / 2, {W / 2, -(y + 40 * k + bh / 2)}, {0.5f, 0.5f},
                                  m_scheme.background2(), m_scheme.background1(), [this] {
                if (m_current) this->request(m_current, false);
            });
            auto retryLabel = makeText("TRY AGAIN", Weight::Bold, 12 * k);
            retryLabel->setPosition({bw / 2, bh / 2});
            retry.node->addChild(retryLabel, 1);
            retry.tinted = {retryLabel};
            retry.textColor = theme::rgb(m_scheme.foreground1());
            retry.textHover = theme::rgb(m_scheme.light1());
            y += h;
            break;
        }
        case State::Loaded: {
            if (m_cards.empty()) {
                // NotFoundDrawable.
                float h = NOT_FOUND_HEIGHT * k;
                auto glyph = makeIcon(icon::SEARCH, 40 * k);
                glyph->setColor(theme::rgb(m_scheme.background1()));
                auto label = makeText("... nope, nothing found.", Weight::Regular, 16 * k);
                label->setColor(theme::rgb(m_scheme.content2()));
                float gw = glyph->getScaledContentSize().width, lw = label->getScaledContentSize().width;
                float x = (W - gw - 10 * k - lw) / 2;
                glyph->setAnchorPoint({0, 0.5f});
                glyph->setPosition({x, -(y + h / 2)});
                label->setAnchorPoint({0, 0.5f});
                label->setPosition({x + gw + 10 * k, -(y + h / 2)});
                m_footer->addChild(glyph);
                m_footer->addChild(label);
                y += h;
            } else if (!m_more) {
                note("end of results", 12 * k, theme::rgb(m_scheme.foreground1()), 30 * k);
                y += 30 * k;
            }
            break;
        }
    }

    m_scroll->setContentHeight(y + CARDS_BOTTOM * k);
    // GD's hidden list registered for the wheel when it was built.
    m_scroll->claimWheel();
}

// --- filter rows ---

bool LevelListingOverlay::rowUsable(Row row) const {
    // The demon kind only matters with demons chosen (GD's search screen does the same).
    if (row == Row::Demon) return (m_difficulty >> DIFF_DEMON) & 1;
    return true;
}

bool LevelListingOverlay::optionActive(Row row, int option) const {
    switch (row) {
        case Row::Difficulty: return (m_difficulty >> option) & 1;
        case Row::Demon: return m_demon == option;
        case Row::Length: return (m_length >> option) & 1;
        case Row::General: return (m_general >> option) & 1;
        case Row::Played: return m_played == option;
        case Row::Type: return (option == 1) == m_lists;
        default: return false;
    }
}

void LevelListingOverlay::toggleOption(Row row, int option) {
    uint32_t bit = 1u << option;
    switch (row) {
        case Row::Difficulty:
            m_difficulty ^= bit;
            // A demon kind goes with demons alone.
            if (option != DIFF_DEMON || !(m_difficulty & bit)) m_demon = 0;
            break;
        case Row::Demon:
            if (m_demon == option) return;
            m_demon = option;
            if (option > 0) m_difficulty = 1u << DIFF_DEMON;
            break;
        case Row::Length:
            m_length ^= bit;
            break;
        case Row::General:
            m_general ^= bit;
            // Rated and unrated rule each other out, so do the two "played" states.
            if (option == RATED && (m_general & bit)) m_general &= ~(1u << UNRATED);
            if (option == UNRATED && (m_general & bit)) m_general &= ~(1u << RATED);
            break;
        case Row::Played:
            if (m_played == option) return;
            m_played = option;
            break;
        case Row::Type:
            if (m_lists == (option == 1)) return;
            m_lists = option == 1;
            break;
        default:
            return;
    }
    queueSearch(FILTER_DEBOUNCE);
}

void LevelListingOverlay::layoutRow(Row row) {
    float k = m_k;
    size_t r = static_cast<size_t>(row);
    float x = m_rowX[r], top = m_rowY[r], width = m_rowW[r], lineH = ROW_LINE * k;
    float lineX = 0;
    int line = 0;
    for (auto index : m_rowPills[r]) {
        auto& p = m_fixedPills[index];
        bool multi = p.activeBg != nullptr;
        bool active = p.active && p.active();
        float textW = std::max(p.label->getScaledContentSize().width, p.boldLabel->getScaledContentSize().width);
        float w = multi && active ? textW + (TAB_PILL_LEFT + TAB_PILL_RIGHT) * k : textW;
        if (lineX > 0 && lineX + w > width) {
            lineX = 0;
            line++;
        }
        p.node->setContentSize({w, lineH});
        p.node->setPosition({x + lineX, -(top + line * lineH + lineH / 2)});
        float labelX = multi && active ? TAB_PILL_LEFT * k : 0;
        p.label->setPosition({labelX, lineH / 2});
        p.boldLabel->setPosition({labelX, lineH / 2});
        if (multi) {
            auto size = p.activeBg->getContentSize();
            p.activeBg->setContentSize({w, size.height});
            p.activeBg->setPosition({w / 2, lineH / 2});
            p.activeIcon->setPosition({8 * k, lineH / 2});
        }
        lineX += w + TAB_SPACING * k;
    }
}

// --- per frame and input ---

void LevelListingOverlay::requestThumbnail(Card& card) {
    card.thumbRequested = true;
    if (card.thumbLevel <= 0) return;
    Ref<RoundedBox> thumb = card.thumb;
    std::weak_ptr<char> alive = m_alive;
    float top = card.top, h = m_cardH;
    thumbnails::fetch(card.thumbLevel, [thumb](CCTexture2D* texture) {
        if (!texture || !thumb->getParent()) return;
        thumb->setTexture(texture);
        thumb->setFillColor(WHITE);
    }, [alive, thumb, this, top, h] {
        // Its card scrolled away (or the page is gone) before its turn came: skip it.
        if (alive.expired() || !thumb->getParent()) return false;
        return this->nearView(top, h);
    });
}

bool LevelListingOverlay::nearView(float top, float height) const {
    // Within a screen of the view, in either direction.
    float scroll = m_scroll->scroll(), viewH = m_scroll->getContentSize().height;
    float cardTop = m_cardsTop + top;
    return cardTop + height > scroll - viewH && cardTop < scroll + viewH * 2;
}

bool LevelListingOverlay::updatePill(Pill& p, bool hovered, float dt) {
    if (hovered && !p.hovered) {
        if (p.kind == Pill::Kind::Card) sfx::hover(sfx::sound::BUTTON_HOVER);
        else if (p.kind == Pill::Kind::Button) sfx::hover(sfx::sound::DEFAULT_HOVER);
    }
    p.hovered = hovered;
    bool active = p.active && p.active();
    bool usable = !p.usable || p.usable();
    switch (p.kind) {
        case Pill::Kind::Card: {
            // BeatmapCardContentBackground: Background2, Background4 while hovered.
            float target = hovered ? 1.f : 0.f;
            if (p.hover.target() != target) p.hover.to(target, CARD_TRANSITION, Easing::OutQuint);
            p.hover.update(dt);
            p.bg->setFillColor(theme::lerp(p.color, p.hoverColor, p.hover.get()));
            return false;
        }
        case Pill::Kind::SortTab: {
            // TabButton.UpdateState: the background shows while active or hovered,
            // the active one reads bold and in Light1.
            p.bg->setFillColor(active || hovered ? p.hoverColor : p.color);
            auto colour = active && !hovered ? theme::rgb(m_scheme.light1()) : ccColor3B {255, 255, 255};
            p.label->setVisible(!active);
            p.boldLabel->setVisible(active);
            p.label->setColor(colour);
            p.boldLabel->setColor(colour);
            return false;
        }
        case Pill::Kind::FilterTab: {
            // FilterTabItem.UpdateState, and MultipleSelectionFilterTabItem's white pill.
            bool multi = p.activeBg != nullptr;
            ccColor4B colour = active ? (multi ? m_scheme.light1() : m_scheme.content1()) : m_scheme.light2();
            if (!usable) colour = theme::lerp(colour, BLACK, 0.5f);
            else if (hovered) colour = active && multi ? theme::lerp(colour, BLACK, 0.2f) : theme::lerp(colour, WHITE, 0.2f);
            p.label->setVisible(!active);
            p.boldLabel->setVisible(active);
            p.label->setColor(theme::rgb(colour));
            if (multi) {
                p.activeBg->setVisible(active);
                p.activeIcon->setVisible(active);
                p.activeBg->setFillColor(colour);
                p.boldLabel->setColor(theme::rgb(m_scheme.background4()));
            } else {
                p.boldLabel->setColor(theme::rgb(colour));
            }
            bool changed = active != p.wasActive;
            p.wasActive = active;
            return changed;
        }
        case Pill::Kind::Button: {
            if (p.bg) {
                auto colour = hovered ? p.hoverColor : p.color;
                if (!p.enabled) colour = theme::lerp(p.color, {40, 40, 40, 255}, 0.6f);
                p.bg->setFillColor(colour);
            }
            for (auto label : p.tinted) label->setColor(hovered ? p.textHover : p.textColor);
            return false;
        }
    }
    return false;
}

void LevelListingOverlay::onUpdate(float dt) {
    if (m_leaving) return;
    // The close button: leave right away (the waves keep dropping during the fade).
    if (!isOpen()) return goBack();
    float ms = dt * 1000.f;

    if (m_searchDelay >= 0) {
        m_searchDelay -= ms;
        if (m_searchDelay < 0) startSearch();
    }
    if (m_state == State::Loading) {
        m_loadingMs += ms;
        if (m_loadingMs > LOAD_TIMEOUT_MS) {
            m_key.clear();
            m_state = State::Failed;
            m_dirty = true;
        }
    }
    if (m_dirty) rebuildFooter();
    for (auto spinner : m_spinners) spinner->setRotation(spinner->getRotation() + dt * SPIN_SPEED);

    for (auto& card : m_cards) {
        // New cards fade in (BeatmapListingOverlay fades loaded cards in).
        card.appear.update(dt);
        auto alpha = static_cast<GLubyte>(255 * std::clamp(card.appear.get(), 0.f, 1.f));
        if (card.root->getOpacity() != alpha) card.root->setOpacity(alpha);
        // Thumbnails for the cards near the view.
        if (!card.thumbRequested && nearView(card.top, m_cardH)) requestThumbnail(card);
    }

    // The next page as the end of this one comes into view (osu! pages on scroll).
    if (m_state == State::Loaded && m_more) {
        float viewH = m_scroll->getContentSize().height;
        if (m_scroll->contentHeight() - (m_scroll->scroll() + viewH) < viewH * 0.5f) loadMore();
    }

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !popupOnTop() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    bool rowsChanged = false;
    for (auto list : {&m_fixedPills, &m_sortPills, &m_cardPills, &m_footerPills}) {
        for (auto& p : *list) {
            bool usable = p.enabled && (!p.usable || p.usable());
            bool hovered = interactive && p.action && usable && nodeShown(p.node) && nodeContains(p.node, mouse);
            if (updatePill(p, hovered, dt)) rowsChanged = true;
        }
    }
    // Chosen multi-select tabs take more room: their row flows again.
    if (rowsChanged) {
        for (int r = 0; r < static_cast<int>(Row::Count); r++) layoutRow(static_cast<Row>(r));
    }
}

LevelListingOverlay::Pill* LevelListingOverlay::pillAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    Pill* hit = nullptr;
    for (auto list : {&m_fixedPills, &m_sortPills, &m_cardPills, &m_footerPills}) {
        for (auto& p : *list) {
            bool usable = p.enabled && (!p.usable || p.usable());
            if (p.action && usable && nodeShown(p.node) && nodeContains(p.node, world)) hit = &p;
        }
    }
    return hit;
}

bool LevelListingOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (m_leaving) return false;
    auto loc = touch->getLocation();
    // The search box takes its own touches (its input node registers below us).
    if (isOpen() && m_input && nodeShown(m_input) && m_scroll->containsWorldPoint(loc) && nodeContains(m_input, loc)) {
        return false;
    }
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    // A tap anywhere else leaves the box (it only ever sees its own touches).
    if (m_input) m_input->defocus();
    m_pressed = pillAt(loc);
    m_drag.began(m_scroll, loc);
    return true;
}

void LevelListingOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void LevelListingOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || !pressed->action || !nodeContains(pressed->node, touch->getLocation())) return;
    if (!pressed->enabled || (pressed->usable && !pressed->usable())) return;
    // HoverSampleSet.Button for cards; the tabs and buttons use the default.
    sfx::click(pressed->kind == Pill::Kind::Card ? sfx::sound::BUTTON_SELECT : sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the list, and the pill with it
    action();
}

void LevelListingOverlay::textChanged(CCTextInputNode*) {
    if (!m_input) return;
    auto query = trim(std::string(m_input->getString()));
    if (query == m_query) return;
    m_query = query;
    // Typing waits a little before searching (BeatmapListingFilterControl's debounce).
    queueSearch(QUERY_DEBOUNCE);
}

void LevelListingOverlay::enterPressed(CCTextInputNode*) {
    if (m_input) m_query = trim(std::string(m_input->getString()));
    startSearch();
}

} // namespace lazer

// GD's LevelBrowserLayer shows every online list; for those it stays hidden
// under our page and keeps doing the work: fetching pages as the browser's
// LevelManagerDelegate, handing them to the page. Nothing is removed, so
// other mods' hooks on it keep working.
class $modify(LazerLevelListing, LevelBrowserLayer) {
    struct Fields {
        lazer::LevelListingOverlay* page = nullptr;
        CCNode* backdrop = nullptr;
        // GD answered from its cache while the layer was still being built.
        Ref<CCArray> pendingLevels;
        std::string pendingKey;
        bool pendingLoaded = false;
        bool pendingFailed = false;
        std::string pendingInfo, pendingInfoKey;
    };

    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search)) return false;
        if (!lazer::LevelListingOverlay::wants(search)) return true;
        auto page = lazer::LevelListingOverlay::create(this, search);
        if (!page) return true;
        auto& f = m_fields;

        // A dark stage for the waves to rise over (FullscreenOverlay's Background6).
        auto backdrop = CCLayerColor::create(lazer::theme::Scheme {200}.background6());
        backdrop->setContentSize(CCDirector::get()->getWinSize());
        this->addChild(backdrop, 99);
        page->setID("level-listing"_spr);
        this->addChild(page, 100);
        f->page = page;
        f->backdrop = backdrop;
        this->hideVanilla();
        page->open();

        if (!f->pendingInfoKey.empty()) page->pageInfo(f->pendingInfo, f->pendingInfoKey.c_str());
        if (f->pendingLoaded) page->levelsLoaded(f->pendingLevels.data(), f->pendingKey.c_str());
        else if (f->pendingFailed) page->levelsFailed(f->pendingKey.c_str());
        f->pendingLevels = nullptr;
        return true;
    }

    // Hide (not remove) GD's nodes so other mods hooking them keep working.
    // Its list gets rebuilt with every page: hidden again each time.
    void hideVanilla() {
        auto& f = m_fields;
        for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (child == f->page || child == f->backdrop) continue;
            child->setVisible(false);
        }
        // The hidden list would still scroll under a finger, and the loading
        // circle swallows every touch it gets.
        if (m_list && m_list->m_listView && m_list->m_listView->m_tableView) {
            m_list->m_listView->m_tableView->setTouchEnabled(false);
        }
        if (m_circle) m_circle->setTouchEnabled(false);
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int type) {
        LevelBrowserLayer::loadLevelsFinished(levels, key, type);
        auto& f = m_fields;
        if (f->page) {
            this->hideVanilla();
            f->page->levelsLoaded(levels, key);
        } else if (key) {
            f->pendingLevels = levels;
            f->pendingKey = key;
            f->pendingLoaded = true;
            f->pendingFailed = false;
        }
    }

    void loadLevelsFailed(char const* key, int type) {
        LevelBrowserLayer::loadLevelsFailed(key, type);
        auto& f = m_fields;
        if (f->page) {
            this->hideVanilla();
            f->page->levelsFailed(key);
        } else if (key) {
            f->pendingKey = key;
            f->pendingFailed = true;
            f->pendingLoaded = false;
        }
    }

    void setupPageInfo(gd::string info, char const* key) {
        LevelBrowserLayer::setupPageInfo(info, key);
        auto& f = m_fields;
        if (f->page) f->page->pageInfo(std::string(info), key);
        else if (key) {
            f->pendingInfo = std::string(info);
            f->pendingInfoKey = key;
        }
    }

    // Back goes where the player came from (the menu or song select), not to
    // GD's search screen.
    void onBack(CCObject* sender) {
        if (m_fields->page) return m_fields->page->goBack();
        LevelBrowserLayer::onBack(sender);
    }

#ifndef GEODE_IS_ANDROID
    // On Android, keyBackClicked is just onBack(nullptr), hooked above: too
    // small to hook (the hook's patch spills into the next function).
    void keyBackClicked() {
        if (m_fields->page) return m_fields->page->goBack();
        LevelBrowserLayer::keyBackClicked();
    }

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        if (auto page = m_fields->page) {
            // GD's arrow keys page through its hidden list: nothing else applies here.
            if (key == KEY_Escape) page->goBack();
            return;
        }
        LevelBrowserLayer::keyDown(key, timestamp);
    }
#endif
};
