#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 05-multi-agent-world-shell
 * Interactive Human + 3 Swarm Agent Open World with live 2D Split Dock
 * ============================================================================ */

#define WORLD_WIN_W 1240
#define WORLD_WIN_H 760
#define NUM_AGENTS 3
#define MAX_CHAT_INPUT 128
#define MAX_DIALOG_LINES 8
#define MAX_TERM_LINES 7

typedef struct AgentEntity {
    const char* name;
    const char* role;
    COLORREF    accent;
    float       desk_x;
    float       desk_y;
    uint32_t    actor_id;
    uint32_t    surface_id;
    uint32_t    bind_avatar;
    uint32_t    bind_surface;
    uint32_t    bind_dialog;
    char        bubble[128];
    char        term_lines[MAX_TERM_LINES][96];
    int         term_count;
    int         reply_counter;
} AgentEntity;

typedef struct DialogEntry {
    char     sender[32];
    char     text[128];
    COLORREF col;
} DialogEntry;

typedef struct WorldState {
    bool        initialized;
    uint32_t    frank_actor_id;
    float       human_x;
    float       human_y;
    float       target_x;
    float       target_y;
    bool        has_target;
    int         nearest_agent;
    int         selected_agent;

    AgentEntity agents[NUM_AGENTS];

    bool        chat_focused;
    char        chat_buf[MAX_CHAT_INPUT];
    int         chat_len;
    DialogEntry dialog[MAX_DIALOG_LINES];
    int         dialog_count;

    uint32_t    tick_counter;
} WorldState;

static void push_term_line(AgentEntity* ag, const char* line) {
    if (ag->term_count < MAX_TERM_LINES) {
        snprintf(ag->term_lines[ag->term_count++], 96, "%s", line);
    } else {
        for (int i = 1; i < MAX_TERM_LINES; ++i) {
            memcpy(ag->term_lines[i - 1], ag->term_lines[i], 96);
        }
        snprintf(ag->term_lines[MAX_TERM_LINES - 1], 96, "%s", line);
    }
}

static void push_dialog(WorldState* st, const char* sender, const char* text, COLORREF col) {
    if (st->dialog_count < MAX_DIALOG_LINES) {
        snprintf(st->dialog[st->dialog_count].sender, 32, "%s", sender);
        snprintf(st->dialog[st->dialog_count].text, 128, "%s", text);
        st->dialog[st->dialog_count].col = col;
        st->dialog_count++;
    } else {
        for (int i = 1; i < MAX_DIALOG_LINES; ++i) {
            st->dialog[i - 1] = st->dialog[i];
        }
        snprintf(st->dialog[MAX_DIALOG_LINES - 1].sender, 32, "%s", sender);
        snprintf(st->dialog[MAX_DIALOG_LINES - 1].text, 128, "%s", text);
        st->dialog[MAX_DIALOG_LINES - 1].col = col;
    }
}

static const B8GSurface* find_surface(const B8GDisplayCtx* ctx, uint32_t surface_id) {
    for (uint32_t i = 0; i < B8G_DISPLAY_MAX_SURFACES; ++i) {
        if (ctx->surfaces[i].active && ctx->surfaces[i].surface_id == surface_id) {
            return &ctx->surfaces[i];
        }
    }
    return NULL;
}

static void update_nearest_agent(WorldState* st) {
    float best_d2 = 1e12f;
    int best = 0;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        float dx = st->human_x - st->agents[i].desk_x;
        float dy = st->human_y - st->agents[i].desk_y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    st->nearest_agent = best;
}

static void ensure_init(SkApp* app) {
    WorldState* st = (WorldState*)app->user;
    if (st->initialized) return;
    st->initialized = true;

    /* Register Human Actor "You (Frank)" */
    sectium_actor_register(&app->display, B8G_ACTOR_HUMAN, "You (Frank)", &st->frank_actor_id);

    st->human_x = 0.50f;
    st->human_y = 0.74f;
    st->target_x = st->human_x;
    st->target_y = st->human_y;
    st->has_target = false;
    st->selected_agent = 0;

    const char* names[NUM_AGENTS] = { "display-abi", "wm-engine", "holo-compiler" };
    const char* roles[NUM_AGENTS] = { "B8G_DISPLAY_V1 ABI", "Split Compositor WM", "Zig C11 .b8p CAS" };
    const char* bubbles[NUM_AGENTS] = {
        "Built 4.3 KB WASI ABI",
        "Hot-reloaded split WM",
        "0 warnings in 120ms"
    };
    COLORREF cols[NUM_AGENTS] = { SK_COL_CYAN, SK_COL_PURPLE, SK_COL_GREEN };
    float xs[NUM_AGENTS] = { 0.22f, 0.50f, 0.78f };
    float ys[NUM_AGENTS] = { 0.30f, 0.24f, 0.30f };

    for (int i = 0; i < NUM_AGENTS; ++i) {
        AgentEntity* ag = &st->agents[i];
        ag->name = names[i];
        ag->role = roles[i];
        ag->accent = cols[i];
        ag->desk_x = xs[i];
        ag->desk_y = ys[i];
        snprintf(ag->bubble, sizeof(ag->bubble), "%s", bubbles[i]);

        sectium_actor_register(&app->display, B8G_ACTOR_AGENT, ag->name, &ag->actor_id);
        sectium_surface_create(
            &app->display,
            ag->actor_id,
            1024,
            640,
            B8G_COMP_OPEN_WORLD_3D,
            &ag->surface_id
        );
        sectium_surface_attach_arena(
            &app->display,
            ag->surface_id,
            (uint64_t)(i + 1) * 0x00400000u,
            1024u * 640u * 4u,
            1024u * 4u
        );

        /* Bind AVATAR + SURFACE + DIALOG streams */
        sectium_actor_bind_stream(&app->display, ag->actor_id, ag->surface_id, B8G_STREAM_AVATAR, &ag->bind_avatar);
        sectium_actor_bind_stream(&app->display, ag->actor_id, ag->surface_id, B8G_STREAM_SURFACE, &ag->bind_surface);
        sectium_actor_bind_stream(&app->display, ag->actor_id, ag->surface_id, B8G_STREAM_DIALOG, &ag->bind_dialog);

        uint64_t seq = 0;
        sectium_surface_commit(&app->display, ag->surface_id, app->now_ms, &seq);
    }

    push_term_line(&st->agents[0], "$ zig cc -target wasm32-wasi sectium_display.c");
    push_term_line(&st->agents[0], "[ok] libsectium-display.wasm -> 4,312 bytes");
    push_term_line(&st->agents[0], "[abi] BGD1 magic=0x31444742 align=64B");
    push_term_line(&st->agents[0], "[stream] AVATAR + SURFACE + DIALOG bound");

    push_term_line(&st->agents[1], "$ sectium-wm --hot-reload --tiling=2d");
    push_term_line(&st->agents[1], "[wm] Split compositor active (0 dropped frames)");
    push_term_line(&st->agents[1], "[rebind] Ready for 3D Desk <-> 2D Split");
    push_term_line(&st->agents[1], "[ipc] \\\\.\\pipe\\sectium-dev connected");

    push_term_line(&st->agents[2], "$ holo-compiler build gui-shells/*.c -Werror");
    push_term_line(&st->agents[2], "[cas] .b8p cache hit ratio: 94.2%");
    push_term_line(&st->agents[2], "[cc] 0 warnings in 120ms (native win64)");
    push_term_line(&st->agents[2], "[ready] Awaiting next incremental unit");

    push_dialog(st, "system", "Walk with WASD/Arrows or click floor. Chat talks to nearest agent.", SK_COL_MUTED);
    push_dialog(st, "display-abi", "Built 4.3 KB WASI ABI — ready for 2D split inspection.", SK_COL_CYAN);

    update_nearest_agent(st);
}

static void rebind_agent_surface(SkApp* app, int agent_idx, uint32_t target_mode) {
    WorldState* st = (WorldState*)app->user;
    if (agent_idx < 0 || agent_idx >= NUM_AGENTS) return;
    AgentEntity* ag = &st->agents[agent_idx];

    B8GWorldPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.pos_x = ag->desk_x * 10.0f - 5.0f;
    pose.pos_y = 1.25f;
    pose.pos_z = -ag->desk_y * 8.0f;
    pose.rot_w = 1.0f;
    pose.scale = 1.0f;

    int32_t rc = sectium_surface_rebind_mode(
        &app->display,
        ag->surface_id,
        target_mode,
        (target_mode == B8G_COMP_OPEN_WORLD_3D) ? &pose : NULL
    );

    if (rc == B8G_DISPLAY_OK) {
        uint64_t seq = 0;
        sectium_surface_damage(&app->display, ag->surface_id, 0, 0, 1024, 640);
        sectium_surface_commit(&app->display, ag->surface_id, sk_now_ms(), &seq);
        const B8GSurface* s = find_surface(&app->display, ag->surface_id);
        char line[96];
        if (target_mode == B8G_COMP_TILING_2D) {
            snprintf(line, sizeof(line), "[rebind #%u] Pulled S#%u -> B8G_COMP_TILING_2D",
                     s ? s->rebind_count : 0u, ag->surface_id);
            snprintf(ag->bubble, sizeof(ag->bubble), "Docked in 2D Split (#%u)!", s ? s->rebind_count : 0u);
        } else {
            snprintf(line, sizeof(line), "[rebind #%u] Returned S#%u -> OPEN_WORLD_3D",
                     s ? s->rebind_count : 0u, ag->surface_id);
            snprintf(ag->bubble, sizeof(ag->bubble), "Back at 3D Desk (#%u)!", s ? s->rebind_count : 0u);
        }
        push_term_line(ag, line);
        push_dialog(st, ag->name, ag->bubble, ag->accent);
    }
}

static void submit_chat(SkApp* app) {
    WorldState* st = (WorldState*)app->user;
    if (st->chat_len <= 0) return;

    AgentEntity* ag = &st->agents[st->nearest_agent];
    st->selected_agent = st->nearest_agent;

    push_dialog(st, "You (Frank)", st->chat_buf, SK_COL_ACCENT);

    char tline[96];
    snprintf(tline, sizeof(tline), "<frank> %.78s", st->chat_buf);
    push_term_line(ag, tline);

    ag->reply_counter++;
    const B8GSurface* s = find_surface(&app->display, ag->surface_id);
    if (st->nearest_agent == 0) {
        snprintf(ag->bubble, sizeof(ag->bubble), "Ack #%d! S#%u ABI verified (%s)",
                 ag->reply_counter, ag->surface_id,
                 (s && s->compositor_mode == B8G_COMP_TILING_2D) ? "2D" : "3D");
    } else if (st->nearest_agent == 1) {
        snprintf(ag->bubble, sizeof(ag->bubble), "Copy #%d! WM rebinds=%u, zero drop",
                 ag->reply_counter, s ? s->rebind_count : 0u);
    } else {
        snprintf(ag->bubble, sizeof(ag->bubble), "Compiled #%d! 0 warns, .b8p warm",
                 ag->reply_counter);
    }

    push_dialog(st, ag->name, ag->bubble, ag->accent);
    snprintf(tline, sizeof(tline), "[reply] %.80s", ag->bubble);
    push_term_line(ag, tline);

    uint64_t seq = 0;
    sectium_surface_commit(&app->display, ag->surface_id, sk_now_ms(), &seq);

    st->chat_len = 0;
    st->chat_buf[0] = '\0';
}

/* Layout helpers */
static int split_x(int w) { return (w * 65) / 100; }
static SkRect rect_world(int w, int h) { return sk_rect(12, 48, split_x(w) - 20, h - 154); }
static SkRect rect_dialog_box(int w, int h) { return sk_rect(12, h - 100, split_x(w) - 20, 52); }
static SkRect rect_chat_bar(int w, int h) { return sk_rect(12, h - 44, split_x(w) - 108, 32); }
static SkRect rect_send_btn(int w, int h) { return sk_rect(split_x(w) - 90, h - 44, 82, 32); }
static SkRect rect_dock(int w, int h) { return sk_rect(split_x(w) + 4, 48, w - split_x(w) - 16, h - 60); }
static SkRect rect_pull_btn(int w, int h) {
    SkRect d = rect_dock(w, h);
    int bw = (d.w - 34) / 2;
    return sk_rect(d.x + 12, d.y + 126, bw, 34);
}
static SkRect rect_return_btn(int w, int h) {
    SkRect d = rect_dock(w, h);
    int bw = (d.w - 34) / 2;
    return sk_rect(d.x + 22 + bw, d.y + 126, bw, 34);
}

static void on_tick(SkApp* app, uint64_t now_ms) {
    (void)now_ms;
    ensure_init(app);
    WorldState* st = (WorldState*)app->user;
    st->tick_counter++;

    if (st->has_target) {
        float dx = st->target_x - st->human_x;
        float dy = st->target_y - st->human_y;
        float d2 = dx * dx + dy * dy;
        if (d2 < 0.0004f) {
            st->human_x = st->target_x;
            st->human_y = st->target_y;
            st->has_target = false;
        } else {
            st->human_x += dx * 0.18f;
            st->human_y += dy * 0.18f;
        }
        update_nearest_agent(st);
    }
}

static void on_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    ensure_init(app);
    WorldState* st = (WorldState*)app->user;
    if (msg != WM_LBUTTONDOWN) return;

    if (sk_pt_in_rect(x, y, rect_chat_bar(app->width, app->height))) {
        st->chat_focused = true;
        return;
    }
    if (sk_pt_in_rect(x, y, rect_send_btn(app->width, app->height))) {
        submit_chat(app);
        return;
    }

    /* Dock agent selector tabs */
    SkRect d = rect_dock(app->width, app->height);
    int tab_w = (d.w - 32) / NUM_AGENTS;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        SkRect tr = sk_rect(d.x + 12 + i * (tab_w + 4), d.y + 12, tab_w, 28);
        if (sk_pt_in_rect(x, y, tr)) {
            st->selected_agent = i;
            st->chat_focused = false;
            return;
        }
    }

    if (sk_pt_in_rect(x, y, rect_pull_btn(app->width, app->height))) {
        rebind_agent_surface(app, st->selected_agent, B8G_COMP_TILING_2D);
        return;
    }
    if (sk_pt_in_rect(x, y, rect_return_btn(app->width, app->height))) {
        rebind_agent_surface(app, st->selected_agent, B8G_COMP_OPEN_WORLD_3D);
        return;
    }

    /* Click inside open world: check agent desks first, else walk human avatar */
    SkRect wr = rect_world(app->width, app->height);
    if (sk_pt_in_rect(x, y, wr)) {
        st->chat_focused = false;
        for (int i = 0; i < NUM_AGENTS; ++i) {
            int ax = wr.x + (int)(st->agents[i].desk_x * (float)wr.w);
            int ay = wr.y + (int)(st->agents[i].desk_y * (float)wr.h);
            int dx = x - ax;
            int dy = y - ay;
            if (dx * dx + dy * dy <= 44 * 44) {
                st->selected_agent = i;
            }
        }
        float nx = (float)(x - wr.x) / (float)wr.w;
        float ny = (float)(y - wr.y) / (float)wr.h;
        if (nx < 0.06f) nx = 0.06f;
        if (nx > 0.94f) nx = 0.94f;
        if (ny < 0.12f) ny = 0.12f;
        if (ny > 0.92f) ny = 0.92f;
        st->target_x = nx;
        st->target_y = ny;
        st->has_target = true;
    }
}

static void on_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    ensure_init(app);
    WorldState* st = (WorldState*)app->user;

    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
            st->chat_focused = false;
            return;
        }
        if (wp == VK_RETURN) {
            if (st->chat_focused) {
                submit_chat(app);
            } else {
                st->chat_focused = true;
            }
            return;
        }
        if (wp == VK_BACK && st->chat_focused) {
            if (st->chat_len > 0) {
                st->chat_buf[--st->chat_len] = '\0';
            }
            return;
        }

        /* Walk with Arrow keys always, or WASD when chat is unfocused */
        float step = 0.032f;
        bool moved = false;
        if (wp == VK_LEFT || (!st->chat_focused && wp == 'A')) { st->human_x -= step; moved = true; }
        if (wp == VK_RIGHT || (!st->chat_focused && wp == 'D')) { st->human_x += step; moved = true; }
        if (wp == VK_UP || (!st->chat_focused && wp == 'W')) { st->human_y -= step; moved = true; }
        if (wp == VK_DOWN || (!st->chat_focused && wp == 'S')) { st->human_y += step; moved = true; }

        if (moved) {
            st->has_target = false;
            if (st->human_x < 0.06f) st->human_x = 0.06f;
            if (st->human_x > 0.94f) st->human_x = 0.94f;
            if (st->human_y < 0.12f) st->human_y = 0.12f;
            if (st->human_y > 0.92f) st->human_y = 0.92f;
            update_nearest_agent(st);
            st->selected_agent = st->nearest_agent;
        }
    } else if (msg == WM_CHAR) {
        unsigned int ch = (unsigned int)wp;
        if (st->chat_focused && ch >= 32 && ch < 127 && st->chat_len < MAX_CHAT_INPUT - 1) {
            st->chat_buf[st->chat_len++] = (char)ch;
            st->chat_buf[st->chat_len] = '\0';
        }
    }
}

static void on_render(SkApp* app, HDC hdc) {
    ensure_init(app);
    WorldState* st = (WorldState*)app->user;
    int w = app->width;
    int h = app->height;

    B8GDisplayStats stats;
    memset(&stats, 0, sizeof(stats));
    sectium_display_get_stats(&app->display, &stats);

    /* Top header bar */
    sk_fill_rect(hdc, sk_rect(0, 0, w, 40), SK_COL_SURFACE);
    sk_line(hdc, 0, 39, w, 39, SK_COL_BORDER, 1);
    sk_text(hdc, app->font_title, sk_rect(14, 0, 360, 40),
            "SECTIUM MULTI-AGENT OPEN WORLD", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    char hdr_stats[160];
    snprintf(hdr_stats, sizeof(hdr_stats),
             "Actors: %u  |  Streams: %u  |  3D: %u  |  2D: %u  |  Total Rebinds: %llu",
             stats.active_actors, stats.active_bindings, stats.surfaces_3d, stats.surfaces_2d,
             (unsigned long long)stats.total_rebinds);
    sk_text(hdc, app->font_mono, sk_rect(380, 0, w - 394, 40),
            hdr_stats, SK_COL_CYAN, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* Left 65%: Open World Grid */
    SkRect wr = rect_world(w, h);
    sk_round_rect(hdc, wr, 10, SK_COL_SURFACE, SK_COL_BORDER);

    /* Grid lines */
    int grid_step = 40;
    for (int gx = wr.x + grid_step; gx < wr.x + wr.w; gx += grid_step) {
        sk_line(hdc, gx, wr.y + 4, gx, wr.y + wr.h - 4, SK_COL_SURFACE2, 1);
    }
    for (int gy = wr.y + grid_step; gy < wr.y + wr.h; gy += grid_step) {
        sk_line(hdc, wr.x + 4, gy, wr.x + wr.w - 4, gy, SK_COL_SURFACE2, 1);
    }

    sk_text(hdc, app->font_small, sk_rect(wr.x + 12, wr.y + 8, wr.w - 24, 18),
            "OPEN WORLD FLOOR  |  WASD / Arrows or Click to walk  |  Click Agent desk to inspect",
            SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

    /* Walk target marker */
    if (st->has_target) {
        int tx = wr.x + (int)(st->target_x * (float)wr.w);
        int ty = wr.y + (int)(st->target_y * (float)wr.h);
        sk_circle(hdc, tx, ty, 8, SK_COL_SURFACE2, SK_COL_ACCENT);
    }

    /* Draw proximity tether from human to nearest agent */
    int hx = wr.x + (int)(st->human_x * (float)wr.w);
    int hy = wr.y + (int)(st->human_y * (float)wr.h);
    {
        AgentEntity* nag = &st->agents[st->nearest_agent];
        int ax = wr.x + (int)(nag->desk_x * (float)wr.w);
        int ay = wr.y + (int)(nag->desk_y * (float)wr.h);
        sk_line(hdc, hx, hy, ax, ay, SK_COL_ACCENT_DIM, 2);
    }

    /* Draw 3 Agent desks + avatars + speech bubbles */
    int bob = (int)((st->tick_counter / 6) % 4);
    if (bob == 3) bob = 1;

    for (int i = 0; i < NUM_AGENTS; ++i) {
        AgentEntity* ag = &st->agents[i];
        const B8GSurface* surf = find_surface(&app->display, ag->surface_id);
        bool is_2d = (surf && surf->compositor_mode == B8G_COMP_TILING_2D);
        bool is_nearest = (i == st->nearest_agent);
        bool is_sel = (i == st->selected_agent);

        int ax = wr.x + (int)(ag->desk_x * (float)wr.w);
        int ay = wr.y + (int)(ag->desk_y * (float)wr.h);

        /* Desk rectangle */
        SkRect desk = sk_rect(ax - 62, ay + 14, 124, 42);
        sk_round_rect(hdc, desk, 6, SK_COL_SURFACE2, is_sel ? ag->accent : SK_COL_BORDER);

        /* Desk monitor showing 3D vs 2D state */
        SkRect mon = sk_rect(ax - 44, ay + 20, 88, 22);
        sk_round_rect(hdc, mon, 4, is_2d ? SK_COL_BG : SK_COL_SURFACE3, is_2d ? SK_COL_MUTED : ag->accent);
        sk_text(hdc, app->font_small, mon, is_2d ? "IN 2D SPLIT" : "3D TERMINAL",
                is_2d ? SK_COL_MUTED : ag->accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        /* Proximity highlight ring */
        if (is_nearest) {
            sk_circle(hdc, ax, ay - 6 + bob, 26, SK_COL_SURFACE, ag->accent);
        }

        /* Agent avatar circle */
        sk_circle(hdc, ax, ay - 6 + bob, 19, ag->accent, SK_COL_TEXT);
        char initials[4];
        initials[0] = (char)(ag->name[0] >= 'a' ? ag->name[0] - 32 : ag->name[0]);
        initials[1] = (char)(ag->name[1] >= 'a' ? ag->name[1] - 32 : ag->name[1]);
        initials[2] = '\0';
        sk_text(hdc, app->font_bold, sk_rect(ax - 18, ay - 22 + bob, 36, 32),
                initials, SK_COL_BG, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        /* Name tag under desk */
        char tag[64];
        snprintf(tag, sizeof(tag), "@%s (A#%u)", ag->name, ag->actor_id);
        sk_text(hdc, app->font_bold, sk_rect(ax - 75, ay + 58, 150, 18),
                tag, is_sel ? ag->accent : SK_COL_TEXT, DT_CENTER | DT_SINGLELINE);

        /* Speech bubble above avatar */
        SkRect bub = sk_rect(ax - 96, ay - 62 + bob, 192, 28);
        sk_round_rect(hdc, bub, 8, SK_COL_BG, ag->accent);
        sk_text(hdc, app->font_small, sk_rect(bub.x + 6, bub.y, bub.w - 12, bub.h),
                ag->bubble, SK_COL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Draw Human Avatar "You (Frank)" */
    sk_circle(hdc, hx, hy, 22, SK_COL_ACCENT, SK_COL_TEXT);
    sk_text(hdc, app->font_bold, sk_rect(hx - 20, hy - 14, 40, 28), "YOU", SK_COL_BG, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    sk_pill(hdc, app->font_small, sk_rect(hx - 48, hy + 25, 96, 20),
            "You (Frank)", SK_COL_BG, SK_COL_ACCENT, SK_COL_ACCENT);

    /* Dialog mini-log above chat bar */
    SkRect dbox = rect_dialog_box(w, h);
    sk_round_rect(hdc, dbox, 6, SK_COL_SURFACE, SK_COL_BORDER);
    int show_lines = 2;
    int start = st->dialog_count > show_lines ? (st->dialog_count - show_lines) : 0;
    for (int i = start; i < st->dialog_count; ++i) {
        int row = i - start;
        char line[180];
        snprintf(line, sizeof(line), "[%s]: %s", st->dialog[i].sender, st->dialog[i].text);
        sk_text(hdc, app->font_small, sk_rect(dbox.x + 10, dbox.y + 6 + row * 20, dbox.w - 20, 18),
                line, st->dialog[i].col, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Bottom Chat Bar */
    SkRect cbar = rect_chat_bar(w, h);
    sk_round_rect(hdc, cbar, 6, SK_COL_SURFACE, st->chat_focused ? SK_COL_ACCENT : SK_COL_BORDER);
    bool blink = ((app->now_ms / 500) % 2) == 0;
    char prompt[192];
    if (st->chat_len == 0) {
        snprintf(prompt, sizeof(prompt), "Talk to nearest agent @%s... (click or press Enter)%s",
                 st->agents[st->nearest_agent].name, (st->chat_focused && blink) ? "|" : "");
        sk_text(hdc, app->font_ui, sk_rect(cbar.x + 10, cbar.y, cbar.w - 20, cbar.h),
                prompt, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        snprintf(prompt, sizeof(prompt), "%s%s", st->chat_buf, (st->chat_focused && blink) ? "|" : "");
        sk_text(hdc, app->font_ui, sk_rect(cbar.x + 10, cbar.y, cbar.w - 20, cbar.h),
                prompt, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    sk_button(hdc, app, rect_send_btn(w, h), "Send", true, SK_COL_ACCENT);

    /* Right 35%: Live 2D Split Dock */
    SkRect dr = rect_dock(w, h);
    sk_round_rect(hdc, dr, 10, SK_COL_SURFACE, SK_COL_BORDER);

    /* Agent Tabs */
    int tab_w = (dr.w - 32) / NUM_AGENTS;
    for (int i = 0; i < NUM_AGENTS; ++i) {
        SkRect tr = sk_rect(dr.x + 12 + i * (tab_w + 4), dr.y + 12, tab_w, 28);
        sk_button(hdc, app, tr, st->agents[i].name, i == st->selected_agent, st->agents[i].accent);
    }

    AgentEntity* sel = &st->agents[st->selected_agent];
    const B8GSurface* s = find_surface(&app->display, sel->surface_id);
    bool in_2d = (s && s->compositor_mode == B8G_COMP_TILING_2D);

    /* Selected Agent Metadata Card */
    SkRect meta = sk_rect(dr.x + 12, dr.y + 48, dr.w - 24, 70);
    sk_round_rect(hdc, meta, 6, SK_COL_SURFACE2, sel->accent);
    char m1[96], m2[96], m3[96];
    snprintf(m1, sizeof(m1), "Actor #%u: %s  (%s)", sel->actor_id, sel->name, sel->role);
    snprintf(m2, sizeof(m2), "Surface #%u  |  Mode: %s  |  rebind_count: %u",
             sel->surface_id, in_2d ? "TILING_2D" : "OPEN_WORLD_3D", s ? s->rebind_count : 0u);
    snprintf(m3, sizeof(m3), "Streams: AVATAR(#%u) + SURFACE(#%u) + DIALOG(#%u)",
             sel->bind_avatar, sel->bind_surface, sel->bind_dialog);
    sk_text(hdc, app->font_bold, sk_rect(meta.x + 10, meta.y + 6, meta.w - 20, 18), m1, sel->accent, DT_LEFT | DT_SINGLELINE);
    sk_text(hdc, app->font_mono, sk_rect(meta.x + 10, meta.y + 26, meta.w - 20, 18), m2, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
    sk_text(hdc, app->font_small, sk_rect(meta.x + 10, meta.y + 46, meta.w - 20, 18), m3, SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

    /* Rebind Action Buttons */
    sk_button(hdc, app, rect_pull_btn(w, h), "[Pull to 2D Split]", in_2d, SK_COL_GREEN);
    sk_button(hdc, app, rect_return_btn(w, h), "[Return to 3D Desk]", !in_2d, SK_COL_AMBER);

    /* Live Terminal Surface View */
    SkRect term = sk_rect(dr.x + 12, dr.y + 170, dr.w - 24, dr.h - 296);
    sk_round_rect(hdc, term, 6, SK_COL_BG, in_2d ? SK_COL_GREEN : SK_COL_BORDER);
    sk_fill_rect(hdc, sk_rect(term.x + 1, term.y + 1, term.w - 2, 24), SK_COL_SURFACE2);
    char thdr[96];
    snprintf(thdr, sizeof(thdr), "LIVE TERMINAL SURFACE #%u  [%s]",
             sel->surface_id, in_2d ? "DOCKED IN 2D SPLIT" : "PREVIEW FROM 3D DESK");
    sk_text(hdc, app->font_small, sk_rect(term.x + 8, term.y + 3, term.w - 16, 18),
            thdr, in_2d ? SK_COL_GREEN : SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

    for (int i = 0; i < sel->term_count; ++i) {
        sk_text(hdc, app->font_mono, sk_rect(term.x + 10, term.y + 32 + i * 22, term.w - 20, 20),
                sel->term_lines[i], (i == sel->term_count - 1) ? sel->accent : SK_COL_TEXT,
                DT_LEFT | DT_SINGLELINE);
    }

    /* Dialog Stream History */
    SkRect dlog = sk_rect(dr.x + 12, dr.y + dr.h - 118, dr.w - 24, 106);
    sk_round_rect(hdc, dlog, 6, SK_COL_SURFACE2, SK_COL_BORDER);
    sk_text(hdc, app->font_small, sk_rect(dlog.x + 8, dlog.y + 4, dlog.w - 16, 16),
            "DIALOG STREAM LOG", SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);
    int dstart = st->dialog_count > 4 ? (st->dialog_count - 4) : 0;
    for (int i = dstart; i < st->dialog_count; ++i) {
        int r = i - dstart;
        char dl[180];
        snprintf(dl, sizeof(dl), "%s: %s", st->dialog[i].sender, st->dialog[i].text);
        sk_text(hdc, app->font_small, sk_rect(dlog.x + 8, dlog.y + 22 + r * 20, dlog.w - 16, 18),
                dl, st->dialog[i].col, DT_LEFT | DT_SINGLELINE);
    }
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show) {
    (void)hinst;
    (void)hprev;
    (void)cmdline;
    (void)show;

    WorldState state;
    memset(&state, 0, sizeof(state));

    SkApp app;
    memset(&app, 0, sizeof(app));
    app.user = &state;
    app.on_render = on_render;
    app.on_mouse = on_mouse;
    app.on_key = on_key;
    app.on_tick = on_tick;

    return sk_run(&app, "SectiumMultiAgentWorld", "Sectium Multi-Agent Open World", WORLD_WIN_W, WORLD_WIN_H, false, false, 33);
}
