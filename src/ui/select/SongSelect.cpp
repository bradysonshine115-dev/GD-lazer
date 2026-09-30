#include "SongSelectInternal.hpp"

#include "../../audio/MusicPlayer.hpp"
#include "../../audio/Sfx.hpp"
#include "../core/MenuCursor.hpp"
#include "../core/Quips.hpp"
#include "../core/Theme.hpp"
#include "../menu/MenuBackground.hpp"
#include "../overlays/Dialog.hpp"
#include "../overlays/LevelListingOverlay.hpp"

#include <algorithm>
#include <random>
#include <unordered_map>

using namespace geode::prelude;

namespace lazer {

namespace songselect {
    levels::Kind g_lastKind = levels::Kind::Classic;
    Remembered& remembered() {
        static Remembered r[3];
        return r[static_cast<int>(g_lastKind)];
    }

    Resume g_resume;
}

bool& SongSelect::returnsHere() {
    static bool value = false;
    return value;
}

bool& SongSelect::browsingOnline() {
    static bool value = false;
    return value;
}

CCScene* SongSelect::scene(levels::Kind kind) {
    auto scene = CCScene::create();
    scene->addChild(SongSelect::create(kind, true));
    return scene;
}

CCScene* SongSelect::scene() {
    auto scene = CCScene::create();
    scene->addChild(SongSelect::create(g_lastKind));
    return scene;
}

SongSelect* SongSelect::create(levels::Kind kind, bool fromMenu) {
    auto ret = new SongSelect();
    if (ret->init(kind, fromMenu)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SongSelect::init(levels::Kind kind, bool fromMenu) {
    if (!CCLayer::init()) return false;
    m_kind = kind;
    g_lastKind = kind;
    browsingOnline() = false;
    m_win = CCDirector::get()->getWinSize();
    m_k = unitScale();
    float k = m_k;
    this->setID("song-select"_spr);

    m_footerH = FOOTER_HEIGHT * k;
    float bonus = std::max(0.f, m_win.width / m_win.height - 2.f);
    m_rightW = std::clamp(m_win.width * 0.5f, 500 * k, (700 + bonus * 300) * k);
    m_rightW = std::min(m_rightW, m_win.width * 0.6f);
    m_leftW = std::min(m_win.width * 0.5f, (700 + bonus * 100) * k);
    m_panelH = PANEL_HEIGHT * k;
    m_spacing = PANEL_SPACING * k;
    m_carouselTop = m_win.height - FILTER_HEIGHT * k - 5 * k;
    m_carouselBottom = m_footerH + 5 * k;

    // Background: the selected level's thumbnail, blurred and dimmed.
    auto source = CCLayerGradient::create({34, 26, 50, 255}, {8, 8, 12, 255});
    source->setVisible(false);
    this->addChild(source, -10);
    m_background = MenuBackground::create(source, BACKGROUND_DIM, true, true);
    this->addChild(m_background, -5);

    // osu!'s side shading: darker behind the wedges and behind the carousel.
    auto leftShade = CCLayerGradient::create({0, 0, 0, 77}, {0, 0, 0, 0}, {1, 0});
    leftShade->setContentSize({m_win.width * 0.6f, m_win.height});
    this->addChild(leftShade, -4);
    auto rightShade = CCLayerGradient::create({0, 0, 0, 0}, {0, 0, 0, 128}, {1, 0});
    rightShade->setContentSize({m_rightW, m_win.height});
    rightShade->setPosition({m_win.width - m_rightW, 0});
    this->addChild(rightShade, -4);

    m_carousel = CCNode::create();
    this->addChild(m_carousel, 1);
    m_bar = RoundedBox::create({SCROLLBAR_WIDTH * k, SCROLLBAR_WIDTH * k}, SCROLLBAR_WIDTH / 2 * k, {136, 136, 136, 255});
    m_bar->setAnchorPoint({1, 0.5f});
    m_bar->setVisible(false);
    this->addChild(m_bar, 1);
    // The label beside the held bar.
    m_barLabel = CCNode::create();
    m_barLabelBg = RoundedBox::create({10, 10}, 8 * k, {24, 23, 28, 235});
    m_barLabelBg->setShadow(8 * k, {0, 0, 0, 90});
    m_barLabelBg->setAnchorPoint({1, 0.5f});
    m_barLabel->addChild(m_barLabelBg);
    m_barLabelText = makeText("", Weight::SemiBold, 22 * k);
    m_barLabelText->setAnchorPoint({1, 0.5f});
    m_barLabel->addChild(m_barLabelText);
    m_barLabel->setVisible(false);
    this->addChild(m_barLabel, 6);
    m_wedge = CCNode::create();
    this->addChild(m_wedge, 2);

    buildFilter();
    buildFooter();

    auto& r = remembered();
    m_group = static_cast<Group>(std::clamp(r.group, 0, 2));
    m_folder = r.folder;
    m_sort = r.sort;
    m_query = r.query;
    if (m_search && !m_query.empty()) m_search->setString(m_query);
    if (packMode()) {
        // The packs (and any pack's levels) come from the session's store,
        // told here whenever more arrive; GD's cache may fill it in at once.
        packs::setListener([this] { this->onPacksChanged(); });
        for (auto& p : packs::all()) packs::refresh(p);
        packs::load();
        if (packs::state() != packs::State::Loading) rebuildPackEntries();
        log::info("Song select: {} map packs", packs::all().size());
        // Many players dread this page. So does the cursor.
        if (fromMenu) {
            static std::mt19937 rng {std::random_device {}()};
            cursorSay(MAP_PACKS_CURSOR[std::uniform_int_distribution<size_t>(0, MAP_PACKS_CURSOR.size() - 1)(rng)]);
        }
    } else {
        m_entries = levels::all(m_kind);
        log::info("Song select: {} {} levels", m_entries.size(), m_kind == levels::Kind::Platformer ? "platformer" : "classic");
    }
    // Coming from the menu, its song carries on and picks the selection (osu!
    // selects the playing beatmap). Back from a level, the last selection stays.
    auto track = MusicPlayer::get().current();
    int playingID = track ? track->songID : 0;
    std::string playing = fromMenu ? MusicPlayer::get().handOff() : "";
    if (packMode() || playing.empty() || !selectSong(playing, playingID)) applyFilter();
    if (packMode()) restoreExpandedPack();
    m_scroll = m_scrollTarget;

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    this->setKeyboardEnabled(true);
    this->scheduleUpdate();
    return true;
}

void SongSelect::onEnter() {
    CCLayer::onEnter();
    CCDirector::get()->getMouseDispatcher()->addDelegate(this);
}

void SongSelect::onExit() {
    CCDirector::get()->getMouseDispatcher()->removeDelegate(this);
    if (packMode()) packs::setListener(nullptr);
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    stopListening();
    // Leaving with a level built but not entered (the push hands it over first).
    dropLevel();
    CCLayer::onExit();
}

void SongSelect::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, 0, true);
}

// --- top right: search, groups, sort ---

void SongSelect::buildFilter() {
    float k = m_k;
    float left = m_win.width - m_rightW;
    auto box = RoundedBox::create({m_rightW + 60 * k, FILTER_HEIGHT * k + 20 * k}, CORNER * k, {0, 0, 0, 170});
    box->setAnchorPoint({0, 0});
    box->setPosition({left, m_win.height - FILTER_HEIGHT * k});
    box->setSkewX(skewDegrees());
    this->addChild(box, 3);

    // Search field.
    float searchY = m_win.height - 30 * k;
    float searchW = m_rightW - 50 * k;
    auto field = RoundedBox::create({searchW, 36 * k}, 8 * k, {255, 255, 255, 28});
    field->setAnchorPoint({0, 0.5f});
    field->setPosition({left + 30 * k, searchY});
    this->addChild(field, 4);
    auto icon = makeIcon(icon::SEARCH, 16 * k);
    icon->setColor(theme::LIGHT1);
    icon->setPosition({left + 30 * k + 20 * k, searchY});
    this->addChild(icon, 5);

    float inputScale = 0.8f;
    m_search = TextInput::create((searchW - 150 * k) / inputScale, "type to search", "outfit-regular.fnt"_spr);
    m_search->hideBG();
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(inputScale);
    m_search->setAnchorPoint({0, 0.5f});
    m_search->setPosition({left + 30 * k + 38 * k, searchY});
    m_search->setCallback([this](std::string const& text) {
        m_query = text;
        remembered().query = text;
        applyFilter();
        m_noResultsMs = 900; // once the typing stops
    });
    this->addChild(m_search, 5);

    m_countLabel = makeText("", Weight::Regular, 15 * k);
    m_countLabel->setColor(theme::LIGHT1);
    m_countLabel->setAnchorPoint({1, 0.5f});
    m_countLabel->setPosition({left + 30 * k + searchW - 14 * k, searchY});
    this->addChild(m_countLabel, 5);

    // Groups (osu!'s collection / grouping), GD's folders and sort.
    float rowY = m_win.height - 72 * k;
    float x = left + 22 * k;
    // Packs: every pack, the ones still to finish, the finished ones.
    bool packs = packMode();
    char const* names[] = {packs ? "all" : "saved", packs ? "unfinished" : "official", packs ? "completed" : "liked"};
    char const* glyphs[] = {nullptr, nullptr, packs ? icon::CHECK : icon::HEART};
    for (int i = 0; i < 3; i++) {
        auto& tab = addButton(m_tabs, this, glyphs[i], names[i], {x, rowY}, 28 * k, TAB, [this, i] {
            m_group = static_cast<Group>(i);
            remembered().group = i;
            closeFolders();
            applyFilter();
        }, 0);
        x += tab.node->getContentSize().width + 6 * k;
    }

    auto& folders = addButton(m_buttons, this, icon::FOLDER, "all folders", {x + 4 * k, rowY}, 28 * k, TAB,
                              [this] { this->toggleFolders(); }, 0);
    m_folderButton = m_buttons.size() - 1;
    for (auto child : CCArrayExt<CCNode*>(folders.node->getChildren())) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(child); label && label->getTag() == 1) m_folderLabel = label;
    }
    x += 4 * k + folders.node->getContentSize().width + 6 * k;

    auto& sort = addButton(m_buttons, this, icon::SLIDERS, "sort: difficulty", {x, rowY}, 28 * k, TAB, [this] {
        m_sort = static_cast<levels::Sort>((static_cast<int>(m_sort) + 1) % 4);
        remembered().sort = m_sort;
        applyFilter();
    }, 0);
    for (auto child : CCArrayExt<CCNode*>(sort.node->getChildren())) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(child); label && label->getTag() == 1) m_sortLabel = label;
    }
}

void SongSelect::toggleFolders() {
    if (m_folderMenu) return closeFolders();
    float k = m_k;
    std::vector<int> ids;
    for (auto const& e : m_entries) {
        if (!e.official && e.folder > 0 && std::find(ids.begin(), ids.end(), e.folder) == ids.end()) ids.push_back(e.folder);
    }
    std::sort(ids.begin(), ids.end());

    auto anchor = m_buttons[m_folderButton].node;
    float itemH = 28 * k, gap = 4 * k, pad = 6 * k;
    float width = std::max(anchor->getContentSize().width, 170 * k);
    size_t count = ids.size() + (ids.empty() ? 2 : 1);
    float height = count * itemH + (count - 1) * gap + pad * 2;

    m_folderMenu = CCNode::create();
    m_folderMenu->setPosition({anchor->getPositionX(), anchor->getPositionY() - itemH / 2 - 6 * k - height});
    this->addChild(m_folderMenu, 30);
    auto bg = RoundedBox::create({width + pad * 2, height}, 8 * k, {20, 18, 28, 245});
    bg->setShadow(12 * k, {0, 0, 0, 120});
    bg->setAnchorPoint({0, 0});
    bg->setPosition({-pad, 0});
    m_folderMenu->addChild(bg);

    float y = height - pad - itemH / 2;
    auto item = [&](std::string const& name, int folder) {
        std::function<void()> action;
        if (folder >= 0) {
            action = [this, folder] {
                m_folder = folder;
                remembered().folder = folder;
                closeFolders();
                applyFilter();
            };
        }
        auto& b = addButton(m_folderItems, m_folderMenu, folder > 0 ? icon::FOLDER : nullptr, name, {0, y}, itemH, TAB,
                            std::move(action), 0);
        b.selected = folder == m_folder;
        // Full-width rows.
        b.node->setContentSize({width, itemH});
        b.bg->setContentSize({width, itemH});
        y -= itemH + gap;
    };
    item("all folders", 0);
    for (int id : ids) item(levels::folderName(id), id);
    if (ids.empty()) item("no folders yet", -1);
}

void SongSelect::closeFolders() {
    if (!m_folderMenu) return;
    if (m_pressed >= m_folderItems.data() && m_pressed < m_folderItems.data() + m_folderItems.size()) m_pressed = nullptr;
    m_folderItems.clear();
    m_folderMenu->removeFromParent();
    m_folderMenu = nullptr;
}

void SongSelect::confirmDeleteUnhearted() {
    int count = levels::countUnhearted();
    if (count == 0) {
        Dialog::show(icon::CIRCLE_INFO, "Nothing to delete", "Every saved level is hearted or in a folder.",
                     {{"OK", Dialog::Kind::Cancel, nullptr}});
        return;
    }
    Ref<SongSelect> self = this;
    Dialog::show(icon::TRASH, "Delete unhearted levels?",
        fmt::format("{} saved level{} that aren't hearted or in a folder. This can't be undone.",
                    count, count == 1 ? "" : "s"), {
        {"Yes. Go for it.", Dialog::Kind::Dangerous, [self] {
            levels::deleteUnhearted();
            quips::say("delete-unhearted");
            self->reloadEntries();
        }},
        {"No! Abort mission", Dialog::Kind::Cancel, nullptr},
    });
}

// The selected saved level, after asking (osu!'s BeatmapDeleteDialog). The
// selection moves on to the next level (the one before, at the end of the list).
void SongSelect::confirmDeleteLevel() {
    if (!m_hasSelection || m_selected >= m_visible.size()) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.official || e.pack >= 0 || !e.level) return;
    Ref<GJGameLevel> level = e.level;
    Ref<SongSelect> self = this;
    Dialog::show(icon::TRASH, "Confirm deletion of", fmt::format("{} by {}", e.name, e.creator), {
        {"Yes. Go for it.", Dialog::Kind::Dangerous, [self, level] {
            auto& visible = self->m_visible;
            auto& entries = self->m_entries;
            auto it = std::find_if(visible.begin(), visible.end(), [&](size_t i) { return entries[i].level == level; });
            if (it == visible.end()) return;
            size_t at = it - visible.begin();
            auto deleted = entries[*it];
            if (visible.size() > 1) {
                // applyFilter keeps the selected level: make that the neighbour.
                self->m_selected = at + 1 < visible.size() ? at + 1 : at - 1;
                self->m_hasSelection = true;
            }
            levels::deleteLevel(deleted);
            self->reloadEntries();
        }},
        {"No! Abort mission", Dialog::Kind::Cancel, nullptr},
    });
}

// --- bottom: footer ---

void SongSelect::buildFooter() {
    float k = m_k;
    auto bar = CCLayerColor::create({0, 0, 0, 150}, m_win.width, m_footerH);
    this->addChild(bar, 4);

    float h = m_footerH - 10 * k;
    float y = m_footerH / 2;
    auto& back = addButton(m_buttons, this, icon::CHEVRON_LEFT, "back", {-12 * k, y}, h, PINK, [this] { this->back(); }, skewDegrees());
    float x = back.node->getPositionX() + back.node->getContentSize().width + 14 * k;
    auto& random = addButton(m_buttons, this, icon::SHUFFLE, "random", {x, y}, h, TAB, [this] { this->selectRandom(); }, skewDegrees());
    x += random.node->getContentSize().width + 10 * k;
    if (!packMode()) {
        // Online levels: GD's search, with what's typed here already in it.
        auto& browse = addButton(m_buttons, this, icon::GLOBE, "browse", {x, y}, h, TAB, [this] { this->browseOnline(); }, skewDegrees());
        x += browse.node->getContentSize().width + 10 * k;
    }
    auto& page = addButton(m_buttons, this, icon::CIRCLE_INFO, "level page", {x, y}, h, TAB, [this] { this->openLevelPage(); }, skewDegrees());
    x += page.node->getContentSize().width + 10 * k;
    // Packs' levels aren't yours to delete from here.
    if (!packMode()) {
        addButton(m_buttons, this, icon::TRASH, "delete unhearted", {x, y}, h, TAB, [this] { this->confirmDeleteUnhearted(); }, skewDegrees());
    }

    auto& play = addButton(m_buttons, this, icon::PLAY, "play", {0, y}, h, PURPLE, [this] { this->start(); }, skewDegrees());
    play.node->setPositionX(m_win.width - play.node->getContentSize().width + 12 * k);
}

SongSelect::Button& SongSelect::addButton(std::vector<Button>& list, CCNode* parent, char const* glyph,
                                          std::string const& label, CCPoint pos, float height, ccColor4B color,
                                          std::function<void()> action, float skew) {
    float k = m_k;
    float pad = height * 0.55f;
    auto node = CCNode::create();
    node->setAnchorPoint({0, 0.5f});

    CCLabelBMFont* iconLabel = nullptr;
    float contentW = 0;
    if (glyph) {
        iconLabel = makeIcon(glyph, height * 0.42f);
        contentW += iconLabel->getScaledContentSize().width + 8 * k;
    }
    auto text = makeText(label, Weight::SemiBold, height * 0.5f);
    text->setTag(1);
    contentW += text->getScaledContentSize().width;
    float w = std::max(contentW + pad * 2, height * 2.2f);
    node->setContentSize({w, height});

    auto bg = RoundedBox::create({w, height}, std::min(8 * k, height / 2), color);
    bg->setAnchorPoint({0, 0});
    if (skew != 0) bg->setSkewX(skew);
    node->addChild(bg);

    float x = (w - contentW) / 2;
    if (iconLabel) {
        iconLabel->setAnchorPoint({0, 0.5f});
        iconLabel->setPosition({x, height / 2});
        node->addChild(iconLabel, 1);
        x += iconLabel->getScaledContentSize().width + 8 * k;
    }
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({x, height / 2});
    node->addChild(text, 1);

    node->setPosition(pos);
    parent->addChild(node, 5);
    list.push_back({node, bg, color, std::move(action)});
    return list.back();
}

// --- data ---

void SongSelect::applyFilter() {
    int keepId = -1;
    bool keepOfficial = false, keepHeader = false;
    if (m_hasSelection && m_selected < m_visible.size() && m_visible[m_selected] < m_entries.size()) {
        auto const& e = m_entries[m_visible[m_selected]];
        keepId = e.id;
        keepOfficial = e.official;
        keepHeader = e.packHeader;
    } else {
        keepId = remembered().selectedId;
        keepOfficial = remembered().selectedOfficial;
        keepHeader = remembered().selectedHeader;
    }

    std::string query = lower(m_query);
    m_visible.clear();
    for (size_t i = 0; i < m_entries.size(); i++) {
        auto const& e = m_entries[i];
        if (packMode()) {
            // The packs are the rows; the open one's levels (and, searching,
            // the matching ones) go under them below.
            if (!e.packHeader) continue;
            auto pack = packOf(e);
            bool done = pack && !pack->levelIDs.empty() && pack->completed >= static_cast<int>(pack->levelIDs.size());
            if (m_group == Group::Official && done) continue; // unfinished
            if (m_group == Group::Liked && !done) continue;   // completed
            if (!query.empty() && e.search.find(query) == std::string::npos) {
                // By a level's name or creator, once the levels are known.
                bool byLevel = pack && std::any_of(pack->levels.begin(), pack->levels.end(), [&](auto const& l) {
                    return l.search.find(query) != std::string::npos;
                });
                if (!byLevel) continue;
            }
            m_visible.push_back(i);
            continue;
        }
        if ((m_group == Group::Official) != e.official) continue;
        if (m_group == Group::Liked && !levels::favorited(e)) continue;
        if (m_group != Group::Official && m_folder != 0 && e.folder != m_folder) continue;
        if (!query.empty() && e.search.find(query) == std::string::npos) continue;
        m_visible.push_back(i);
    }

    auto& entries = m_entries;
    switch (m_sort) {
        case levels::Sort::Title:
            std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
                return lower(entries[a].name) < lower(entries[b].name);
            });
            break;
        case levels::Sort::Difficulty:
            std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
                int ra = difficultyRank(entries[a].difficulty), rb = difficultyRank(entries[b].difficulty);
                if (ra != rb) return ra < rb;
                return entries[a].stars < entries[b].stars;
            });
            break;
        case levels::Sort::Progress:
            std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
                return entries[a].normalPercent > entries[b].normalPercent;
            });
            break;
        case levels::Sort::Default:
            break;
    }
    if (packMode()) {
        // Under each header: the open pack's levels, and while searching the
        // levels that match, in the pack's order. Searching needs every
        // pack's levels: fetched now, a few packs at a time.
        if (!query.empty() && packs::levelsProgress() < 1.f) packs::loadAllLevels();
        std::vector<size_t> rows;
        for (size_t v : m_visible) {
            rows.push_back(v);
            auto const& h = m_entries[v];
            for (size_t j = v + 1; j < m_entries.size(); j++) {
                auto const& l = m_entries[j];
                if (l.packHeader || l.pack != h.pack) break;
                bool open = h.pack == m_expandedPack;
                bool hit = !query.empty() && l.search.find(query) != std::string::npos;
                if (open || hit) rows.push_back(j);
            }
        }
        m_visible = std::move(rows);
    }
    layoutRows();

    // Indices changed: panels still in the list move to their new index (no
    // fade in again while typing), the rest go.
    std::unordered_map<size_t, Panel> byEntry;
    for (auto& [index, panel] : m_panels) byEntry.emplace(panel.entry, std::move(panel));
    m_panels.clear();
    for (size_t i = 0; i < m_visible.size() && !byEntry.empty(); i++) {
        auto it = byEntry.find(m_visible[i]);
        if (it == byEntry.end()) continue;
        m_panels.emplace(i, std::move(it->second));
        byEntry.erase(it);
    }
    for (auto& [entry, panel] : byEntry) panel.root->removeFromParent();

    for (size_t i = 0; i < m_tabs.size(); i++) m_tabs[i].selected = static_cast<int>(m_group) == static_cast<int>(i);
    // RobTop's levels aren't in folders (and packs aren't levels).
    auto& folderButton = m_buttons[m_folderButton];
    folderButton.node->setVisible(m_group != Group::Official && !packMode());
    folderButton.selected = m_folder != 0;
    if (m_folderLabel) {
        m_folderLabel->setString(m_folder == 0 ? "all folders" : levels::folderName(m_folder).c_str());
        fitLabel(m_folderLabel, folderButton.node);
    }
    static char const* SORT_NAMES[] = {"sort: default", "sort: title", "sort: difficulty", "sort: progress"};
    if (m_sortLabel) {
        m_sortLabel->setString(SORT_NAMES[static_cast<int>(m_sort)]);
        fitLabel(m_sortLabel, m_sortLabel->getParent());
    }
    if (m_countLabel) {
        if (packMode()) {
            size_t n = std::count_if(m_visible.begin(), m_visible.end(), [&](size_t i) { return m_entries[i].packHeader; });
            std::string text = fmt::format("{} map pack{}", n, n == 1 ? "" : "s");
            if (packs::state() == packs::State::Loading) text += " so far";
            else if (!query.empty() && packs::loadingLevels()) {
                text = fmt::format("searching levels... {}%", static_cast<int>(packs::levelsProgress() * 100));
            }
            m_countLabel->setString(text.c_str());
        } else {
            m_countLabel->setString(fmt::format("{} {} level{}", m_visible.size(),
                m_kind == levels::Kind::Platformer ? "platformer" : "classic", m_visible.size() == 1 ? "" : "s").c_str());
        }
    }

    if (m_visible.empty()) {
        m_hasSelection = false;
        m_lastSelection = {};
        updateWedge();
        return;
    }
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < m_visible.size(); i++) {
        auto const& e = m_entries[m_visible[i]];
        if (e.id == keepId && e.official == keepOfficial && e.packHeader == keepHeader) {
            index = i;
            found = true;
            break;
        }
    }
    // Gone (a pack closed on its level): its header, if it's showing.
    if (!found && packMode() && m_expandedPack >= 0) {
        for (size_t i = 0; i < m_visible.size(); i++) {
            auto const& e = m_entries[m_visible[i]];
            if (e.packHeader && e.pack == m_expandedPack) {
                index = i;
                break;
            }
        }
    }
    m_hasSelection = false; // force the selection to refresh
    select(index);
}

void SongSelect::reloadEntries() {
    // Entry indices change: no panel can be reused.
    for (auto& [index, panel] : m_panels) panel.root->removeFromParent();
    m_panels.clear();
    if (packMode()) {
        for (auto& p : packs::all()) packs::refresh(p);
        rebuildPackEntries();
        m_hasSelection = false; // the old rows are gone: found again by ID
    } else {
        m_entries = levels::all(m_kind);
    }
    applyFilter();
}

// --- map packs ---

void SongSelect::rebuildPackEntries() {
    // Panels keep going across the rebuild: each one's entry is found again
    // (a pack's header by pack, a level by ID), so nothing fades in twice.
    struct Key { bool header; int pack; int id; };
    std::unordered_map<size_t, Key> keys;
    for (auto& [index, panel] : m_panels) {
        if (panel.entry < m_entries.size()) {
            auto const& e = m_entries[panel.entry];
            keys.emplace(index, Key {e.packHeader, e.pack, e.id});
        }
    }
    m_entries.clear();
    auto& packs = packs::all();
    for (size_t i = 0; i < packs.size(); i++) {
        auto const& p = packs[i];
        levels::Entry h;
        h.packHeader = true;
        h.pack = static_cast<int>(i);
        h.id = p.id;
        h.name = p.name;
        h.creator = "RobTop";
        h.difficulty = p.difficulty;
        h.stars = p.stars;
        h.coins = p.coins;
        h.coinsVerified = true;
        int total = static_cast<int>(p.levelIDs.size());
        h.normalPercent = total > 0 ? p.completed * 100 / total : 0;
        h.search = p.search;
        h.resolved = true;
        m_entries.push_back(std::move(h));
        for (auto const& level : p.levels) {
            auto e = level;
            e.pack = static_cast<int>(i);
            m_entries.push_back(std::move(e));
        }
    }
    for (auto it = m_panels.begin(); it != m_panels.end();) {
        auto key = keys.find(it->first);
        size_t found = SIZE_MAX;
        if (key != keys.end()) {
            for (size_t j = 0; j < m_entries.size(); j++) {
                auto const& e = m_entries[j];
                if (e.packHeader == key->second.header && e.pack == key->second.pack && e.id == key->second.id) {
                    found = j;
                    break;
                }
            }
        }
        if (found == SIZE_MAX) {
            it->second.root->removeFromParent();
            it = m_panels.erase(it);
        } else {
            it->second.entry = found;
            ++it;
        }
    }
}

void SongSelect::onPacksChanged() {
    if (!packMode() || m_starting) return;
    // The list comes a page at a time: it shows once it's all here, so the
    // screen doesn't build and fade in again with every page.
    if (packs::state() == packs::State::Loading) {
        if (m_countLabel) {
            size_t n = packs::all().size();
            m_countLabel->setString(fmt::format("{} map pack{} so far", n, n == 1 ? "" : "s").c_str());
        }
        return;
    }
    // The rows are rebuilt: the selection is found again by ID (select()
    // keeps it in remembered()), and its details are redrawn in place.
    rebuildPackEntries();
    m_hasSelection = false;
    applyFilter();
    refreshDetails();
    restoreExpandedPack();
    if (m_expandedPack >= 0 && m_expandedPack < static_cast<int>(packs::all().size())
        && packs::all()[m_expandedPack].state == packs::State::Failed) {
        cursorSay("the servers said no");
    }
}

void SongSelect::restoreExpandedPack() {
    if (packs::state() != packs::State::Loaded) return;
    auto& packs = packs::all();
    if (m_expandedPack >= static_cast<int>(packs.size())) m_expandedPack = -1;
    if (m_expandedPack < 0) {
        // The pack open last time stays open.
        int wanted = remembered().expandedPack;
        for (size_t i = 0; i < packs.size() && wanted >= 0; i++) {
            if (packs[i].id != wanted) continue;
            m_expandedPack = static_cast<int>(i);
            m_hasSelection = false; // its levels go under it: the rows change
            applyFilter();
            break;
        }
        if (m_expandedPack < 0) return;
    }
    auto& p = packs[m_expandedPack];
    if (p.state == packs::State::Loaded) {
        // Its levels are here: on to the first one still to beat.
        if (auto e = selectedEntry(); e && e->packHeader && e->pack == m_expandedPack) selectPackLevel();
    } else if (p.state == packs::State::Unloaded) {
        // Not fetched yet (or its request had to wait for another pack's).
        packs::loadLevels(m_expandedPack);
    }
}

void SongSelect::expandPack(int pack) {
    if (!packMode()) return;
    auto& packs = packs::all();
    if (pack >= static_cast<int>(packs.size())) pack = -1;
    int was = m_expandedPack;
    if (pack == was) return;
    m_expandedPack = pack;
    auto& r = remembered();
    r.expandedPack = pack >= 0 ? packs[pack].id : -1;
    // The selection moves to the pack's header (its levels follow), or to
    // the header of the pack that closed.
    int header = pack >= 0 ? pack : was;
    if (header >= 0) {
        r.selectedId = packs[header].id;
        r.selectedOfficial = false;
        r.selectedHeader = true;
    }
    m_hasSelection = false;
    applyFilter();
    if (pack < 0) return;
    auto& p = packs[pack];
    if (p.state == packs::State::Loaded) selectPackLevel();
    else if (p.state != packs::State::Loading) packs::loadLevels(pack);
}

void SongSelect::selectPackLevel() {
    size_t first = SIZE_MAX;
    for (size_t v = 0; v < m_visible.size(); v++) {
        auto const& e = m_entries[m_visible[v]];
        if (e.packHeader || e.pack != m_expandedPack) continue;
        if (first == SIZE_MAX) first = v;
        if (e.normalPercent < 100) {
            first = v;
            break;
        }
    }
    if (first != SIZE_MAX) select(first);
}

void SongSelect::claimPack(int pack) {
    auto& packs = packs::all();
    if (pack < 0 || pack >= static_cast<int>(packs.size()) || !packs::canClaim(packs[pack])) return;
    auto& p = packs[pack];
    packs::claim(p);
    sfx::play(sfx::sound::DIALOG_OK_SELECT);
    Dialog::show(icon::GIFT, "Map pack complete!",
        fmt::format("{} gave you {} star{} and {} coin{}.", p.name, p.stars, p.stars == 1 ? "" : "s",
                    p.coins, p.coins == 1 ? "" : "s"),
        {{"Nice", Dialog::Kind::Ok, nullptr}});
    cursorSay("free stars! well, earned.");
    // The header's progress and its reward chip change.
    rebuildPackEntries();
    m_hasSelection = false;
    applyFilter();
    refreshDetails();
}

levels::Entry const* SongSelect::selectedEntry() const {
    if (!m_hasSelection || m_selected >= m_visible.size() || m_visible[m_selected] >= m_entries.size()) return nullptr;
    return &m_entries[m_visible[m_selected]];
}

// Rows follow one another with osu!'s spacing, and the open pack stands
// apart with room above it and below its last level (osu! gives its expanded
// set twice the spacing; more here, so the open one is plain to see).
void SongSelect::layoutRows() {
    m_rowTops.assign(m_visible.size() + 1, 0.f);
    float y = 0;
    for (size_t i = 0; i < m_visible.size(); i++) {
        if (i > 0) {
            auto const& above = m_entries[m_visible[i - 1]];
            auto const& row = m_entries[m_visible[i]];
            bool opening = row.packHeader && row.pack == m_expandedPack;
            bool closing = !above.packHeader && above.pack >= 0 && row.packHeader;
            y += (opening || closing) ? PACK_OPEN_GAP * m_k : m_spacing;
        }
        m_rowTops[i] = y;
        y += rowHeight(i);
    }
    m_rowTops[m_visible.size()] = y;
}

float SongSelect::rowHeight(size_t visibleIndex) const {
    if (packMode() && visibleIndex < m_visible.size()) {
        auto const& e = m_entries[m_visible[visibleIndex]];
        if (e.packHeader) return PACK_HEADER_HEIGHT * m_k;
        if (e.pack >= 0) return PACK_LEVEL_HEIGHT * m_k;
    }
    return m_panelH;
}

float SongSelect::itemTop(size_t visibleIndex) const {
    if (visibleIndex < m_rowTops.size()) return m_rowTops[visibleIndex];
    return visibleIndex * (m_panelH + m_spacing);
}

float SongSelect::viewHeight() const {
    return m_carouselTop - m_carouselBottom;
}

void SongSelect::select(size_t visibleIndex, bool scroll) {
    if (m_visible.empty()) return;
    visibleIndex = std::min(visibleIndex, m_visible.size() - 1);
    bool changed = !m_hasSelection || visibleIndex != m_selected;
    m_selected = visibleIndex;
    m_hasSelection = true;

    if (scroll) m_scrollTarget = itemTop(visibleIndex) + rowHeight(visibleIndex) / 2 - viewHeight() / 2;
    if (!changed) return;
    // The rows were rebuilt and it's the same level: nothing to redo (the
    // caller refreshes the details if they changed).
    auto const& entry = m_entries[m_visible[visibleIndex]];
    SelectionKey key {entry.id, entry.official, entry.packHeader};
    if (key == m_lastSelection) return;
    m_lastSelection = key;

    // Song and coins for the details, the preview and play.
    levels::resolve(m_entries[m_visible[visibleIndex]]);
    auto const& e = m_entries[m_visible[visibleIndex]];
    remembered().selectedId = e.id;
    remembered().selectedOfficial = e.official;
    remembered().selectedHeader = e.packHeader;
    updateWedge();
    m_previewDelay = PREVIEW_DELAY;

    // Background: the level's thumbnail.
    int request = ++m_backgroundRequest;
    Ref<SongSelect> self = this;
    levelThumbnail(e, [self, request](CCTexture2D* texture) {
        if (self->m_backgroundRequest != request) return;
        self->m_background->setImage(texture);
    });
}

bool SongSelect::selectSong(std::string const& path, int songID) {
    // Prefer the level last selected here, if it's one with this song.
    auto& r = remembered();
    levels::Entry const* found = nullptr;
    for (auto& e : m_entries) {
        // Only levels with this song have their song file checked.
        if (e.songID != songID) continue;
        levels::resolve(e);
        if (e.songPath != path) continue;
        if (!found || (e.id == r.selectedId && e.official == r.selectedOfficial)) found = &e;
    }
    if (!found) return false;
    r.selectedId = found->id;
    r.selectedOfficial = found->official;
    m_previewPath = path; // already playing: select() mustn't restart it
    m_hasSelection = false;
    applyFilter();
    if (m_hasSelection && m_entries[m_visible[m_selected]].songPath == path) return true;

    // The filters hide it: show everything in its group instead.
    m_group = found->official ? Group::Official : Group::Saved;
    m_folder = 0;
    m_query.clear();
    if (m_search) m_search->setString("");
    r.group = static_cast<int>(m_group);
    r.folder = 0;
    r.query.clear();
    r.selectedId = found->id;
    r.selectedOfficial = found->official;
    m_hasSelection = false;
    applyFilter();
    return true;
}

void SongSelect::selectRandom() {
    static std::mt19937 rng {std::random_device {}()};
    // Five presses in a few seconds: the cursor has opinions.
    if (quips::spam("random", 5, 3.f)) {
        quips::sayLine(RANDOM_SPAM_CURSOR[std::uniform_int_distribution<size_t>(0, RANDOM_SPAM_CURSOR.size() - 1)(rng)]);
    }
    if (packMode()) {
        // Another pack, opened.
        std::vector<int> others;
        for (size_t v : m_visible) {
            auto const& e = m_entries[v];
            if (e.packHeader && e.pack != m_expandedPack) others.push_back(e.pack);
        }
        if (others.empty()) return;
        sfx::play(sfx::sound::DEFAULT_SELECT);
        expandPack(others[std::uniform_int_distribution<size_t>(0, others.size() - 1)(rng)]);
        return;
    }
    if (m_visible.size() < 2) return;
    size_t index = std::uniform_int_distribution<size_t>(0, m_visible.size() - 2)(rng);
    if (index >= m_selected) index++;
    sfx::play(sfx::sound::DEFAULT_SELECT);
    select(index);
}

void SongSelect::restorePreview() {
    if (m_leaving) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (!m_previewPath.empty() && engine->isMusicPlaying(0)) {
        engine->fadeInMusic(0.3f, 0);
        return;
    }
    m_previewPath.clear();
    previewSong();
}

void SongSelect::openLevelPage() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    // RobTop's levels have no level page: straight into the level. A pack opens.
    if (e.official || e.packHeader || !e.level) {
        start();
        return;
    }
    sfx::play(sfx::sound::DEFAULT_SELECT);
    returnsHere() = true;
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, LevelInfoLayer::scene(e.level, false)));
}

void SongSelect::browseOnline() {
    if (m_starting) return;
    closeFolders();
    // Our search page (over GD's hidden browser) takes over, with the search
    // text already searched; backing out of it comes back here. Levels played
    // from there return to their level page.
    returnsHere() = false;
    browsingOnline() = true;
    auto scene = LevelListingOverlay::searchScene(m_query);
    if (!scene) {
        browsingOnline() = false;
        return;
    }
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

void SongSelect::back() {
    sfx::play(sfx::sound::DEFAULT_SELECT);
    returnsHere() = false;

    // The menu carries on with whatever is playing (osu! keeps the track going).
    m_leaving = true;
    m_previewDelay = -1;
    auto engine = FMODAudioEngine::sharedEngine();
    std::string playing = engine->getActiveMusic(0);
    if (!playing.empty() && engine->isMusicPlaying(0)) {
        // Whatever plays was previewed here, so its level has its song path.
        auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](auto const& e) {
            return e.resolved && e.songPath == playing;
        });
        if (it != m_entries.end()) {
            int songID = it->official ? MusicPlayer::officialSongID(it->level->m_audioTrack) : it->level->m_songID;
            MusicPlayer::Track track {songID, playing, it->songTitle, it->songArtist, {}};
            // (RobTop's level IDs aren't online IDs: no thumbnails to look up for them.)
            if (!it->official) track.levels.push_back({it->id, it->name, it->creator});
            MusicPlayer::get().adopt(std::move(track));
        }
    }
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
}

void SongSelect::previewSong() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.songPath.empty() || e.songPath == m_previewPath) return;
    m_previewPath = e.songPath;
    auto engine = FMODAudioEngine::sharedEngine();
    engine->playMusic(e.songPath, true, 0.5f, 0);
    // No preview points in GD: start a little way in, where most songs have got
    // going. Back from a play (or a cancelled one) of this song: from where it was.
    unsigned length = engine->getMusicLengthMS(0);
    unsigned start = static_cast<unsigned>(length * 0.35f);
    if (g_resume.path == e.songPath && g_resume.ms > 0 && g_resume.ms + 1000 < length) start = g_resume.ms;
    g_resume = {};
    if (length > 0) engine->setMusicTimeMS(start, true, 0);
}

void SongSelect::update(float dt) {
    float ms = dt * 1000.f;
    m_enterMs += ms;
    // GD's mouse dispatcher only feeds its newest delegate, and GD's song widget
    // (or mods extending it) can register one: take the wheel back now and then.
    m_wheelClaimMs += ms;
    if (m_wheelClaimMs > 500 && !g_overlayOpen) {
        m_wheelClaimMs = 0;
        auto dispatcher = CCDirector::get()->getMouseDispatcher();
        dispatcher->removeDelegate(this);
        dispatcher->addDelegate(this);
    }
    // An overlay (the comments page) covers everything: the search box mustn't
    // take taps through it, and nothing here hovers.
    if (m_search && m_searchEnabled == g_overlayOpen) {
        m_searchEnabled = !g_overlayOpen;
        m_search->setEnabled(m_searchEnabled);
    }
    // Playing: only the loader animates; song select is frozen and fading.
    if (m_starting) {
        updateLoader(dt);
        return;
    }
    updateCarousel(dt);
    updateScrollbar(dt);

    m_wedgeAlpha.update(dt);
    if (m_loadingSpinner) m_loadingSpinner->setRotation(m_loadingSpinner->getRotation() + dt * 300.f);
    if (m_noResultsMs >= 0) {
        m_noResultsMs -= ms;
        bool searching = packMode() && packs::loadingLevels();
        if (m_noResultsMs < 0 && !m_query.empty() && m_visible.empty() && !searching && m_saidFor != m_query) {
            m_saidFor = m_query;
            cursorSay(NO_RESULTS_CURSOR[pickLine(m_query + "!", NO_RESULTS_CURSOR.size())]);
        }
    }
    m_wedge->setPositionX(-24 * m_k * (1.f - m_wedgeAlpha.get()));

    if (m_previewDelay >= 0) {
        m_previewDelay -= ms;
        if (m_previewDelay < 0) previewSong();
    } else if (!m_leaving && !m_previewPath.empty() && !FMODAudioEngine::sharedEngine()->isMusicPlaying(0)) {
        // The menu's song (not looped, unlike previews) ran out: loop it like one.
        m_previewPath.clear();
        previewSong();
    }

    auto mouse = geode::cocos::getMousePos();
    auto updateButton = [&](Button& b) {
        bool hovered = !g_overlayOpen && hittable(b, mouse);
        if (hovered != b.hovered) {
            b.hovered = hovered;
            b.hover.to(hovered ? 1.f : 0.f, hovered ? 100 : 400, Easing::OutQuint);
            if (hovered) sfx::hover(sfx::sound::BUTTON_HOVER);
        }
        b.hover.update(dt);
        auto base = b.selected ? theme::COLOUR3 : b.color;
        b.bg->setFillColor(theme::lerp(base, {255, 255, 255, 255}, b.hover.get() * 0.15f));
    };
    for (auto& b : m_tabs) updateButton(b);
    for (auto& b : m_buttons) updateButton(b);
    for (auto& b : m_wedgeButtons) updateButton(b);
    for (auto& b : m_folderItems) updateButton(b);
    updateSongCard();
}

// --- input ---

size_t SongSelect::panelAt(CCPoint world) {
    if (world.y < m_carouselBottom || world.y > m_carouselTop) return SIZE_MAX;
    for (auto& [index, panel] : m_panels) {
        if (containsWorld(panel.root, world)) return index;
    }
    return SIZE_MAX;
}

bool SongSelect::hittable(Button const& b, CCPoint world) {
    for (CCNode* n = b.node; n; n = n->getParent()) {
        if (!n->isVisible()) return false;
    }
    if (b.clip && !b.clip->containsWorldPoint(world)) return false;
    return containsWorld(b.node, world);
}

SongSelect::Button* SongSelect::buttonAt(CCPoint world) {
    // The open dropdown sits on top of everything.
    for (auto& b : m_folderItems) {
        if (hittable(b, world)) return &b;
    }
    for (auto list : {&m_tabs, &m_buttons, &m_wedgeButtons}) {
        for (auto& b : *list) {
            if (hittable(b, world)) return &b;
        }
    }
    return nullptr;
}

bool SongSelect::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    // An overlay is open over song select: its own text box may want the touch.
    if (g_overlayOpen) return false;
    // Let the search field take its own touches.
    if (m_search && containsWorld(m_search, loc)) return false;
    if (m_starting) return true;
    m_touchDown = true;
    m_dragging = false;
    m_touchStart = m_touchLast = loc;
    // The scrollbar: grab it where it was touched, or (touching the track
    // beside it) jump there and hold it by the middle.
    if (!m_folderMenu && scrollbarHit(loc)) {
        m_barDragging = true;
        m_barGrab = std::abs(loc.y - m_barY) <= m_barLength / 2 ? loc.y - m_barY : 0.f;
        m_barHighlight.to(1.f, 100, Easing::None);
        m_barWidth.to(SCROLLBAR_HELD_WIDTH, 400, Easing::OutElastic);
        m_barLabelAlpha.to(1.f, 150, Easing::OutQuint);
        m_barText.clear();
        dragScrollbar(loc);
        return true;
    }
    m_pressed = buttonAt(loc);
    // A tap outside the open folder list closes it.
    if (m_folderMenu) {
        bool inMenu = m_pressed && (m_pressed == &m_buttons[m_folderButton]
            || (m_pressed >= m_folderItems.data() && m_pressed < m_folderItems.data() + m_folderItems.size()));
        if (!inMenu) {
            closeFolders();
            m_pressed = nullptr;
            m_touchDown = false;
            return true;
        }
    }
    m_detailsDrag.began(m_details, loc);
    return true;
}

void SongSelect::ccTouchMoved(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    if (m_barDragging) {
        dragScrollbar(loc);
        return;
    }
    if (!m_touchDown) return;
    // Dragging the details scrolls them, and cancels a press on their buttons.
    if (m_detailsDrag.moved(loc)) {
        m_pressed = nullptr;
        m_touchLast = loc;
        return;
    }
    bool inCarousel = m_touchStart.x > m_win.width - m_rightW && m_touchStart.y > m_carouselBottom && m_touchStart.y < m_carouselTop;
    if (!m_dragging && inCarousel && !m_pressed && std::abs(loc.y - m_touchStart.y) > 8 * m_k) m_dragging = true;
    if (m_dragging) m_scrollTarget += loc.y - m_touchLast.y;
    m_touchLast = loc;
}

void SongSelect::ccTouchEnded(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    if (m_barDragging) {
        m_barDragging = false;
        m_touchDown = false;
        m_barHighlight.to(0.f, 100, Easing::None);
        m_barWidth.to(1.f, 300, Easing::OutQuint);
        // Let go: the rubber snaps back and wobbles.
        m_barPull.to(0.f, 600, Easing::OutElastic);
        m_barLabelAlpha.to(0.f, 300, Easing::OutQuint);
        return;
    }
    bool wasDragging = m_dragging;
    bool wasDown = m_touchDown;
    m_touchDown = false;
    m_dragging = false;
    bool scrolledDetails = m_detailsDrag.ended();
    if (m_refreshPending) refreshDetails();
    if (!wasDown || wasDragging || scrolledDetails) {
        m_pressed = nullptr;
        return;
    }

    if (m_pressed) {
        if (buttonAt(loc) == m_pressed) {
            sfx::click(sfx::sound::BUTTON_SELECT);
            auto action = m_pressed->action;
            m_pressed = nullptr;
            if (action) action();
        }
        m_pressed = nullptr;
        return;
    }

    size_t index = panelAt(loc);
    if (index == SIZE_MAX || index >= m_visible.size()) return;
    auto const& row = m_entries[m_visible[index]];
    // A pack opens on a click and closes on another, like osu!'s beatmap sets.
    if (row.packHeader) {
        sfx::play(sfx::sound::DEFAULT_SELECT);
        expandPack(row.pack == m_expandedPack ? -1 : row.pack);
        return;
    }
    // Clicking the selected level plays it, like osu!.
    if (m_hasSelection && index == m_selected) {
        start();
        return;
    }
    sfx::play(sfx::sound::DEFAULT_SELECT);
    select(index);
}

void SongSelect::scrollWheel(float y, float) {
    if (m_starting || Dialog::isOpen() || g_overlayOpen) return;
    // Left side: the level details; right side: the carousel.
    if (geode::cocos::getMousePos().x < m_win.width - m_rightW) {
        if (m_details) m_details->scrollWheel(y, 0);
        return;
    }
    // Positive = down; one notch moves about one and a half panels.
    float notches = std::clamp(y / 12.f, -3.f, 3.f);
    m_scrollTarget += notches * (m_panelH + m_spacing) * 1.5f;
}

void SongSelect::keyDown(enumKeyCodes key, double timestamp) {
    // A dialog or an overlay (the comments page) has the keys, and handles Escape itself.
    if (Dialog::isOpen() || g_overlayOpen) return;
    // GD's CCLayer::keyDown turns Escape into keyBackClicked: let it through.
    if (!m_hasSelection || m_starting) return CCLayer::keyDown(key, timestamp);
    // Stepping onto a closed pack opens it (osu! moves through the sets' difficulties).
    auto step = [this](size_t to) {
        sfx::play(sfx::sound::DEFAULT_HOVER);
        auto const& e = m_entries[m_visible[to]];
        if (e.packHeader && e.pack != m_expandedPack) expandPack(e.pack);
        else select(to);
    };
    switch (key) {
        case KEY_Up:
            if (m_selected > 0) step(m_selected - 1);
            break;
        case KEY_Down:
            if (m_selected + 1 < m_visible.size()) step(m_selected + 1);
            break;
        case KEY_Enter:
            start();
            break;
        case KEY_F2:
            selectRandom();
            break;
        default:
            CCLayer::keyDown(key, timestamp);
            break;
    }
}

void SongSelect::keyBackClicked() {
    if (Dialog::isOpen() || g_overlayOpen) return;
    // During the loader, back cancels it (osu!'s back button does the same).
    if (m_starting) return cancelLoader();
    back();
}

} // namespace lazer
