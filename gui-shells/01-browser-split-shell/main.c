#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 01 — Browser Split Shell (Tabgruppen + Mixed Split View + B8G_DISPLAY_V1)
 * ============================================================================ */

#define BS_TAB_COUNT      6
#define BS_TOPBAR_H       48
#define BS_STATUSBAR_H    26
#define BS_SIDEBAR_W      234
#define BS_SPLITTER_W     8

typedef enum BsTabKind {
    BS_KIND_MEDIA = 0,
    BS_KIND_WEB   = 1,
    BS_KIND_CODE  = 2,
    BS_KIND_TERM  = 3,
    BS_KIND_DOCS  = 4
} BsTabKind;

typedef struct BsTab {
    const char* title;
    const char* group;
    const char* url;
    const char* badge;
    COLORREF    accent;
    BsTabKind   kind;
    uint32_t    surface_id;
    uint32_t    default_leases;
} BsTab;

typedef struct BrowserShellState {
    bool     initialized;
    bool     sidebar_open;
    bool     split_mode;
    int      left_tab;
    int      right_tab;
    int      focused_pane; /* 0 = left, 1 = right */
    float    split_ratio;
    bool     dragging_split;
    uint32_t tick_phase;
    BsTab    tabs[BS_TAB_COUNT];

    /* Cached hit-test rectangles */
    SkRect   btn_sidebar;
    SkRect   btn_split;
    SkRect   btn_freeze;
    SkRect   btn_audio;
    SkRect   tab_rects[BS_TAB_COUNT];
    SkRect   splitter_rect;
    SkRect   left_pane_rect;
    SkRect   right_pane_rect;
} BrowserShellState;

static B8GSurface* bs_find_surface(B8GDisplayCtx* ctx, uint32_t surface_id) {
    for (uint32_t i = 0; i < B8G_DISPLAY_MAX_SURFACES; i++) {
        if (ctx->surfaces[i].active && ctx->surfaces[i].surface_id == surface_id) {
            return &ctx->surfaces[i];
        }
    }
    return NULL;
}

static void bs_ensure_init(SkApp* app, BrowserShellState* st) {
    if (st->initialized) return;
    st->initialized = true;
    st->sidebar_open = true;
    st->split_mode = true;
    st->left_tab = 1;       /* GitHub / awesome-os */
    st->right_tab = 2;      /* VS Code: sectium_display.h */
    st->focused_pane = 0;
    st->split_ratio = 0.52f;
    st->dragging_split = false;

    const BsTab defaults[BS_TAB_COUNT] = {
        { "Twitch Live [AUDIO]",        "MEDIA",     "https://twitch.tv/awesome_os_dev",               "LIVE",   SK_COL_PURPLE, BS_KIND_MEDIA, 0, B8G_LEASE_AUDIO },
        { "GitHub / awesome-os",        "RESEARCH",  "https://github.com/awesome-os/sectium",          "REPO",   SK_COL_ACCENT, BS_KIND_WEB,   0, B8G_LEASE_NONE },
        { "VS Code: sectium_display.h", "WORKSPACE", "sectium://workspace/sections/sectium_display.h", "C11",    SK_COL_CYAN,   BS_KIND_CODE,  0, B8G_LEASE_NONE },
        { "Terminal: holo-compiler",    "WORKSPACE", "b8g://ttyS0/holo-compiler --cas .b8p",           "TTY0",   SK_COL_GREEN,  BS_KIND_TERM,  0, B8G_LEASE_COMPUTE },
        { "X / Timeline",               "SOCIAL",    "https://x.com/home",                             "FEED",   SK_COL_AMBER,  BS_KIND_WEB,   0, B8G_LEASE_NONE },
        { "Docs: B8G_DISPLAY_V1",       "RESEARCH",  "https://docs.awesome-os.com/b8g-display-v1",     "SPEC",   SK_COL_ACCENT, BS_KIND_DOCS,  0, B8G_LEASE_NONE }
    };

    for (int i = 0; i < BS_TAB_COUNT; i++) {
        st->tabs[i] = defaults[i];
        uint32_t sid = 0;
        sectium_surface_create(
            &app->display,
            app->human_actor_id,
            960u,
            640u,
            B8G_COMP_TILING_2D,
            &sid
        );
        st->tabs[i].surface_id = sid;
        if (st->tabs[i].default_leases != B8G_LEASE_NONE) {
            sectium_surface_set_leases(&app->display, sid, st->tabs[i].default_leases);
        }
        uint32_t bind_id = 0;
        sectium_actor_bind_stream(&app->display, app->human_actor_id, sid, B8G_STREAM_SURFACE, &bind_id);
        uint64_t seq = 0;
        sectium_surface_commit(&app->display, sid, app->now_ms, &seq);
    }
}

static int bs_active_tab_index(const BrowserShellState* st) {
    if (!st->split_mode) return st->left_tab;
    return st->focused_pane == 0 ? st->left_tab : st->right_tab;
}

static void bs_activate_tab(SkApp* app, BrowserShellState* st, int idx) {
    if (idx < 0 || idx >= BS_TAB_COUNT) return;
    if (!st->split_mode || st->focused_pane == 0) {
        st->left_tab = idx;
    } else {
        st->right_tab = idx;
    }
    uint32_t sid = st->tabs[idx].surface_id;
    sectium_surface_thaw(&app->display, sid, app->now_ms);
    uint64_t seq = 0;
    sectium_surface_commit(&app->display, sid, app->now_ms, &seq);
}

static void bs_freeze_idle_tabs(SkApp* app, BrowserShellState* st) {
    int active = bs_active_tab_index(st);
    uint64_t future_ms = app->now_ms + 120000ULL;
    for (int i = 0; i < BS_TAB_COUNT; i++) {
        if (i == active) continue;
        uint32_t sid = st->tabs[i].surface_id;
        uint32_t state = 0;
        /* Step 1: ACTIVE -> FROZEN (or DISPLAY_SLEEP if wake lease active) */
        sectium_surface_evaluate_idle(&app->display, sid, future_ms, 10000ULL, 30000ULL, &state);
        /* Step 2: FROZEN -> CHECKPOINTED for unleased tabs; leased tabs remain DISPLAY_SLEEP */
        sectium_surface_evaluate_idle(&app->display, sid, future_ms + 30000ULL, 10000ULL, 30000ULL, &state);
    }
}

static void bs_toggle_audio_lease(SkApp* app, BrowserShellState* st) {
    /* Toggle LEASE_AUDIO on currently active tab (or tab 0 if desired) */
    int idx = bs_active_tab_index(st);
    B8GSurface* s = bs_find_surface(&app->display, st->tabs[idx].surface_id);
    if (!s) return;
    uint32_t next_mask = (s->wake_leases & B8G_LEASE_AUDIO)
        ? (s->wake_leases & ~B8G_LEASE_AUDIO)
        : (s->wake_leases | B8G_LEASE_AUDIO);
    sectium_surface_set_leases(&app->display, s->surface_id, next_mask);
}

static void bs_draw_pane_content(SkApp* app, BrowserShellState* st, HDC hdc, SkRect area, int tab_idx, bool focused) {
    BsTab* tab = &st->tabs[tab_idx];
    B8GSurface* surf = bs_find_surface(&app->display, tab->surface_id);
    uint32_t state = surf ? surf->state : B8G_SURFACE_ACTIVE;
    uint32_t leases = surf ? surf->wake_leases : B8G_LEASE_NONE;

    sk_fill_rect(hdc, area, SK_COL_SURFACE);
    sk_stroke_rect(hdc, area, focused ? SK_COL_ACCENT : SK_COL_BORDER, focused ? 2 : 1);

    /* Pane header */
    SkRect hdr = sk_rect(area.x + 1, area.y + 1, area.w - 2, 34);
    sk_fill_rect(hdc, hdr, focused ? SK_COL_SURFACE2 : SK_COL_SURFACE);
    sk_line(hdc, area.x, area.y + 35, area.x + area.w, area.y + 35, SK_COL_BORDER, 1);

    sk_text(hdc, app->font_bold, sk_rect(hdr.x + 12, hdr.y, hdr.w - 220, hdr.h),
            tab->title, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* State pill */
    COLORREF st_col = sk_surface_state_color(state);
    SkRect pill = sk_rect(hdr.x + hdr.w - 118, hdr.y + 6, 108, 22);
    sk_pill(hdc, app->font_small, pill, sk_surface_state_name(state), SK_COL_BG, st_col, st_col);

    if (leases & B8G_LEASE_AUDIO) {
        SkRect ap = sk_rect(pill.x - 76, hdr.y + 6, 70, 22);
        sk_pill(hdc, app->font_small, ap, "AUDIO", SK_COL_SURFACE3, SK_COL_CYAN, SK_COL_CYAN);
    }

    SkRect body = sk_rect(area.x + 14, area.y + 46, area.w - 28, area.h - 58);
    if (body.w < 40 || body.h < 40) return;

    if (state == B8G_SURFACE_CHECKPOINTED || state == B8G_SURFACE_FROZEN) {
        sk_round_rect(hdc, body, 8, SK_COL_BG, SK_COL_PURPLE);
        sk_text(hdc, app->font_title, sk_rect(body.x + 20, body.y + body.h / 2 - 34, body.w - 40, 28),
                state == B8G_SURFACE_CHECKPOINTED ? "SURFACE CHECKPOINTED (.b8p)" : "SURFACE FROZEN",
                SK_COL_PURPLE, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        char info[128];
        snprintf(info, sizeof(info), "Zero CPU/GPU wakeups | epoch=%llu | Click tab or pane to thaw",
                 (unsigned long long)(surf ? surf->checkpoint_epoch : 0));
        sk_text(hdc, app->font_ui, sk_rect(body.x + 20, body.y + body.h / 2, body.w - 40, 24),
                info, SK_COL_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (tab->kind == BS_KIND_MEDIA) {
        SkRect vid = sk_rect(body.x, body.y, body.w, body.h * 55 / 100);
        sk_round_rect(hdc, vid, 8, SK_COL_BG, state == B8G_SURFACE_DISPLAY_SLEEP ? SK_COL_CYAN : SK_COL_PURPLE);
        sk_pill(hdc, app->font_small, sk_rect(vid.x + 12, vid.y + 12, 58, 20), "LIVE", SK_COL_RED, SK_COL_TEXT, SK_COL_RED);
        sk_text(hdc, app->font_title, sk_rect(vid.x + 16, vid.y + 44, vid.w - 32, 28),
                state == B8G_SURFACE_DISPLAY_SLEEP
                    ? "DISPLAY_SLEEP — Audio Stream Active (0 GPU Frames)"
                    : "Awesome-OS Kernel Hacking — Live Stream",
                state == B8G_SURFACE_DISPLAY_SLEEP ? SK_COL_CYAN : SK_COL_TEXT,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Animated audio spectrum bars */
        int bar_count = 24;
        int bar_area_w = vid.w - 32;
        int bar_w = bar_area_w / bar_count;
        if (bar_w < 4) bar_w = 4;
        for (int b = 0; b < bar_count && (vid.x + 16 + b * bar_w) < (vid.x + vid.w - 16); b++) {
            int wave = (int)((st->tick_phase * 3u + (uint32_t)b * 7u) % 29u);
            int bh = 12 + (wave > 14 ? (29 - wave) * 4 : wave * 4);
            if (bh > vid.h - 90) bh = vid.h - 90;
            if (bh < 8) bh = 8;
            SkRect br = sk_rect(vid.x + 16 + b * bar_w, vid.y + vid.h - 14 - bh, bar_w - 3, bh);
            sk_fill_rect(hdc, br, (b % 3 == 0) ? SK_COL_CYAN : SK_COL_PURPLE);
        }

        /* Chat stream below */
        int cy = vid.y + vid.h + 12;
        sk_text(hdc, app->font_bold, sk_rect(body.x, cy, body.w, 20), "Stream Chat (B8G_LEASE_AUDIO)", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
        const char* chat[] = {
            "lumi_dev: DISPLAY_SLEEP keeps audio alive with 0% GPU!",
            "zig_fan: 5 KB control plane compiling in 320ms",
            "sectium_bot: B8G_ARENA_V1 64-byte aligned zero-copy active"
        };
        for (int i = 0; i < 3; i++) {
            sk_text(hdc, app->font_mono, sk_rect(body.x + 4, cy + 24 + i * 22, body.w - 8, 20),
                    chat[i], i == 0 ? SK_COL_CYAN : SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
        }
    } else if (tab->kind == BS_KIND_CODE) {
        const char* lines[] = {
            "/* sections/sectium-display/sectium_display.h */",
            "#define B8G_DISPLAY_MAGIC_V1    0x31444742u",
            "#define B8G_DISPLAY_ARENA_ALIGN 64u",
            "",
            "typedef enum B8GWakeLease {",
            "    B8G_LEASE_NONE     = 0u,",
            "    B8G_LEASE_AUDIO    = (1u << 0),",
            "    B8G_LEASE_NET_PUSH = (1u << 1),",
            "    B8G_LEASE_COMPUTE  = (1u << 2)",
            "} B8GWakeLease;",
            "",
            "int32_t sectium_surface_evaluate_idle(",
            "    B8GDisplayCtx* ctx, uint32_t surface_id,",
            "    uint64_t now_ms, uint64_t idle_ms, uint64_t freeze_ms,",
            "    uint32_t* out_state);"
        };
        int nlines = (int)(sizeof(lines) / sizeof(lines[0]));
        sk_round_rect(hdc, body, 6, SK_COL_BG, SK_COL_BORDER);
        for (int i = 0; i < nlines && (body.y + 12 + i * 21) < (body.y + body.h - 14); i++) {
            char ln[12];
            snprintf(ln, sizeof(ln), "%2d", i + 1);
            sk_text(hdc, app->font_mono, sk_rect(body.x + 8, body.y + 10 + i * 21, 28, 20),
                    ln, SK_COL_MUTED, DT_RIGHT | DT_SINGLELINE);
            COLORREF c = SK_COL_TEXT;
            if (lines[i][0] == '/') c = SK_COL_MUTED;
            else if (lines[i][0] == '#') c = SK_COL_AMBER;
            else if (strstr(lines[i], "typedef") || strstr(lines[i], "int32_t")) c = SK_COL_PURPLE;
            else if (strstr(lines[i], "B8G_LEASE")) c = SK_COL_CYAN;
            sk_text(hdc, app->font_mono, sk_rect(body.x + 44, body.y + 10 + i * 21, body.w - 52, 20),
                    lines[i], c, DT_LEFT | DT_SINGLELINE);
        }
    } else if (tab->kind == BS_KIND_TERM) {
        const char* out[] = {
            "PS E:\\awesome-os> .\\bin\\zig.cmd cc -target x86_64-windows-gnu -O2 ...",
            "[holo-compiler] .b8p CAS hit: sectium_display.o (0.4ms)",
            "[holo-compiler] linked gui-shells/01-browser-split-shell.exe",
            "[b8g-display]   B8G_DISPLAY_MAGIC_V1 = 0x31444742 (OK)",
            "[b8g-display]   actors=1 surfaces=7 bindings=7",
            "[sectium-cp]    5,080 B initramfs ready | WoL warm-RAM enabled",
            "PS E:\\awesome-os> _"
        };
        sk_round_rect(hdc, body, 6, SK_COL_BG, SK_COL_GREEN);
        for (int i = 0; i < 7 && (body.y + 12 + i * 22) < (body.y + body.h - 14); i++) {
            COLORREF c = (i == 0 || i == 6) ? SK_COL_GREEN : (i == 3 ? SK_COL_CYAN : SK_COL_TEXT);
            sk_text(hdc, app->font_mono, sk_rect(body.x + 12, body.y + 12 + i * 22, body.w - 24, 20),
                    out[i], c, DT_LEFT | DT_SINGLELINE);
        }
    } else if (tab->kind == BS_KIND_DOCS) {
        sk_text(hdc, app->font_title, sk_rect(body.x, body.y, body.w, 28),
                "B8G_DISPLAY_V1 Protocol Reference", SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);
        const char* rows[] = {
            "ACTIVE         -> Live interactive surface receiving damage/commits",
            "IDLE           -> Past idle_threshold_ms; awaiting freeze evaluation",
            "DISPLAY_SLEEP  -> Wake lease held (AUDIO/NET/COMPUTE); 0 GPU frames",
            "FROZEN         -> Unleased surface halted; ready for checkpoint",
            "CHECKPOINTED   -> Flushed to 64B-aligned .b8p warm-RAM arena"
        };
        for (int i = 0; i < 5; i++) {
            SkRect row = sk_rect(body.x, body.y + 40 + i * 38, body.w, 32);
            sk_round_rect(hdc, row, 5, SK_COL_SURFACE2, SK_COL_BORDER);
            sk_text(hdc, app->font_mono, sk_rect(row.x + 10, row.y, row.w - 20, row.h),
                    rows[i], i == 2 ? SK_COL_CYAN : SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        /* Web / GitHub / X card feed */
        sk_text(hdc, app->font_title, sk_rect(body.x, body.y, body.w, 28),
                tab->title, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
        sk_text(hdc, app->font_mono, sk_rect(body.x, body.y + 28, body.w, 20),
                tab->url, SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

        const char* cards[3][2] = {
            { "feat(sectium-display): zero-copy B8G_ARENA_V1 surface lifecycle", "Merged 12m ago  |  +467 -0  |  C11 ABI 0x31444742" },
            { "perf(holo-compiler): sub-second native x86_64-windows-gnu builds", "Verified  |  0 warnings (-Wall -Wextra -Werror)" },
            { "docs(gui-shells): Tabgruppen + mixed split-view shell boilerplate", "Active surface  |  Click [Freeze Idle] to test checkpointing" }
        };
        for (int i = 0; i < 3; i++) {
            SkRect card = sk_rect(body.x, body.y + 60 + i * 74, body.w, 64);
            if (card.y + card.h > body.y + body.h) break;
            sk_round_rect(hdc, card, 8, SK_COL_SURFACE2, SK_COL_BORDER);
            sk_text(hdc, app->font_bold, sk_rect(card.x + 14, card.y + 10, card.w - 28, 22),
                    cards[i][0], SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
            sk_text(hdc, app->font_small, sk_rect(card.x + 14, card.y + 36, card.w - 28, 20),
                    cards[i][1], SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
        }
    }
}

static void bs_render(SkApp* app, HDC hdc) {
    BrowserShellState* st = (BrowserShellState*)app->user;
    bs_ensure_init(app, st);

    int w = app->width;
    int h = app->height;

    /* 1. Top address & action bar */
    SkRect top = sk_rect(0, 0, w, BS_TOPBAR_H);
    sk_fill_rect(hdc, top, SK_COL_SURFACE);
    sk_line(hdc, 0, BS_TOPBAR_H - 1, w, BS_TOPBAR_H - 1, SK_COL_BORDER, 1);

    int bx = 10;
    st->btn_sidebar = sk_rect(bx, 9, 118, 30); bx += 126;
    st->btn_split   = sk_rect(bx, 9, 112, 30); bx += 120;
    st->btn_freeze  = sk_rect(bx, 9, 102, 30); bx += 110;
    st->btn_audio   = sk_rect(bx, 9, 106, 30); bx += 116;

    int active_idx = bs_active_tab_index(st);
    B8GSurface* active_surf = bs_find_surface(&app->display, st->tabs[active_idx].surface_id);
    bool has_audio = active_surf && ((active_surf->wake_leases & B8G_LEASE_AUDIO) != 0);

    sk_button(hdc, app, st->btn_sidebar, st->sidebar_open ? "Hide Sidebar" : "Show Sidebar", st->sidebar_open, SK_COL_ACCENT);
    sk_button(hdc, app, st->btn_split,   st->split_mode ? "Split View" : "Single View", st->split_mode, SK_COL_CYAN);
    sk_button(hdc, app, st->btn_freeze,  "Freeze Idle", false, SK_COL_PURPLE);
    sk_button(hdc, app, st->btn_audio,   has_audio ? "Audio: ON" : "Audio Lease", has_audio, SK_COL_GREEN);

    /* Address bar */
    int addr_w = w - bx - 12;
    if (addr_w > 120) {
        SkRect addr = sk_rect(bx, 9, addr_w, 30);
        sk_round_rect(hdc, addr, 6, SK_COL_BG, SK_COL_BORDER);
        sk_text(hdc, app->font_mono, sk_rect(addr.x + 12, addr.y, addr.w - 24, addr.h),
                st->tabs[active_idx].url, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    int content_y = BS_TOPBAR_H;
    int content_h = h - BS_TOPBAR_H - BS_STATUSBAR_H;
    int main_x = 0;

    /* 2. Collapsible Tabgruppen sidebar */
    if (st->sidebar_open) {
        SkRect sb = sk_rect(0, content_y, BS_SIDEBAR_W, content_h);
        sk_fill_rect(hdc, sb, SK_COL_SURFACE);
        sk_line(hdc, BS_SIDEBAR_W - 1, content_y, BS_SIDEBAR_W - 1, content_y + content_h, SK_COL_BORDER, 1);

        sk_text(hdc, app->font_bold, sk_rect(14, content_y + 10, BS_SIDEBAR_W - 28, 22),
                "TABGRUPPEN", SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int ty = content_y + 38;
        const char* prev_group = "";
        for (int i = 0; i < BS_TAB_COUNT; i++) {
            if (strcmp(prev_group, st->tabs[i].group) != 0) {
                prev_group = st->tabs[i].group;
                sk_text(hdc, app->font_small, sk_rect(14, ty, BS_SIDEBAR_W - 28, 18),
                        prev_group, SK_COL_ACCENT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                ty += 20;
            }
            SkRect tr = sk_rect(8, ty, BS_SIDEBAR_W - 16, 46);
            st->tab_rects[i] = tr;

            bool is_left = (i == st->left_tab);
            bool is_right = (st->split_mode && i == st->right_tab);
            bool hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, tr);
            COLORREF bg = (is_left || is_right) ? SK_COL_SURFACE3 : (hover ? SK_COL_SURFACE2 : SK_COL_SURFACE);
            COLORREF bd = (i == active_idx) ? SK_COL_ACCENT : ((is_left || is_right) ? SK_COL_BORDER : SK_COL_SURFACE);
            sk_round_rect(hdc, tr, 6, bg, bd);

            B8GSurface* s = bs_find_surface(&app->display, st->tabs[i].surface_id);
            uint32_t state = s ? s->state : B8G_SURFACE_ACTIVE;
            COLORREF dot = sk_surface_state_color(state);
            sk_circle(hdc, tr.x + 12, tr.y + 15, 4, dot, dot);

            sk_text(hdc, (i == active_idx) ? app->font_bold : app->font_ui,
                    sk_rect(tr.x + 22, tr.y + 4, tr.w - 28, 20),
                    st->tabs[i].title, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            char sub[64];
            const char* pane_tag = is_left && is_right ? "[L+R]" : (is_left ? "[L]" : (is_right ? "[R]" : ""));
            snprintf(sub, sizeof(sub), "%s %s", sk_surface_state_name(state), pane_tag);
            sk_text(hdc, app->font_small, sk_rect(tr.x + 22, tr.y + 24, tr.w - 28, 16),
                    sub, dot, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            ty += 52;
        }
        main_x = BS_SIDEBAR_W;
    } else {
        for (int i = 0; i < BS_TAB_COUNT; i++) {
            st->tab_rects[i] = sk_rect(0, 0, 0, 0);
        }
    }

    /* 3. Main split / single viewport */
    int main_w = w - main_x;
    int pad = 8;
    if (st->split_mode && main_w > 260) {
        int usable_w = main_w - pad * 2 - BS_SPLITTER_W;
        int left_w = (int)((float)usable_w * st->split_ratio);
        if (left_w < 160) left_w = 160;
        if (left_w > usable_w - 160) left_w = usable_w - 160;
        int right_w = usable_w - left_w;

        st->left_pane_rect = sk_rect(main_x + pad, content_y + pad, left_w, content_h - pad * 2);
        st->splitter_rect  = sk_rect(st->left_pane_rect.x + left_w, content_y + pad, BS_SPLITTER_W, content_h - pad * 2);
        st->right_pane_rect = sk_rect(st->splitter_rect.x + BS_SPLITTER_W, content_y + pad, right_w, content_h - pad * 2);

        bs_draw_pane_content(app, st, hdc, st->left_pane_rect, st->left_tab, st->focused_pane == 0);
        bs_draw_pane_content(app, st, hdc, st->right_pane_rect, st->right_tab, st->focused_pane == 1);

        /* Draggable vertical splitter */
        bool sp_hover = st->dragging_split || sk_pt_in_rect(app->mouse_x, app->mouse_y, st->splitter_rect);
        sk_fill_rect(hdc, st->splitter_rect, sp_hover ? SK_COL_ACCENT_DIM : SK_COL_BG);
        int cx = st->splitter_rect.x + BS_SPLITTER_W / 2;
        int cy = st->splitter_rect.y + st->splitter_rect.h / 2;
        sk_line(hdc, cx, cy - 28, cx, cy + 28, sp_hover ? SK_COL_ACCENT : SK_COL_BORDER, 2);
    } else {
        st->left_pane_rect = sk_rect(main_x + pad, content_y + pad, main_w - pad * 2, content_h - pad * 2);
        st->splitter_rect = sk_rect(0, 0, 0, 0);
        st->right_pane_rect = sk_rect(0, 0, 0, 0);
        bs_draw_pane_content(app, st, hdc, st->left_pane_rect, st->left_tab, true);
    }

    /* 4. Status bar with live B8GDisplayStats */
    B8GDisplayStats stats;
    sectium_display_get_stats(&app->display, &stats);
    SkRect sb_bar = sk_rect(0, h - BS_STATUSBAR_H, w, BS_STATUSBAR_H);
    sk_fill_rect(hdc, sb_bar, SK_COL_SURFACE2);
    sk_line(hdc, 0, sb_bar.y, w, sb_bar.y, SK_COL_BORDER, 1);

    char stat_buf[256];
    snprintf(stat_buf, sizeof(stat_buf),
             "B8G_DISPLAY_V1  |  Surfaces: %u  |  Active: %u  |  DisplaySleep: %u  |  Checkpointed: %u  |  Commits: %llu  |  Drag splitter to resize",
             stats.active_surfaces, stats.surfaces_active, stats.surfaces_display_sleep,
             stats.surfaces_checkpointed, (unsigned long long)stats.total_commits);
    sk_text(hdc, app->font_small, sk_rect(12, sb_bar.y, w - 24, BS_STATUSBAR_H),
            stat_buf, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void bs_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    BrowserShellState* st = (BrowserShellState*)app->user;
    bs_ensure_init(app, st);

    if (msg == WM_LBUTTONDOWN) {
        if (sk_pt_in_rect(x, y, st->btn_sidebar)) {
            st->sidebar_open = !st->sidebar_open;
            return;
        }
        if (sk_pt_in_rect(x, y, st->btn_split)) {
            st->split_mode = !st->split_mode;
            if (!st->split_mode) st->focused_pane = 0;
            return;
        }
        if (sk_pt_in_rect(x, y, st->btn_freeze)) {
            bs_freeze_idle_tabs(app, st);
            return;
        }
        if (sk_pt_in_rect(x, y, st->btn_audio)) {
            bs_toggle_audio_lease(app, st);
            return;
        }
        if (st->split_mode && sk_pt_in_rect(x, y, st->splitter_rect)) {
            st->dragging_split = true;
            return;
        }
        for (int i = 0; i < BS_TAB_COUNT; i++) {
            if (sk_pt_in_rect(x, y, st->tab_rects[i])) {
                bs_activate_tab(app, st, i);
                return;
            }
        }
        if (sk_pt_in_rect(x, y, st->left_pane_rect)) {
            st->focused_pane = 0;
            bs_activate_tab(app, st, st->left_tab);
            return;
        }
        if (st->split_mode && sk_pt_in_rect(x, y, st->right_pane_rect)) {
            st->focused_pane = 1;
            bs_activate_tab(app, st, st->right_tab);
            return;
        }
    } else if (msg == WM_MOUSEMOVE) {
        if (st->dragging_split && st->split_mode) {
            int main_x = st->sidebar_open ? BS_SIDEBAR_W : 0;
            int usable_w = app->width - main_x - 16 - BS_SPLITTER_W;
            if (usable_w > 100) {
                float r = (float)(x - (main_x + 8)) / (float)usable_w;
                if (r < 0.22f) r = 0.22f;
                if (r > 0.78f) r = 0.78f;
                st->split_ratio = r;
            }
        }
    } else if (msg == WM_LBUTTONUP) {
        st->dragging_split = false;
    }
}

static void bs_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_KEYDOWN) return;
    BrowserShellState* st = (BrowserShellState*)app->user;
    bs_ensure_init(app, st);

    if (wp == 'B') {
        st->sidebar_open = !st->sidebar_open;
    } else if (wp == 'S') {
        st->split_mode = !st->split_mode;
    } else if (wp == 'F') {
        bs_freeze_idle_tabs(app, st);
    } else if (wp >= '1' && wp <= '6') {
        bs_activate_tab(app, st, (int)(wp - '1'));
    }
}

static void bs_tick(SkApp* app, uint64_t now_ms) {
    (void)now_ms;
    BrowserShellState* st = (BrowserShellState*)app->user;
    bs_ensure_init(app, st);
    st->tick_phase++;
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show) {
    (void)hinst;
    (void)hprev;
    (void)cmdline;
    (void)show;

    static BrowserShellState state;
    memset(&state, 0, sizeof(state));

    static SkApp app;
    memset(&app, 0, sizeof(app));
    app.on_render = bs_render;
    app.on_mouse  = bs_mouse;
    app.on_key    = bs_key;
    app.on_tick   = bs_tick;
    app.user      = &state;

    return sk_run(
        &app,
        "SectiumBrowserSplitShell",
        "Sectium Shell 01 — Browser Split & Tabgruppen (B8G_DISPLAY_V1)",
        1360,
        820,
        false,
        false,
        120
    );
}
