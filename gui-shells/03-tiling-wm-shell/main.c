#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 03 — Scriptable Tiling WM Shell (Workspaces + Dynamic Layouts + B8G_DISPLAY_V1)
 * ============================================================================ */

#define WM_WS_COUNT       5
#define WM_MAX_PANES      6
#define WM_TOPBAR_H       44
#define WM_STATUSBAR_H    28
#define WM_GAP            8

typedef enum WmLayout {
    WM_LAYOUT_MASTER_STACK = 0,
    WM_LAYOUT_GRID         = 1,
    WM_LAYOUT_COLUMNS      = 2
} WmLayout;

typedef struct WmPane {
    char     title[48];
    char     subtitle[64];
    uint32_t surface_id;
    uint32_t leases;
    COLORREF accent;
    SkRect   rect;
} WmPane;

typedef struct WmWorkspace {
    const char* name;
    WmLayout    layout;
    int         pane_count;
    int         focused_pane;
    WmPane      panes[WM_MAX_PANES];
} WmWorkspace;

typedef struct WmState {
    bool        initialized;
    int         active_ws;
    uint32_t    pane_seq;
    WmWorkspace ws[WM_WS_COUNT];

    /* Hit-test rects */
    SkRect      ws_rects[WM_WS_COUNT];
    SkRect      btn_layout;
    SkRect      btn_add_pane;
    SkRect      btn_rem_pane;
    SkRect      btn_freeze;
} WmState;

static B8GSurface* wm_find_surface(B8GDisplayCtx* ctx, uint32_t surface_id) {
    for (uint32_t i = 0; i < B8G_DISPLAY_MAX_SURFACES; i++) {
        if (ctx->surfaces[i].active && ctx->surfaces[i].surface_id == surface_id) {
            return &ctx->surfaces[i];
        }
    }
    return NULL;
}

static const char* wm_layout_name(WmLayout l) {
    switch (l) {
        case WM_LAYOUT_MASTER_STACK: return "Master-Stack";
        case WM_LAYOUT_GRID:         return "Grid";
        case WM_LAYOUT_COLUMNS:      return "Columns";
        default:                     return "Master-Stack";
    }
}

static void wm_add_pane_to_ws(SkApp* app, WmState* st, int ws_idx, const char* title, const char* sub, uint32_t leases, COLORREF accent) {
    WmWorkspace* w = &st->ws[ws_idx];
    if (w->pane_count >= WM_MAX_PANES) return;

    WmPane* p = &w->panes[w->pane_count];
    memset(p, 0, sizeof(*p));
    snprintf(p->title, sizeof(p->title), "%s", title);
    snprintf(p->subtitle, sizeof(p->subtitle), "%s", sub);
    p->leases = leases;
    p->accent = accent;

    uint32_t sid = 0;
    sectium_surface_create(&app->display, app->human_actor_id, 640u, 400u, B8G_COMP_TILING_2D, &sid);
    p->surface_id = sid;
    if (leases != B8G_LEASE_NONE) {
        sectium_surface_set_leases(&app->display, sid, leases);
    }
    uint32_t bid = 0;
    sectium_actor_bind_stream(&app->display, app->human_actor_id, sid, B8G_STREAM_SURFACE, &bid);
    uint64_t seq = 0;
    sectium_surface_commit(&app->display, sid, app->now_ms, &seq);

    w->focused_pane = w->pane_count;
    w->pane_count++;
    st->pane_seq++;
}

static void wm_ensure_init(SkApp* app, WmState* st) {
    if (st->initialized) return;
    st->initialized = true;
    st->active_ws = 0;
    st->pane_seq = 1;

    const char* names[WM_WS_COUNT] = { "[1] Code", "[2] Web", "[3] Swarm", "[4] Media", "[5] Bench" };
    for (int i = 0; i < WM_WS_COUNT; i++) {
        st->ws[i].name = names[i];
        st->ws[i].layout = (i == 2) ? WM_LAYOUT_GRID : ((i == 4) ? WM_LAYOUT_COLUMNS : WM_LAYOUT_MASTER_STACK);
        st->ws[i].pane_count = 0;
        st->ws[i].focused_pane = 0;
    }

    /* Workspace 1: Code */
    wm_add_pane_to_ws(app, st, 0, "nvim: sectium_display.c",  "B8G_DISPLAY_V1 C11 core",        B8G_LEASE_NONE,    SK_COL_ACCENT);
    wm_add_pane_to_ws(app, st, 0, "ttyS0: holo-compiler",     "zig cc -O2 -Wall -Wextra",       B8G_LEASE_COMPUTE, SK_COL_GREEN);
    wm_add_pane_to_ws(app, st, 0, "git: uvs-merkle",          "0 unstaged | branch main",       B8G_LEASE_NONE,    SK_COL_CYAN);
    st->ws[0].focused_pane = 0;

    /* Workspace 2: Web */
    wm_add_pane_to_ws(app, st, 1, "Sectium Browser",          "https://github.com/awesome-os",  B8G_LEASE_NONE,    SK_COL_ACCENT);
    wm_add_pane_to_ws(app, st, 1, "Docs: B8G_ARENA_V1",       "64-byte aligned zero-copy spec", B8G_LEASE_NONE,    SK_COL_CYAN);
    st->ws[1].focused_pane = 0;

    /* Workspace 3: Swarm */
    wm_add_pane_to_ws(app, st, 2, "Agent @coordinator",       "Swarm orchestrator stream",      B8G_LEASE_NET_PUSH, SK_COL_PURPLE);
    wm_add_pane_to_ws(app, st, 2, "Agent @shells-a",          "Win32 GUI shell boilerplates",   B8G_LEASE_NET_PUSH, SK_COL_ACCENT);
    wm_add_pane_to_ws(app, st, 2, "Agent @verifier",          "Zero-warning build gate",        B8G_LEASE_NET_PUSH, SK_COL_GREEN);
    wm_add_pane_to_ws(app, st, 2, "Swarm Message Board",      "NDJSON channel=command",         B8G_LEASE_NET_PUSH, SK_COL_AMBER);
    st->ws[2].focused_pane = 0;

    /* Workspace 4: Media */
    wm_add_pane_to_ws(app, st, 3, "Twitch Live [AUDIO]",      "LEASE_AUDIO -> DISPLAY_SLEEP",   B8G_LEASE_AUDIO,   SK_COL_PURPLE);
    wm_add_pane_to_ws(app, st, 3, "Audio Mixer",              "48 kHz zero-copy ring buffer",   B8G_LEASE_AUDIO,   SK_COL_CYAN);
    st->ws[3].focused_pane = 0;

    /* Workspace 5: Bench */
    wm_add_pane_to_ws(app, st, 4, "b8g-bench: Frame Commit",  "0.12 ms avg commit latency",     B8G_LEASE_COMPUTE, SK_COL_GREEN);
    wm_add_pane_to_ws(app, st, 4, "b8g-bench: .b8p Checkpoint","Warm-RAM WoL arena snapshot",   B8G_LEASE_COMPUTE, SK_COL_AMBER);
    wm_add_pane_to_ws(app, st, 4, "b8g-bench: Control Plane", "5,080 B Linux .so / 20 KB DLL",  B8G_LEASE_NONE,    SK_COL_ACCENT);
    st->ws[4].focused_pane = 0;
}

static void wm_add_dynamic_pane(SkApp* app, WmState* st) {
    WmWorkspace* w = &st->ws[st->active_ws];
    if (w->pane_count >= WM_MAX_PANES) return;
    char title[48];
    char sub[64];
    snprintf(title, sizeof(title), "Pane #%u (%s)", st->pane_seq, w->name);
    snprintf(sub, sizeof(sub), "Dynamic B8G_DISPLAY_V1 surface");
    uint32_t lease = (st->active_ws == 3) ? B8G_LEASE_AUDIO : ((st->active_ws == 2) ? B8G_LEASE_NET_PUSH : B8G_LEASE_NONE);
    COLORREF cols[4] = { SK_COL_ACCENT, SK_COL_CYAN, SK_COL_GREEN, SK_COL_PURPLE };
    wm_add_pane_to_ws(app, st, st->active_ws, title, sub, lease, cols[w->pane_count % 4]);
}

static void wm_remove_focused_pane(WmState* st) {
    WmWorkspace* w = &st->ws[st->active_ws];
    if (w->pane_count <= 1) return;
    int idx = w->focused_pane;
    for (int i = idx; i < w->pane_count - 1; i++) {
        w->panes[i] = w->panes[i + 1];
    }
    w->pane_count--;
    if (w->focused_pane >= w->pane_count) {
        w->focused_pane = w->pane_count - 1;
    }
}

static void wm_toggle_freeze_focused(SkApp* app, WmState* st) {
    WmWorkspace* w = &st->ws[st->active_ws];
    if (w->pane_count <= 0) return;
    WmPane* p = &w->panes[w->focused_pane];
    B8GSurface* s = wm_find_surface(&app->display, p->surface_id);
    if (!s) return;

    if (s->state == B8G_SURFACE_ACTIVE || s->state == B8G_SURFACE_IDLE) {
        uint64_t future = app->now_ms + 120000ULL;
        uint32_t out = 0;
        sectium_surface_evaluate_idle(&app->display, p->surface_id, future, 10000ULL, 30000ULL, &out);
        sectium_surface_evaluate_idle(&app->display, p->surface_id, future + 30000ULL, 10000ULL, 30000ULL, &out);
    } else {
        sectium_surface_thaw(&app->display, p->surface_id, app->now_ms);
        uint64_t seq = 0;
        sectium_surface_commit(&app->display, p->surface_id, app->now_ms, &seq);
    }
}

static void wm_compute_layout(WmWorkspace* w, SkRect area) {
    int n = w->pane_count;
    if (n <= 0) return;

    if (n == 1) {
        w->panes[0].rect = area;
        return;
    }

    if (w->layout == WM_LAYOUT_MASTER_STACK) {
        int master_w = (area.w - WM_GAP) * 55 / 100;
        int stack_w = area.w - WM_GAP - master_w;
        w->panes[0].rect = sk_rect(area.x, area.y, master_w, area.h);

        int stack_n = n - 1;
        int total_gap = WM_GAP * (stack_n - 1);
        int cell_h = (area.h - total_gap) / stack_n;
        for (int i = 0; i < stack_n; i++) {
            int cy = area.y + i * (cell_h + WM_GAP);
            int ch = (i == stack_n - 1) ? (area.y + area.h - cy) : cell_h;
            w->panes[i + 1].rect = sk_rect(area.x + master_w + WM_GAP, cy, stack_w, ch);
        }
    } else if (w->layout == WM_LAYOUT_GRID) {
        int cols = (n <= 2) ? 2 : ((n <= 4) ? 2 : 3);
        int rows = (n + cols - 1) / cols;
        int cell_w = (area.w - WM_GAP * (cols - 1)) / cols;
        int cell_h = (area.h - WM_GAP * (rows - 1)) / rows;
        for (int i = 0; i < n; i++) {
            int r = i / cols;
            int c = i % cols;
            int cx = area.x + c * (cell_w + WM_GAP);
            int cy = area.y + r * (cell_h + WM_GAP);
            int cw = (c == cols - 1) ? (area.x + area.w - cx) : cell_w;
            int ch = (r == rows - 1) ? (area.y + area.h - cy) : cell_h;
            w->panes[i].rect = sk_rect(cx, cy, cw, ch);
        }
    } else {
        /* WM_LAYOUT_COLUMNS */
        int cell_w = (area.w - WM_GAP * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            int cx = area.x + i * (cell_w + WM_GAP);
            int cw = (i == n - 1) ? (area.x + area.w - cx) : cell_w;
            w->panes[i].rect = sk_rect(cx, area.y, cw, area.h);
        }
    }
}

static void wm_render(SkApp* app, HDC hdc) {
    WmState* st = (WmState*)app->user;
    wm_ensure_init(app, st);

    int w = app->width;
    int h = app->height;
    WmWorkspace* ws = &st->ws[st->active_ws];

    /* 1. Top Workspace & Control Bar */
    sk_fill_rect(hdc, sk_rect(0, 0, w, WM_TOPBAR_H), SK_COL_SURFACE);
    sk_line(hdc, 0, WM_TOPBAR_H - 1, w, WM_TOPBAR_H - 1, SK_COL_BORDER, 1);

    int wx = 10;
    for (int i = 0; i < WM_WS_COUNT; i++) {
        SkRect wr = sk_rect(wx, 7, 94, 30);
        st->ws_rects[i] = wr;
        sk_button(hdc, app, wr, st->ws[i].name, i == st->active_ws, SK_COL_ACCENT);
        wx += 100;
    }

    int rx = w - 10;
    st->btn_freeze   = sk_rect(rx - 128, 7, 128, 30); rx -= 136;
    st->btn_rem_pane = sk_rect(rx - 76,  7, 76,  30); rx -= 84;
    st->btn_add_pane = sk_rect(rx - 76,  7, 76,  30); rx -= 84;
    st->btn_layout   = sk_rect(rx - 124, 7, 124, 30);

    sk_button(hdc, app, st->btn_layout,   wm_layout_name(ws->layout), true,  SK_COL_CYAN);
    sk_button(hdc, app, st->btn_add_pane, "+ Pane",                   false, SK_COL_GREEN);
    sk_button(hdc, app, st->btn_rem_pane, "- Pane",                   false, SK_COL_RED);
    sk_button(hdc, app, st->btn_freeze,   "Freeze Focused",           false, SK_COL_PURPLE);

    /* 2. Dynamic Tiled Panes */
    SkRect tile_area = sk_rect(WM_GAP, WM_TOPBAR_H + WM_GAP, w - WM_GAP * 2, h - WM_TOPBAR_H - WM_STATUSBAR_H - WM_GAP * 2);
    wm_compute_layout(ws, tile_area);

    for (int i = 0; i < ws->pane_count; i++) {
        WmPane* p = &ws->panes[i];
        SkRect r = p->rect;
        bool focused = (i == ws->focused_pane);
        B8GSurface* s = wm_find_surface(&app->display, p->surface_id);
        uint32_t state = s ? s->state : B8G_SURFACE_ACTIVE;
        COLORREF st_col = sk_surface_state_color(state);

        sk_fill_rect(hdc, r, SK_COL_SURFACE);
        sk_stroke_rect(hdc, r, focused ? SK_COL_ACCENT : SK_COL_BORDER, focused ? 3 : 1);

        /* Pane Title Bar */
        SkRect hdr = sk_rect(r.x + 2, r.y + 2, r.w - 4, 30);
        sk_fill_rect(hdc, hdr, focused ? SK_COL_SURFACE3 : SK_COL_SURFACE2);
        sk_circle(hdc, hdr.x + 12, hdr.y + 15, 4, p->accent, p->accent);
        sk_text(hdc, focused ? app->font_bold : app->font_ui,
                sk_rect(hdr.x + 24, hdr.y, hdr.w - 140, hdr.h),
                p->title, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SkRect pill = sk_rect(hdr.x + hdr.w - 110, hdr.y + 5, 102, 20);
        sk_pill(hdc, app->font_small, pill, sk_surface_state_name(state), SK_COL_BG, st_col, st_col);

        /* Pane Content */
        SkRect body = sk_rect(r.x + 14, r.y + 42, r.w - 28, r.h - 54);
        if (body.w > 40 && body.h > 40) {
            sk_text(hdc, app->font_bold, sk_rect(body.x, body.y, body.w, 22),
                    p->subtitle, focused ? SK_COL_TEXT : SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

            const char* lease_str = (p->leases & B8G_LEASE_AUDIO) ? "LEASE_AUDIO" :
                                    (p->leases & B8G_LEASE_NET_PUSH) ? "LEASE_NET_PUSH" :
                                    (p->leases & B8G_LEASE_COMPUTE) ? "LEASE_COMPUTE" : "LEASE_NONE";

            char info[256];
            snprintf(info, sizeof(info),
                     "surface_id: #%u\nstate:      %s\nwake_lease: %s\ncommits:    %llu\ncheckpoint: epoch %llu\nrect:       %dx%d",
                     p->surface_id, sk_surface_state_name(state), lease_str,
                     (unsigned long long)(s ? s->commit_seq : 0),
                     (unsigned long long)(s ? s->checkpoint_epoch : 0),
                     r.w, r.h);
            sk_text(hdc, app->font_mono, sk_rect(body.x, body.y + 30, body.w, body.h - 30),
                    info, state == B8G_SURFACE_CHECKPOINTED ? SK_COL_PURPLE : SK_COL_CYAN, DT_LEFT);
        }
    }

    /* 3. Bottom Status Bar with Keyboard Hints & Live B8G_DISPLAY_V1 Stats */
    B8GDisplayStats stats;
    sectium_display_get_stats(&app->display, &stats);
    SkRect sb = sk_rect(0, h - WM_STATUSBAR_H, w, WM_STATUSBAR_H);
    sk_fill_rect(hdc, sb, SK_COL_SURFACE2);
    sk_line(hdc, 0, sb.y, w, sb.y, SK_COL_BORDER, 1);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "Keys: [1-5] Workspace  [N] +Pane  [X] -Pane  [L] Layout  [F] Freeze/Thaw  [Tab] Focus  |  B8G_DISPLAY_V1: %u surfaces (%u active, %u sleep, %u ckpt)  Commits: %llu",
             stats.active_surfaces, stats.surfaces_active, stats.surfaces_display_sleep,
             stats.surfaces_checkpointed, (unsigned long long)stats.total_commits);
    sk_text(hdc, app->font_small, sk_rect(10, sb.y, w - 20, WM_STATUSBAR_H),
            buf, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void wm_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    if (msg != WM_LBUTTONDOWN) return;
    WmState* st = (WmState*)app->user;
    wm_ensure_init(app, st);

    for (int i = 0; i < WM_WS_COUNT; i++) {
        if (sk_pt_in_rect(x, y, st->ws_rects[i])) {
            st->active_ws = i;
            return;
        }
    }
    if (sk_pt_in_rect(x, y, st->btn_layout)) {
        WmWorkspace* ws = &st->ws[st->active_ws];
        ws->layout = (WmLayout)((ws->layout + 1) % 3);
        return;
    }
    if (sk_pt_in_rect(x, y, st->btn_add_pane)) {
        wm_add_dynamic_pane(app, st);
        return;
    }
    if (sk_pt_in_rect(x, y, st->btn_rem_pane)) {
        wm_remove_focused_pane(st);
        return;
    }
    if (sk_pt_in_rect(x, y, st->btn_freeze)) {
        wm_toggle_freeze_focused(app, st);
        return;
    }

    WmWorkspace* ws = &st->ws[st->active_ws];
    for (int i = 0; i < ws->pane_count; i++) {
        if (sk_pt_in_rect(x, y, ws->panes[i].rect)) {
            ws->focused_pane = i;
            sectium_surface_thaw(&app->display, ws->panes[i].surface_id, app->now_ms);
            uint64_t seq = 0;
            sectium_surface_commit(&app->display, ws->panes[i].surface_id, app->now_ms, &seq);
            return;
        }
    }
}

static void wm_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_KEYDOWN) return;
    WmState* st = (WmState*)app->user;
    wm_ensure_init(app, st);
    WmWorkspace* ws = &st->ws[st->active_ws];

    if (wp >= '1' && wp <= '5') {
        st->active_ws = (int)(wp - '1');
    } else if (wp == 'N') {
        wm_add_dynamic_pane(app, st);
    } else if (wp == 'X' || wp == VK_DELETE) {
        wm_remove_focused_pane(st);
    } else if (wp == 'L') {
        ws->layout = (WmLayout)((ws->layout + 1) % 3);
    } else if (wp == 'F') {
        wm_toggle_freeze_focused(app, st);
    } else if (wp == VK_TAB || wp == VK_RIGHT || wp == VK_DOWN) {
        if (ws->pane_count > 0) {
            ws->focused_pane = (ws->focused_pane + 1) % ws->pane_count;
        }
    } else if (wp == VK_LEFT || wp == VK_UP) {
        if (ws->pane_count > 0) {
            ws->focused_pane = (ws->focused_pane + ws->pane_count - 1) % ws->pane_count;
        }
    }
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show) {
    (void)hinst;
    (void)hprev;
    (void)cmdline;
    (void)show;

    static WmState state;
    memset(&state, 0, sizeof(state));

    static SkApp app;
    memset(&app, 0, sizeof(app));
    app.on_render = wm_render;
    app.on_mouse  = wm_mouse;
    app.on_key    = wm_key;
    app.user      = &state;

    return sk_run(
        &app,
        "SectiumTilingWmShell",
        "Sectium Shell 03 — Scriptable Tiling WM (B8G_DISPLAY_V1)",
        1360,
        820,
        false,
        false,
        250
    );
}
