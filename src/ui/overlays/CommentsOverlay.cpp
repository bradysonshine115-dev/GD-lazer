#include "CommentsOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../../settings/Account.hpp"
#include "../core/Text.hpp"
#include "Dialog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Blue overlay scheme: the beatmap overlay's, where its comments live.
    constexpr theme::Scheme SCHEME {200};

    // osu! sizes (768 px tall screen), scaled by m_k.
    constexpr float HORIZONTAL_PADDING = 50.f;  // WaveOverlayContainer
    constexpr float COUNTER_HEIGHT = 50.f;      // TotalCommentsCounter
    constexpr float EDITOR_PADDING = 20.f;      // around the editor
    constexpr float EDITOR_AVATAR = 50.f;
    constexpr float EDITOR_BORDER = 3.f;        // CommentEditor.BorderThickness
    constexpr float EDITOR_SIDE = 8.f;          // CommentEditor.side_padding
    constexpr float TEXTBOX_HEIGHT = 40.f;      // CommentEditor's text box
    constexpr float EDITOR_FOOTER = 35.f;
    constexpr float BUTTON_WIDTH = 80.f;        // EditorButton
    constexpr float BUTTON_HEIGHT = 25.f;
    constexpr float HEADER_HEIGHT = 40.f;       // CommentsHeader
    constexpr float TAB_HEIGHT = 20.f;          // HeaderButton
    constexpr float AVATAR = 40.f;              // DrawableComment.avatar_size
    constexpr float COMMENT_PADDING = 15.f;     // DrawableComment's vertical padding
    constexpr float VOTE_HEIGHT = 20.f;         // VotePill
    constexpr float PLACEHOLDER_HEIGHT = 80.f;  // NoCommentsPlaceholder
    constexpr float INFO_PADDING = 15.f;        // around the description and its chips
    constexpr float COPIED_MS = 1500.f;         // "copied" stays this long
    // GD's limit for a level comment (ShareCommentLayer's charLimit).
    constexpr int COMMENT_LIMIT = 100;
    // GD reports its own request failures; this catches one that never reports.
    constexpr float LOAD_TIMEOUT_MS = 30000.f;
    constexpr float SPIN_SPEED = 300.f;         // spinner, degrees per second

    constexpr ccColor4B GREEN_LIGHT {0xb3, 0xd9, 0x44, 255}; // OsuColour.GreenLight: VotePill's accent
    constexpr ccColor4B DANGER {204, 51, 85, 255};
    constexpr ccColor4B SEPARATOR {26, 26, 26, 255};        // OsuColour.Gray(0.1f)
    constexpr ccColor3B MUTED {128, 128, 128};              // OsuColour.Gray(0.5f): deleted comments
    constexpr ccColor4B CLEAR {0, 0, 0, 0};

    // Pill tags: the sort tabs are TAG_TAB + the sort they pick.
    constexpr int TAG_TAB = 1;

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

    std::string trim(std::string s) {
        auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (!s.empty() && space(s.back())) s.pop_back();
        size_t start = 0;
        while (start < s.size() && space(s[start])) start++;
        return s.substr(start);
    }

    // A player's icon (the one they show, like GD's comment cells) in their colours.
    CCNode* playerIcon(int id, IconType type, int color1, int color2, int glowColor, bool glow, float size) {
        auto gm = GameManager::get();
        auto player = SimplePlayer::create(1);
        player->updatePlayerFrame(std::max(1, id), type);
        player->setColors(gm->colorForIdx(color1), gm->colorForIdx(color2));
        if (glow) player->setGlowOutline(gm->colorForIdx(glowColor));
        else player->disableGlowOutline();
        player->setScale(size / 30.f);
        return player;
    }

    // Nothing in a hidden GD layer may take touches: its list, menus and
    // loading circle would otherwise still catch them (invisible) over ours.
    void deafen(CCNode* node) {
        if (auto layer = typeinfo_cast<CCLayer*>(node)) layer->setTouchEnabled(false);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) deafen(child);
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
}

bool CommentsOverlay::wants(InfoLayer* layer) {
    if (!layer || !Mod::get()->getSettingValue<bool>("enabled")) return false;
    // A player's comment history (m_score) and a list's comments keep GD's
    // page; so does a level that isn't online (no comments to show).
    return layer->m_level && !layer->m_score && !layer->m_levelList && layer->m_level->m_levelID.value() > 0;
}

bool CommentsOverlay::present(GJGameLevel* level, InfoLayer* gdLayer) {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene || !level) return false;
    auto overlay = new CommentsOverlay();
    if (!overlay->init(level)) {
        delete overlay;
        return false;
    }
    overlay->autorelease();
    if (gdLayer) {
        // GD's page, kept (other mods may hold on to it or hook it) but never
        // drawn or touched. It isn't shown, so it never registered for input.
        gdLayer->setUserObject("hidden"_spr, CCBool::create(true));
        deafen(gdLayer);
        if (auto circle = gdLayer->m_loadingCircle) {
            // It may sit in the scene rather than the layer; it never finishes here.
            circle->setTouchEnabled(false);
            circle->setVisible(false);
        }
        gdLayer->setKeypadEnabled(false);
        gdLayer->setKeyboardEnabled(false);
        gdLayer->setVisible(false);
        overlay->addChild(gdLayer, -10);
    }
    // Under GD's own popups (z 105) and our dialogs, over everything else,
    // like the profile page (which opens over this one from a name).
    scene->addChild(overlay, 100);
    overlay->open();
    return true;
}

bool CommentsOverlay::init(GJGameLevel* level) {
    std::string name = level->m_levelName;
    if (name.size() > 30) name = name.substr(0, 28) + "...";
    std::string creator = level->m_creatorName;
    if (creator.empty()) creator = "unknown";
    if (!WaveOverlay::init(0, SCHEME, icon::COMMENTS, name, "level by " + creator, 72.f)) return false;
    m_level = level;
    m_levelID = level->m_levelID.value();
    m_pad = HORIZONTAL_PADDING * m_k;
    m_comments = CCArray::create();
    // GD attaches your best percent to a comment unless you turn that off;
    // platformers have no percent.
    m_includePercent = !level->isPlatformer() && level->m_normalPercent.value() > 0;
    m_wasLoggedIn = loggedIn();

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);

    // The top part is built once; the comments under it are rebuilt as they load.
    float y = buildInfo(0);
    y = buildCounter(y);
    y = buildEditor(y);
    y = buildSortHeader(y);
    m_listTop = y;
    m_list = CCNode::create();
    m_list->setPosition({0, -m_listTop});
    m_scroll->content()->addChild(m_list);

    updateEditor();
    load(0);
    return true;
}

CommentsOverlay::~CommentsOverlay() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
}

void CommentsOverlay::onEnter() {
    WaveOverlay::onEnter();
    CCDirector::get()->getKeypadDispatcher()->addDelegate(this);
}

void CommentsOverlay::onExit() {
    CCDirector::get()->getKeypadDispatcher()->removeDelegate(this);
    // GD keeps raw pointers to its delegates: a request finishing after the
    // scene changed mustn't call into a dead page.
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
    WaveOverlay::onExit();
}

void CommentsOverlay::keyBackClicked() {
    close();
}

void CommentsOverlay::onClosed() {
    this->removeFromParent();
}

bool CommentsOverlay::loggedIn() const {
    return account::loggedIn();
}

bool CommentsOverlay::covered() {
    auto parent = this->getParent();
    if (!parent || !parent->getChildren()) return false;
    bool after = false;
    for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
        if (child == this) {
            after = true;
            continue;
        }
        if (after && child->isVisible() && typeinfo_cast<WaveOverlay*>(child)) return true;
    }
    return false;
}

// --- loading, posting, voting (GameLevelManager) ---

void CommentsOverlay::load(int page) {
    auto glm = GameLevelManager::sharedState();
    m_state = State::Loading;
    m_loadingMs = 0;
    m_pendingTotal = -1;
    m_page = page;
    m_key = std::string(glm->getCommentKey(m_levelID, page, static_cast<int>(m_sort), CommentKeyType::Level));
    glm->m_levelCommentDelegate = this;
    // `total` is the count GD already knows, so the server can skip counting again.
    glm->getLevelComments(m_levelID, page, std::max(0, m_total), static_cast<int>(m_sort), CommentKeyType::Level);
    rebuild();
}

void CommentsOverlay::reload(bool resetCache) {
    // GD keeps comment pages for a while; a refresh (and a new comment) wants fresh ones.
    if (resetCache) GameLevelManager::sharedState()->resetCommentTimersForLevelID(m_levelID, CommentKeyType::Level);
    m_comments->removeAllObjects();
    m_total = -1;
    load(0);
}

void CommentsOverlay::setupPageInfo(gd::string info, char const* key) {
    if (!key) return;
    // "total:start:count" (GameLevelManager::createPageInfo). GD sends it with
    // the page; whether before or after it, the count lands.
    auto parts = utils::string::split(std::string(info), ":");
    int total = parts.empty() ? -1 : utils::numFromString<int>(parts[0]).unwrapOr(-1);
    if (total < 0) return;
    if (m_key == key) {
        m_pendingTotal = total;
    } else if (m_lastKey == key) {
        m_total = total;
        m_dirty = true;
    }
}

void CommentsOverlay::loadCommentsFinished(CCArray* comments, char const* key) {
    // Only the page asked for: a stale request (a sort switched mid-load) or
    // a profile's posts (the same delegate slot) are someone else's.
    if (!key || m_key != key) return;
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    m_lastKey = m_key;
    m_key.clear();

    int added = 0;
    if (comments) {
        for (auto comment : CCArrayExt<GJComment*>(comments)) {
            bool seen = false;
            for (auto shown : CCArrayExt<GJComment*>(m_comments)) {
                if (shown->m_commentID == comment->m_commentID) { seen = true; break; }
            }
            if (seen) continue;
            m_comments->addObject(comment);
            added++;
        }
    }
    if (m_pendingTotal >= 0) m_total = m_pendingTotal;
    else if (added == 0) m_total = static_cast<int>(m_comments->count()); // no page info and nothing new: that's all of them
    m_page++;
    m_state = State::Loaded;
    rebuild();
}

void CommentsOverlay::loadCommentsFailed(char const* key) {
    if (!key || m_key != key) return;
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    m_key.clear();
    m_state = State::Failed;
    rebuild();
}

void CommentsOverlay::post() {
    if (m_posting) return;
    if (!loggedIn()) return askSignIn("comment on levels");
    std::string text = m_input ? trim(std::string(m_input->getString())) : "";
    if (text.empty()) return;
    if (text.size() > static_cast<size_t>(COMMENT_LIMIT)) text = text.substr(0, COMMENT_LIMIT);
    m_posting = true;
    if (m_input) m_input->defocus();
    auto glm = GameLevelManager::sharedState();
    glm->m_commentUploadDelegate = this;
    int percent = m_includePercent ? m_level->m_normalPercent.value() : 0;
    glm->uploadLevelComment(m_levelID, text, percent);
    updateEditor();
}

void CommentsOverlay::commentUploadFinished(int) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
    m_posting = false;
    if (m_input) m_input->setString("");
    // GD doesn't hand the new comment back: show the newest, where it is.
    m_sort = Sort::Recent;
    reload(true);
    updateEditor();
}

void CommentsOverlay::commentUploadFailed(int, CommentError error) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
    m_posting = false;
    updateEditor();
    Dialog::show(icon::TRIANGLE_EXCLAMATION, "Couldn't post your comment",
        error == CommentError::Banned
            ? "Your account isn't allowed to comment."
            : "GD didn't take it. It may be too soon after your last comment: try again in a bit.",
        {{"OK", Dialog::Kind::Cancel, nullptr}});
}

void CommentsOverlay::vote(GJComment* comment, bool like) {
    if (!comment) return;
    if (!loggedIn()) return askSignIn("vote on comments");
    int id = comment->m_commentID;
    auto glm = GameLevelManager::sharedState();
    // One vote per comment (GD remembers yours on this device).
    if (m_votes.contains(id) || glm->hasLikedItem(LikeItemType::Comment, id, true, m_levelID)
        || glm->hasLikedItem(LikeItemType::Comment, id, false, m_levelID)) return;
    // GD's LikeItemLayer, without the popup.
    glm->likeItem(LikeItemType::Comment, id, like, m_levelID);
    m_votes[id] = like;
    // Count it right away, like GD's comment cells do.
    comment->m_likeCount += like ? 1 : -1;
    float scroll = m_scroll->scroll();
    rebuild();
    m_scroll->scrollTo(scroll, false);
}

void CommentsOverlay::askSignIn(char const* what) {
    Dialog::show(icon::USER, "Sign in first", fmt::format("You need a GD account to {}.", what), {
        {"Sign in", Dialog::Kind::Ok, [] { account::logIn(); }},
        {"Not now", Dialog::Kind::Cancel, nullptr},
    });
}

// --- building ---

CommentsOverlay::Pill& CommentsOverlay::addPill(std::vector<Pill>& list, CCNode* parent, CCSize size, float radius,
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
    return list.back();
}

float CommentsOverlay::buildInfo(float y) {
    float k = m_k, W = bodySize().width;
    auto content = m_scroll->content();
    y += INFO_PADDING * k;

    // The description, as GD's InfoLayer shows it (with its words for none).
    std::string desc = trim(std::string(m_level->getUnpackedLevelDescription()));
    bool none = desc.empty();
    if (none) desc = "(No description provided)";
    auto text = makeWrappedText(desc, 14 * k, W - 2 * m_pad, theme::rgb(none ? m_scheme.foreground1() : m_scheme.content2()));
    text->setPosition({m_pad, -y});
    content->addChild(text, 1);
    y += text->getContentSize().height + 10 * k;

    // Chips under it, flowing onto another line on narrow screens: the ID
    // (tap to copy, like GD's copy button), when it was uploaded and updated,
    // what it's a copy of, and GD's own page.
    float h = TAB_HEIGHT * k, gap = 5 * k, x = m_pad, top = y;
    auto place = [&](float w) {
        if (x > m_pad && x + w > W - m_pad) {
            x = m_pad;
            top += h + gap;
        }
        CCPoint at {x, -(top + h / 2)};
        x += w + gap;
        return at;
    };
    auto muted = theme::rgb(m_scheme.foreground1());
    // A bare icon and label (nothing to tap).
    auto chip = [&](char const* glyph, std::string const& label) {
        auto icon = makeIcon(glyph, 10 * k);
        icon->setColor(muted);
        auto name = makeText(label, Weight::SemiBold, 12 * k);
        name->setColor(muted);
        float iconW = icon->getScaledContentSize().width;
        float w = iconW + 5 * k + name->getScaledContentSize().width + 10 * k;
        auto at = place(w);
        icon->setPosition({at.x + 5 * k + iconW / 2, at.y});
        content->addChild(icon, 1);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({at.x + 5 * k + iconW + 5 * k, at.y});
        content->addChild(name, 1);
    };
    // A header-style button (the sort header's refresh): background on hover.
    auto button = [&](char const* glyph, std::string const& label, float minLabelW, std::function<void()> action) {
        auto icon = makeIcon(glyph, 10 * k);
        auto name = makeText(label, Weight::SemiBold, 12 * k);
        float iconW = icon->getScaledContentSize().width;
        float labelW = std::max(minLabelW, name->getScaledContentSize().width);
        float w = iconW + 5 * k + labelW + 20 * k;
        auto& pill = addPill(m_fixedPills, content, {w, h}, 3 * k, place(w), {0, 0.5f}, CLEAR,
                             m_scheme.background3(), std::move(action));
        icon->setPosition({10 * k + iconW / 2, h / 2});
        pill.node->addChild(icon, 1);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({10 * k + iconW + 5 * k, h / 2});
        pill.node->addChild(name, 1);
        return name;
    };

    int id = m_levelID;
    m_idText = fmt::format("ID {}", id);
    float copiedW = makeText("copied", Weight::SemiBold, 12 * k)->getScaledContentSize().width;
    m_idLabel = button(icon::COPY, m_idText, copiedW, [this, id] {
        utils::clipboard::write(std::to_string(id));
        m_copiedMs = COPIED_MS;
        m_idLabel->setString("copied");
    });
    std::string uploaded = m_level->m_uploadDate;
    std::string updated = m_level->m_updateDate;
    if (!uploaded.empty()) chip(icon::CLOUD_UP, fmt::format("uploaded {} ago", uploaded));
    if (!updated.empty() && updated != uploaded) chip(icon::ROTATE, fmt::format("updated {} ago", updated));
    int original = m_level->m_originalLevel.value();
    if (original > 0 && original != id) chip(icon::LINK, fmt::format("copy of {}", original));
    button(icon::CIRCLE_INFO, "more info", 0, [this] { this->openGDPage(); });

    y = top + h + INFO_PADDING * k;
    // A thin line between the level's part and the comments.
    auto line = CCLayerColor::create(SEPARATOR);
    line->setContentSize({W, 1.5f * k});
    line->setPosition({0, -y});
    content->addChild(line);
    return y;
}

void CommentsOverlay::openGDPage() {
    // GD's own page for the level: its level info, "original" and other
    // mods' buttons. Marked so the hook below lets GD show it.
    auto layer = InfoLayer::create(m_level, nullptr, nullptr);
    if (!layer) return;
    layer->setUserObject("vanilla"_spr, CCBool::create(true));
    layer->show();
}

float CommentsOverlay::buildCounter(float y) {
    float k = m_k, h = COUNTER_HEIGHT * k;
    auto content = m_scroll->content();
    float cy = -(y + h / 2);

    // "comments" and, once known, how many in a small dark pill.
    auto title = makeText("comments", Weight::Regular, 20 * k);
    title->setColor(theme::rgb(m_scheme.light1()));
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({m_pad, cy});
    content->addChild(title);

    float x = m_pad + title->getScaledContentSize().width + 5 * k;
    float pillH = 24 * k;
    m_countBg = RoundedBox::create({40 * k, pillH}, pillH / 2, m_scheme.background6());
    m_countBg->setAnchorPoint({0, 0.5f});
    m_countBg->setPosition({x, cy});
    m_countBg->setVisible(false);
    content->addChild(m_countBg);
    m_countLabel = makeText("0", Weight::Bold, 14 * k);
    m_countLabel->setColor(theme::rgb(m_scheme.foreground1()));
    m_countLabel->setAnchorPoint({0, 0.5f});
    m_countLabel->setPosition({x + 10 * k, cy});
    m_countLabel->setVisible(false);
    content->addChild(m_countLabel, 1);
    return y + h;
}

float CommentsOverlay::buildEditor(float y) {
    float k = m_k, W = bodySize().width;
    auto content = m_scroll->content();
    float top = y + EDITOR_PADDING * k;
    float editorH = (TEXTBOX_HEIGHT + EDITOR_FOOTER) * k;

    // Your icon beside the box, where osu! shows your avatar.
    auto gm = GameManager::get();
    float av = EDITOR_AVATAR * k;
    CCPoint avatarAt {m_pad + av / 2, -(top + av / 2)};
    auto tile = RoundedBox::create({av, av}, av / 2, m_scheme.background6());
    tile->setPosition(avatarAt);
    content->addChild(tile);
    auto me = playerIcon(gm->getPlayerFrame(), IconType::Cube, gm->getPlayerColor(), gm->getPlayerColor2(),
                         gm->getPlayerGlowColor(), gm->getPlayerGlow(), av * 0.62f);
    me->setPosition(avatarAt);
    content->addChild(me, 1);

    // The editor: a bordered box with the text box on top and a footer of buttons.
    float ex = m_pad + 60 * k, ew = W - m_pad - ex;
    float border = EDITOR_BORDER * k;
    auto frame = RoundedBox::create({ew, editorH}, 6 * k, m_scheme.background3());
    frame->setAnchorPoint({0, 1});
    frame->setPosition({ex, -top});
    content->addChild(frame);
    float fieldH = TEXTBOX_HEIGHT * k - border;
    auto field = RoundedBox::create({ew - border * 2, fieldH}, 3 * k, m_scheme.background5());
    field->setAnchorPoint({0, 1});
    field->setPosition({ex + border, -(top + border)});
    content->addChild(field);

    // GD's text input (Geode's box is 30 tall: scaled so its text suits the page).
    float scale = k;
    float fieldCy = -(top + border + fieldH / 2);
    m_input = TextInput::create((ew - EDITOR_SIDE * 2 * k) / scale, "type your comment here", "outfit-regular.fnt"_spr);
    m_input->hideBG();
    m_input->setTextAlign(TextInputAlign::Left);
    m_input->setScale(scale);
    m_input->setAnchorPoint({0, 0.5f});
    m_input->setPosition({ex + EDITOR_SIDE * k, fieldCy});
    m_input->setMaxCharCount(COMMENT_LIMIT);
    m_input->setDelegate(this);
    content->addChild(m_input, 2);

    // Footer: the percent toggle on the left, post (or sign in) on the right.
    float footerCy = -(top + TEXTBOX_HEIGHT * k + EDITOR_FOOTER * k / 2);
    float right = ex + ew - EDITOR_SIDE * k;
    float bw = BUTTON_WIDTH * k, bh = BUTTON_HEIGHT * k;
    auto accent = m_scheme.colour3();
    auto accentHover = theme::lerp(accent, ccColor4B {255, 255, 255, 255}, 0.2f); // RoundedButton lightens on hover
    auto button = [&](std::string const& text, float width, std::function<void()> action) -> size_t {
        auto& pill = addPill(m_fixedPills, content, {width, bh}, 5 * k, {right, footerCy}, {1, 0.5f}, accent, accentHover,
                             std::move(action));
        auto label = makeText(text, Weight::Bold, 12 * k);
        label->setPosition({width / 2, bh / 2});
        pill.node->addChild(label, 1);
        return m_fixedPills.size() - 1;
    };
    m_postPill = button("post", bw, [this] { this->post(); });
    m_signInPill = button("sign in", 100 * k, [this] { this->askSignIn("comment on levels"); });
    m_postSpinner = makeSpinner(18 * k, {255, 255, 255});
    m_postSpinner->setPosition({right - 100 * k - 5 * k - 9 * k, footerCy});
    m_postSpinner->setVisible(false);
    content->addChild(m_postSpinner, 2);

    // GD's percent toggle (ShareCommentLayer's), as a header-style check button.
    if (!m_level->isPlatformer() && m_level->m_normalPercent.value() > 0) {
        auto label = makeText(fmt::format("include my best ({}%)", m_level->m_normalPercent.value()), Weight::SemiBold, 12 * k);
        float box = 10 * k;
        float w = box + 5 * k + label->getScaledContentSize().width + 20 * k;
        auto& pill = addPill(m_fixedPills, content, {w, TAB_HEIGHT * k}, 3 * k, {ex + EDITOR_SIDE * k, footerCy}, {0, 0.5f},
                             CLEAR, m_scheme.background4(), [this] {
            m_includePercent = !m_includePercent;
            sfx::play(m_includePercent ? sfx::sound::CHECK_ON : sfx::sound::CHECK_OFF);
            updateEditor();
        });
        pill.silent = true;
        m_percentPill = m_fixedPills.size() - 1;
        m_percentBox = RoundedBox::create({box, box}, 2 * k, m_scheme.background5());
        m_percentBox->setBorder(1.5f * k, m_scheme.light1());
        m_percentBox->setPosition({10 * k + box / 2, TAB_HEIGHT * k / 2});
        pill.node->addChild(m_percentBox, 1);
        m_percentCheck = makeIcon(icon::CHECK, 7 * k);
        m_percentCheck->setPosition(m_percentBox->getPosition());
        pill.node->addChild(m_percentCheck, 2);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({10 * k + box + 5 * k, TAB_HEIGHT * k / 2});
        pill.node->addChild(label, 1);
    }
    return y + EDITOR_PADDING * 2 * k + editorH;
}

float CommentsOverlay::buildSortHeader(float y) {
    float k = m_k, W = bodySize().width, h = HEADER_HEIGHT * k;
    auto content = m_scroll->content();
    auto bg = CCLayerColor::create(m_scheme.background4());
    bg->setContentSize({W, h});
    bg->setPosition({0, -(y + h)});
    content->addChild(bg);

    float cy = -(y + h / 2);
    auto sortLabel = makeText("Sort", Weight::SemiBold, 12 * k);
    sortLabel->setAnchorPoint({0, 0.5f});
    sortLabel->setPosition({m_pad, cy});
    content->addChild(sortLabel, 1);

    // TabButton: the background shows while active or hovered, the active
    // one reads bold and in Light1.
    float x = m_pad + sortLabel->getScaledContentSize().width + 10 * k;
    for (auto [name, sort] : {std::pair {"recent", Sort::Recent}, std::pair {"top", Sort::Top}}) {
        auto normal = makeText(name, Weight::SemiBold, 12 * k);
        auto bold = makeText(name, Weight::Bold, 12 * k);
        float w = std::max(normal->getScaledContentSize().width, bold->getScaledContentSize().width) + 20 * k;
        auto& tab = addPill(m_fixedPills, content, {w, TAB_HEIGHT * k}, 3 * k, {x, cy}, {0, 0.5f}, CLEAR,
                            m_scheme.background3(), [this, sort = sort] {
            if (m_sort == sort || m_state == State::Loading) return;
            m_sort = sort;
            reload(false);
        });
        tab.tag = TAG_TAB + static_cast<int>(sort);
        tab.tinted = {normal, bold};
        for (auto label : tab.tinted) {
            label->setPosition({w / 2, TAB_HEIGHT * k / 2});
            tab.node->addChild(label, 1);
        }
        x += w + 5 * k;
    }

    // Refresh, at the right (GD's comments have one; osu! keeps "show deleted" there).
    auto refreshIcon = makeIcon(icon::ROTATE, 10 * k);
    auto refreshLabel = makeText("refresh", Weight::SemiBold, 12 * k);
    float iconW = refreshIcon->getScaledContentSize().width;
    float w = iconW + 5 * k + refreshLabel->getScaledContentSize().width + 20 * k;
    auto& refresh = addPill(m_fixedPills, content, {w, TAB_HEIGHT * k}, 3 * k, {W - m_pad, cy}, {1, 0.5f}, CLEAR,
                            m_scheme.background3(), [this] {
        if (m_state != State::Loading) reload(true);
    });
    refreshIcon->setPosition({10 * k + iconW / 2, TAB_HEIGHT * k / 2});
    refresh.node->addChild(refreshIcon, 1);
    refreshLabel->setAnchorPoint({0, 0.5f});
    refreshLabel->setPosition({10 * k + iconW + 5 * k, TAB_HEIGHT * k / 2});
    refresh.node->addChild(refreshLabel, 1);
    return y + h;
}

void CommentsOverlay::rebuild() {
    float k = m_k;
    m_list->removeAllChildren();
    m_pills.clear();
    m_spinners.clear();
    m_pressed = nullptr;
    m_dirty = false;

    // The counter, once the server has said how many.
    bool known = m_total >= 0;
    m_countBg->setVisible(known);
    m_countLabel->setVisible(known);
    if (known) {
        m_countLabel->setString(withCommas(m_total).c_str());
        auto size = m_countBg->getContentSize();
        m_countBg->setContentSize({m_countLabel->getScaledContentSize().width + 20 * k, size.height});
    }

    float y = 0;
    auto note = [&](std::string const& text) {
        float h = PLACEHOLDER_HEIGHT * k;
        auto label = makeText(text, Weight::Regular, 16 * k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({m_pad, -(y + h / 2)});
        m_list->addChild(label);
        y += h;
    };
    for (auto comment : CCArrayExt<GJComment*>(m_comments)) y = buildComment(comment, y);
    if (m_state == State::Loaded && m_comments->count() == 0) note("No comments yet.");
    if (m_state == State::Failed) note(m_comments->count() == 0 ? "Couldn't load the comments." : "Couldn't load more comments.");
    y = buildFooter(y);

    m_scroll->setContentHeight(m_listTop + y);
    m_scroll->claimWheel();
}

float CommentsOverlay::buildComment(GJComment* comment, float y) {
    float k = m_k, W = bodySize().width;
    auto glm = GameLevelManager::sharedState();
    auto content = m_list;
    // Like GD's CommentCell: the author's name is in their user score, and a
    // missing account ID comes from the user IDs GD has seen (kept on the comment).
    std::string author = comment->m_userScore ? std::string(comment->m_userScore->m_userName) : "";
    if (author.empty()) author = glm->userNameForUserID(comment->m_userID);
    if (comment->m_accountID <= 0 && comment->m_userScore) comment->m_accountID = comment->m_userScore->m_accountID;
    if (comment->m_accountID <= 0) comment->m_accountID = glm->accountIDForUserID(comment->m_userID);
    int me = GJAccountManager::get()->m_accountID;
    bool own = me > 0 && comment->m_accountID == me;
    bool hidden = comment->m_isSpam || comment->m_commentDeleted;
    float top = y;
    y += COMMENT_PADDING * k;

    // Their icon, in a circle.
    float av = AVATAR * k;
    CCPoint avatarAt {m_pad + av / 2, -(y + av / 2)};
    auto tile = RoundedBox::create({av, av}, av / 2, m_scheme.background6());
    tile->setPosition(avatarAt);
    content->addChild(tile);
    if (auto s = comment->m_userScore) {
        auto icon = playerIcon(s->m_iconID, s->m_iconType, s->m_color1, s->m_color2, s->m_color3, s->m_glowEnabled, av * 0.62f);
        icon->setPosition(avatarAt);
        content->addChild(icon, 1);
    }

    // Author line (CommentAuthorLine): the name opens their profile, then
    // badges, and "spam" / "deleted" for hidden ones.
    float textX = m_pad + av + 10 * k;
    float textW = W - m_pad - textX;
    float lineCy = -(y + 8 * k);
    float x = textX;
    auto name = makeText(author, Weight::Bold, 14 * k);
    name->setAnchorPoint({0, 0.5f});
    name->setPosition({x, lineCy});
    float nameW = name->getScaledContentSize().width;
    if (nameW > textW * 0.6f) name->setScale(name->getScale() * textW * 0.6f / nameW);
    content->addChild(name, 1);
    if (comment->m_accountID > 0) {
        Pill link;
        link.node = name;
        link.tinted = {name};
        link.textHover = theme::rgb(m_scheme.light1());
        int accountID = comment->m_accountID;
        link.action = [accountID, me] { ProfilePage::create(accountID, accountID == me)->show(); };
        m_pills.push_back(std::move(link));
    }
    x += name->getScaledContentSize().width + 4 * k;
    if (comment->m_modBadge > 0 && comment->m_modBadge <= 3) {
        auto frame = fmt::format("modBadge_0{}_001.png", comment->m_modBadge);
        if (CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame.c_str())) {
            auto badge = CCSprite::createWithSpriteFrameName(frame.c_str());
            auto s = badge->getContentSize();
            badge->setScale(14 * k / std::max(1.f, std::max(s.width, s.height)));
            float bw = badge->getScaledContentSize().width;
            badge->setPosition({x + bw / 2, lineCy});
            content->addChild(badge, 1);
            x += bw + 4 * k;
        }
    }
    // OwnerTitleBadge: a small pill with tiny bold text.
    auto badge = [&](std::string const& text, ccColor4B fill, ccColor3B textColor) {
        auto label = makeText(text, Weight::Bold, 10 * k);
        label->setColor(textColor);
        float w = label->getScaledContentSize().width + 10 * k, h = 14 * k;
        auto box = RoundedBox::create({w, h}, h / 2, fill);
        box->setAnchorPoint({0, 0.5f});
        box->setPosition({x, lineCy});
        content->addChild(box, 1);
        label->setPosition({w / 2, h / 2});
        box->addChild(label);
        x += w + 4 * k;
    };
    // The level's creator (osu!'s "mapper"), and the percent GD attached.
    if (comment->m_accountID > 0 && comment->m_accountID == m_level->m_accountID.value()) {
        badge("creator", m_scheme.light1(), theme::rgb(m_scheme.background6()));
    }
    if (comment->m_percentage > 0) badge(fmt::format("{}%", comment->m_percentage), m_scheme.colour3(), {255, 255, 255});
    if (hidden) {
        auto label = makeText(comment->m_commentDeleted ? "deleted" : "spam", Weight::Bold, 14 * k);
        label->setColor(MUTED);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, lineCy});
        content->addChild(label, 1);
    }
    y += 20 * k;

    // The comment, in GD's colour for it (mods' are tinted).
    std::string text = comment->m_commentString;
    auto message = makeWrappedText(text.empty() ? " " : text, 14 * k, textW, hidden ? MUTED : comment->m_color);
    message->setPosition({textX, -y});
    content->addChild(message, 1);
    y += message->getContentSize().height + 4 * k;

    // When, in GD's words ("3 days").
    std::string when = comment->m_uploadDate;
    if (!when.empty()) {
        auto date = makeText(when + " ago", Weight::Regular, 12 * k);
        date->setColor(theme::rgb(m_scheme.foreground1()));
        date->setAnchorPoint({0, 0.5f});
        date->setPosition({textX, -(y + 7 * k)});
        content->addChild(date, 1);
    }
    y += 14 * k;
    y = std::max(y, top + COMMENT_PADDING * k + av);
    y += COMMENT_PADDING * k;

    // Votes, in the margin left of the icon (VotePill): "+N", green once you
    // liked it, and a thumbs down under it. Not on your own, and only once.
    {
        int id = comment->m_commentID;
        int voted = 0; // 1 liked, -1 disliked
        if (auto it = m_votes.find(id); it != m_votes.end()) voted = it->second ? 1 : -1;
        else if (glm->hasLikedItem(LikeItemType::Comment, id, true, m_levelID)) voted = 1;
        else if (glm->hasLikedItem(LikeItemType::Comment, id, false, m_levelID)) voted = -1;
        bool canVote = !own && voted == 0 && !hidden;
        Ref<GJComment> ref = comment;

        int count = comment->m_likeCount;
        auto countText = makeText(count < 0 ? fmt::format("{}", count) : fmt::format("+{}", count), Weight::Regular, 14 * k);
        float h = VOTE_HEIGHT * k, w = countText->getScaledContentSize().width + 20 * k;
        float right = m_pad - 5 * k;
        auto fill = voted == 1 ? GREEN_LIGHT : m_scheme.background6();
        if (own) fill.a = 0; // your own: just the number
        std::function<void()> like;
        if (canVote) like = [this, ref] { this->vote(ref.data(), true); };
        auto& up = addPill(m_pills, content, {w, h}, h / 2, {std::max(2 * k, right - w), avatarAt.y}, {0, 0.5f}, fill,
                           theme::lerp(fill, ccColor4B {255, 255, 255, 255}, 0.15f), std::move(like));
        countText->setPosition({w / 2, h / 2});
        up.node->addChild(countText, 1);

        if (!own) {
            auto downFill = voted == -1 ? DANGER : m_scheme.background6();
            std::function<void()> dislike;
            if (canVote) dislike = [this, ref] { this->vote(ref.data(), false); };
            auto& down = addPill(m_pills, content, {h, h}, h / 2, {right - h, avatarAt.y - h - 4 * k}, {0, 0.5f}, downFill,
                                 theme::lerp(downFill, ccColor4B {255, 255, 255, 255}, 0.15f), std::move(dislike));
            auto thumb = makeIcon(icon::THUMBS_DOWN, 10 * k);
            thumb->setPosition({h / 2, h / 2});
            down.node->addChild(thumb, 1);
        }
    }

    // A thin line under each comment.
    auto line = CCLayerColor::create(SEPARATOR);
    line->setContentSize({W, 1.5f * k});
    line->setPosition({0, -y});
    content->addChild(line);
    return y;
}

float CommentsOverlay::buildFooter(float y) {
    float k = m_k, W = bodySize().width;
    bool loading = m_state == State::Loading;
    bool failed = m_state == State::Failed;
    int loaded = static_cast<int>(m_comments->count());
    bool more = m_total < 0 || loaded < m_total;
    // Nothing more to load: osu! hides the button.
    if (!loading && !failed && !more) return y + 20 * k;

    // ShowMoreButton: a chevron each side of "SHOW MORE (N)"; only a spinner while loading.
    y += 10 * k;
    std::string text = failed ? "TRY AGAIN" : m_total >= 0 ? fmt::format("SHOW MORE ({})", m_total - loaded) : "SHOW MORE";
    auto label = makeText(text, Weight::Bold, 12 * k);
    auto left = makeIcon(icon::CHEVRON_DOWN, 7.5f * k);
    auto right = makeIcon(icon::CHEVRON_DOWN, 7.5f * k);
    float chevronW = left->getScaledContentSize().width;
    float w = std::max(120 * k, label->getScaledContentSize().width + (chevronW + 10 * k) * 2 + 40 * k);
    float h = 24 * k;
    std::function<void()> action;
    if (!loading) action = [this] { this->load(m_page); }; // after a failure m_page is still the page that failed
    auto& pill = addPill(m_pills, m_list, {w, h}, h / 2, {W / 2, -(y + h / 2)}, {0.5f, 0.5f}, m_scheme.background2(),
                         m_scheme.background1(), std::move(action));
    if (loading) {
        auto spinner = makeSpinner(12 * k, {255, 255, 255});
        spinner->setPosition({w / 2, h / 2});
        pill.node->addChild(spinner, 1);
        m_spinners.push_back(spinner);
    } else {
        label->setPosition({w / 2, h / 2});
        pill.node->addChild(label, 1);
        left->setPosition({20 * k + chevronW / 2, h / 2});
        right->setPosition({w - 20 * k - chevronW / 2, h / 2});
        pill.node->addChild(left, 1);
        pill.node->addChild(right, 1);
        pill.tinted = {left, right};
        pill.textColor = theme::rgb(m_scheme.foreground1());
        pill.textHover = theme::rgb(m_scheme.light1());
    }
    y += h + 10 * k;
    return y + 20 * k;
}

void CommentsOverlay::updateEditor() {
    if (m_fixedPills.size() <= std::max(m_postPill, m_signInPill)) return;
    bool in = loggedIn();
    std::string text = m_input ? trim(std::string(m_input->getString())) : "";
    auto& post = m_fixedPills[m_postPill];
    auto& signIn = m_fixedPills[m_signInPill];
    post.node->setVisible(in);
    signIn.node->setVisible(!in);
    post.enabled = in && !m_posting && !text.empty();
    if (m_postSpinner) m_postSpinner->setVisible(m_posting);
    if (m_input) {
        bool usable = in && !m_posting;
        if (!usable) m_input->defocus();
        m_input->setEnabled(usable);
        static std::string const PLACEHOLDERS[] {"type your comment here", "sign in to comment", "posting..."};
        auto const& placeholder = PLACEHOLDERS[!in ? 1 : m_posting ? 2 : 0];
        if (m_placeholder != placeholder) {
            m_placeholder = placeholder;
            m_input->setPlaceholder(placeholder);
        }
    }
    if (m_percentBox) {
        m_percentBox->setFillColor(m_includePercent ? m_scheme.colour3() : m_scheme.background5());
        m_percentCheck->setVisible(m_includePercent);
    }
}

// --- per frame and input ---

void CommentsOverlay::onUpdate(float dt) {
    float ms = dt * 1000.f;
    if (m_copiedMs > 0) {
        m_copiedMs -= ms;
        if (m_copiedMs <= 0 && m_idLabel) m_idLabel->setString(m_idText.c_str());
    }
    if (m_state == State::Loading) {
        m_loadingMs += ms;
        if (m_loadingMs > LOAD_TIMEOUT_MS) {
            m_key.clear();
            m_state = State::Failed;
            m_dirty = true;
        }
    }
    // Signing in (from the dialog) turns the box on.
    bool in = loggedIn();
    if (in != m_wasLoggedIn) {
        m_wasLoggedIn = in;
        updateEditor();
    }
    if (m_dirty) rebuild();

    for (auto spinner : m_spinners) spinner->setRotation(spinner->getRotation() + dt * SPIN_SPEED);
    if (m_postSpinner && m_postSpinner->isVisible()) m_postSpinner->setRotation(m_postSpinner->getRotation() + dt * SPIN_SPEED);

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !covered() && !popupOnTop() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    for (auto list : {&m_fixedPills, &m_pills}) {
        for (auto& p : *list) {
            bool hovered = interactive && p.action && p.enabled && nodeShown(p.node) && nodeContains(p.node, mouse);
            if (hovered && !p.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            p.hovered = hovered;
            if (p.tag >= TAG_TAB && p.tag < TAG_TAB + 2) {
                // TabButton.UpdateState
                bool active = p.tag - TAG_TAB == static_cast<int>(m_sort);
                p.bg->setFillColor(active || hovered ? p.hoverColor : p.color);
                auto colour = active && !hovered ? theme::rgb(m_scheme.light1()) : ccColor3B {255, 255, 255};
                p.tinted[0]->setVisible(!active);
                p.tinted[1]->setVisible(active);
                for (auto label : p.tinted) label->setColor(colour);
                continue;
            }
            if (p.bg) {
                auto colour = hovered ? p.hoverColor : p.color;
                if (!p.enabled) colour = theme::lerp(p.color, ccColor4B {40, 40, 40, 255}, 0.6f); // greyed, like ButtonRow
                p.bg->setFillColor(colour);
            }
            for (auto label : p.tinted) label->setColor(hovered ? p.textHover : p.textColor);
        }
    }
}

CommentsOverlay::Pill* CommentsOverlay::pillAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    Pill* hit = nullptr;
    for (auto list : {&m_fixedPills, &m_pills}) {
        for (auto& p : *list) {
            if (p.action && p.enabled && nodeShown(p.node) && nodeContains(p.node, world)) hit = &p;
        }
    }
    return hit;
}

bool CommentsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    auto loc = touch->getLocation();
    // The post box takes its own touches (its input node registers below us).
    if (isOpen() && !covered() && m_input && nodeShown(m_input) && m_scroll->containsWorldPoint(loc)
        && nodeContains(m_input, loc)) {
        return false;
    }
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    // A tap anywhere else leaves the box (it only ever sees its own touches).
    if (m_input) m_input->defocus();
    m_pressed = pillAt(loc);
    m_drag.began(m_scroll, loc);
    return true;
}

void CommentsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void CommentsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || !pressed->action || !pressed->enabled || !nodeContains(pressed->node, touch->getLocation())) return;
    if (!pressed->silent) sfx::click(sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the list, and the pill with it
    action();
}

void CommentsOverlay::textChanged(CCTextInputNode*) {
    updateEditor();
}

void CommentsOverlay::enterPressed(CCTextInputNode*) {
    post();
}

} // namespace lazer

// GD's comments page for a level (InfoLayer: the level page's info button,
// and anything else that opens one) shows as our page instead. GD's layer is
// already built by then: the page keeps it, hidden, and falls back to it if
// it can't open.
class $modify(LazerInfoLayer, InfoLayer) {
    void show() {
        // Ours asked for GD's own ("more info"), or one we don't cover.
        if (this->getUserObject("vanilla"_spr) || !lazer::CommentsOverlay::wants(this)) return InfoLayer::show();
        if (!lazer::CommentsOverlay::present(m_level, this)) InfoLayer::show();
    }
};
