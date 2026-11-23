#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 08 — Compact Floating System Control Center / Tray Popover HUD
 * 380x520 | frameless = true | always_on_top = true | B8G_DISPLAY_V1 controls
 * ============================================================================ */

#define HUD_CARD_COUNT 5
#define HUD_BG_SURFACES 4

typedef struct HudState {
    bool     initialized;
    bool     audio_lease;
    bool     net_push_lease;
    bool     compute_lease;
    bool     freeze_idle_tabs;
    int      idle_timeout_sec; /* 5..120 */

    uint32_t bg_surfaces[HUD_BG_SURFACES];

    /* Drag states */
    bool     dragging_window;
    POINT    drag_cursor_start;
    RECT     drag_win_start;
    bool     dragging_slider;

    /* Hit-test rects */
    SkRect   header_rect;
    SkRect   close_rect;
    SkRect   card_rects[HUD_CARD_COUNT];
    SkRect   slider_track;
} HudState;

static void hud_apply_leases_and_idle(SkApp* app, HudState* st) {
    uint32_t mask = B8G_LEASE_NONE;
    if (st->audio_lease)    mask |= B8G_LEASE_AUDIO;
    if (st->net_push_lease) mask |= B8G_LEASE_NET_PUSH;
    if (st->compute_lease)  mask |= B8G_LEASE_COMPUTE;

    sectium_surface_set_leases(&app->display, app->root_surface_id, mask);

    uint64_t now = sk_now_ms();
    uint64_t idle_ms = (uint64_t)st->idle_timeout_sec * 1000ULL;
    uint64_t freeze_ms = idle_ms * 2ULL;

    for (int i = 0; i < HUD_BG_SURFACES; i++) {
        uint32_t sid = st->bg_surfaces[i];
        if (sid == 0) continue;

        /* Surface 0 holds audio lease if enabled; Surface 1 holds net push; 2 & 3 have no leases */
        uint32_t smask = B8G_LEASE_NONE;
        if (i == 0 && st->audio_lease)    smask |= B8G_LEASE_AUDIO;
        if (i == 1 && st->net_push_lease) smask |= B8G_LEASE_NET_PUSH;
        if (i == 0 && st->compute_lease)  smask |= B8G_LEASE_COMPUTE;
        sectium_surface_set_leases(&app->display, sid, smask);

        if (st->freeze_idle_tabs) {
            uint32_t out_state = 0;
            sectium_surface_evaluate_idle(&app->display, sid, now + freeze_ms + 5000ULL, idle_ms, freeze_ms, &out_state);
        } else {
            sectium_surface_thaw(&app->display, sid, now);
        }
    }
}

static void hud_ensure_init(SkApp* app, HudState* st) {
    if (st->initialized) return;
    st->initialized      = true;
    st->audio_lease      = true;
    st->net_push_lease   = true;
    st->compute_lease    = false;
    st->freeze_idle_tabs = true;
    st->idle_timeout_sec = 30;

    uint32_t worker_id = 0;
    sectium_actor_register(&app->display, B8G_ACTOR_WORKER, "tray-policy-daemon", &worker_id);

    for (int i = 0; i < HUD_BG_SURFACES; i++) {
        uint32_t sid = 0;
        if (sectium_surface_create(&app->display, worker_id, 1280, 720, B8G_COMP_TILING_2D, &sid) == B8G_DISPLAY_OK) {
            st->bg_surfaces[i] = sid;
            uint32_t bid = 0;
            sectium_actor_bind_stream(&app->display, worker_id, sid, B8G_STREAM_SURFACE, &bid);
            uint64_t seq = 0;
            sectium_surface_commit(&app->display, sid, app->start_ms, &seq);
        }
    }

    hud_apply_leases_and_idle(app, st);
}

static void hud_draw_toggle_card(
    HDC hdc,
    SkApp* app,
    SkRect r,
    const char* title,
    const char* subtitle,
    bool enabled,
    COLORREF accent
) {
    bool hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, r);
    COLORREF bg = hover ? SK_COL_SURFACE3 : SK_COL_SURFACE2;
    COLORREF bd = enabled ? accent : (hover ? SK_COL_ACCENT : SK_COL_BORDER);
    sk_round_rect(hdc, r, 8, bg, bd);

    sk_circle(hdc, r.x + 18, r.y + r.h / 2, 5, enabled ? accent : SK_COL_MUTED, SK_COL_BORDER);
    sk_text(hdc, app->font_bold,  sk_rect(r.x + 34, r.y + 8,  r.w - 102, 18), title,    SK_COL_TEXT,  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_text(hdc, app->font_small, sk_rect(r.x + 34, r.y + 27, r.w - 102, 16), subtitle, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Pill toggle switch */
    SkRect sw = sk_rect(r.x + r.w - 58, r.y + (r.h - 24) / 2, 46, 24);
    sk_round_rect(hdc, sw, 12, enabled ? accent : SK_COL_BG, enabled ? accent : SK_COL_BORDER);
    int knob_cx = enabled ? (sw.x + sw.w - 12) : (sw.x + 12);
    sk_circle(hdc, knob_cx, sw.y + 12, 8, SK_COL_TEXT, enabled ? accent : SK_COL_BORDER);
}

static void hud_on_render(SkApp* app, HDC hdc) {
    HudState* st = (HudState*)app->user;
    hud_ensure_init(app, st);

    int w = app->width;
    int h = app->height;

    /* Outer frameless container border */
    sk_round_rect(hdc, sk_rect(0, 0, w, h), 10, SK_COL_BG, SK_COL_BORDER);

    /* Draggable rounded dark header */
    st->header_rect = sk_rect(8, 8, w - 16, 44);
    sk_round_rect(hdc, st->header_rect, 8, SK_COL_SURFACE, SK_COL_BORDER);
    sk_circle(hdc, st->header_rect.x + 18, st->header_rect.y + 22, 5, SK_COL_CYAN, SK_COL_ACCENT);
    sk_text(hdc, app->font_bold, sk_rect(st->header_rect.x + 32, st->header_rect.y + 4, 220, 20),
            "Sectium Control HUD", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_text(hdc, app->font_small, sk_rect(st->header_rect.x + 32, st->header_rect.y + 23, 220, 16),
            "Drag header  |  Esc to close", SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Close [X] button */
    st->close_rect = sk_rect(st->header_rect.x + st->header_rect.w - 34, st->header_rect.y + 8, 26, 28);
    bool close_hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, st->close_rect);
    sk_round_rect(hdc, st->close_rect, 6, close_hover ? SK_COL_RED : SK_COL_SURFACE2, close_hover ? SK_COL_RED : SK_COL_BORDER);
    sk_text(hdc, app->font_bold, st->close_rect, "X", close_hover ? SK_COL_BG : SK_COL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* 5 Quick-Toggle Cards */
    int cy = 62;
    int ch = 52;
    int gap = 8;

    const char* titles[HUD_CARD_COUNT] = {
        "Audio Lease",
        "Net Push Lease",
        "Compute Lease",
        "Freeze Idle Tabs",
        "Always On Top"
    };
    const char* subs[HUD_CARD_COUNT] = {
        "B8G_LEASE_AUDIO (prevent audio sleep)",
        "B8G_LEASE_NET_PUSH (keep stream sockets)",
        "B8G_LEASE_COMPUTE (background workers)",
        "Auto-freeze & checkpoint idle surfaces",
        "Pin floating HUD via HWND_TOPMOST"
    };
    bool vals[HUD_CARD_COUNT] = {
        st->audio_lease,
        st->net_push_lease,
        st->compute_lease,
        st->freeze_idle_tabs,
        app->always_on_top
    };
    COLORREF accents[HUD_CARD_COUNT] = {
        SK_COL_GREEN,
        SK_COL_CYAN,
        SK_COL_PURPLE,
        SK_COL_AMBER,
        SK_COL_ACCENT
    };

    for (int i = 0; i < HUD_CARD_COUNT; i++) {
        st->card_rects[i] = sk_rect(12, cy + i * (ch + gap), w - 24, ch);
        hud_draw_toggle_card(hdc, app, st->card_rects[i], titles[i], subs[i], vals[i], accents[i]);
    }

    /* Draggable Interactive Slider: Idle Freeze Timeout (5s..120s) */
    int sy = cy + HUD_CARD_COUNT * (ch + gap) + 4;
    SkRect slider_card = sk_rect(12, sy, w - 24, 78);
    sk_round_rect(hdc, slider_card, 8, SK_COL_SURFACE, SK_COL_BORDER);

    sk_text(hdc, app->font_bold, sk_rect(slider_card.x + 12, slider_card.y + 10, 210, 18),
            "Idle Freeze Timeout", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    char val_badge[24];
    snprintf(val_badge, sizeof(val_badge), "%ds", st->idle_timeout_sec);
    sk_pill(hdc, app->font_mono, sk_rect(slider_card.x + slider_card.w - 64, slider_card.y + 8, 52, 22),
            val_badge, SK_COL_SURFACE2, SK_COL_ACCENT, SK_COL_ACCENT);

    st->slider_track = sk_rect(slider_card.x + 16, slider_card.y + 42, slider_card.w - 32, 8);
    sk_round_rect(hdc, st->slider_track, 4, SK_COL_BG, SK_COL_BORDER);

    float norm = (float)(st->idle_timeout_sec - 5) / (float)(120 - 5);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    int fill_w = (int)(norm * (float)st->slider_track.w);
    if (fill_w > 0) {
        sk_round_rect(hdc, sk_rect(st->slider_track.x, st->slider_track.y, fill_w, st->slider_track.h), 4, SK_COL_ACCENT, SK_COL_ACCENT);
    }

    int thumb_x = st->slider_track.x + fill_w;
    sk_circle(hdc, thumb_x, st->slider_track.y + st->slider_track.h / 2, 9, SK_COL_TEXT, SK_COL_ACCENT);

    sk_text(hdc, app->font_small, sk_rect(slider_card.x + 14, slider_card.y + 56, 60, 16), "5s", SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_text(hdc, app->font_small, sk_rect(slider_card.x + slider_card.w - 74, slider_card.y + 56, 60, 16), "120s", SK_COL_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* Live B8GDisplayStats Footer Bar */
    B8GDisplayStats stats;
    sectium_display_get_stats(&app->display, &stats);

    SkRect footer = sk_rect(12, h - 54, w - 24, 42);
    sk_round_rect(hdc, footer, 8, SK_COL_SURFACE, SK_COL_BORDER);

    char line1[96];
    char line2[96];
    snprintf(line1, sizeof(line1), "B8G_DISPLAY_V1  |  Surfaces:%u  Active:%u  Sleep:%u  Frozen:%u",
             stats.active_surfaces, stats.surfaces_active, stats.surfaces_display_sleep, stats.surfaces_frozen);
    snprintf(line2, sizeof(line2), "Actors:%u  Bindings:%u  Commits:%llu  TopMost:%s",
             stats.active_actors, stats.active_bindings, (unsigned long long)stats.total_commits,
             app->always_on_top ? "ON" : "OFF");

    sk_text(hdc, app->font_mono, sk_rect(footer.x + 10, footer.y + 4,  footer.w - 20, 16), line1, SK_COL_CYAN,  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_text(hdc, app->font_mono, sk_rect(footer.x + 10, footer.y + 21, footer.w - 20, 16), line2, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void hud_update_slider_from_x(SkApp* app, HudState* st, int x) {
    if (st->slider_track.w <= 0) return;
    float norm = (float)(x - st->slider_track.x) / (float)st->slider_track.w;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    int sec = 5 + (int)(norm * (float)(120 - 5) + 0.5f);
    if (sec < 5) sec = 5;
    if (sec > 120) sec = 120;
    if (sec != st->idle_timeout_sec) {
        st->idle_timeout_sec = sec;
        hud_apply_leases_and_idle(app, st);
    }
}

static void hud_on_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    HudState* st = (HudState*)app->user;
    hud_ensure_init(app, st);

    if (msg == WM_LBUTTONDOWN) {
        if (sk_pt_in_rect(x, y, st->close_rect)) {
            PostQuitMessage(0);
            return;
        }

        if (sk_pt_in_rect(x, y, st->header_rect)) {
            st->dragging_window = true;
            GetCursorPos(&st->drag_cursor_start);
            GetWindowRect(app->hwnd, &st->drag_win_start);
            return;
        }

        /* Slider hit area (generous vertical padding around track) */
        SkRect slider_hit = sk_rect(st->slider_track.x - 10, st->slider_track.y - 12, st->slider_track.w + 20, st->slider_track.h + 24);
        if (sk_pt_in_rect(x, y, slider_hit)) {
            st->dragging_slider = true;
            hud_update_slider_from_x(app, st, x);
            return;
        }

        for (int i = 0; i < HUD_CARD_COUNT; i++) {
            if (sk_pt_in_rect(x, y, st->card_rects[i])) {
                if (i == 0) {
                    st->audio_lease = !st->audio_lease;
                    hud_apply_leases_and_idle(app, st);
                } else if (i == 1) {
                    st->net_push_lease = !st->net_push_lease;
                    hud_apply_leases_and_idle(app, st);
                } else if (i == 2) {
                    st->compute_lease = !st->compute_lease;
                    hud_apply_leases_and_idle(app, st);
                } else if (i == 3) {
                    st->freeze_idle_tabs = !st->freeze_idle_tabs;
                    hud_apply_leases_and_idle(app, st);
                } else if (i == 4) {
                    app->always_on_top = !app->always_on_top;
                    SetWindowPos(
                        app->hwnd,
                        app->always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                        0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                    );
                }
                return;
            }
        }
    } else if (msg == WM_MOUSEMOVE) {
        if (st->dragging_window) {
            POINT pt;
            GetCursorPos(&pt);
            int nx = st->drag_win_start.left + (pt.x - st->drag_cursor_start.x);
            int ny = st->drag_win_start.top  + (pt.y - st->drag_cursor_start.y);
            SetWindowPos(app->hwnd, NULL, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (st->dragging_slider) {
            hud_update_slider_from_x(app, st, x);
        }
    } else if (msg == WM_LBUTTONUP) {
        st->dragging_window = false;
        st->dragging_slider = false;
    }
}

static void hud_on_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)app;
    (void)lp;
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        PostQuitMessage(0);
    }
}

static void hud_on_tick(SkApp* app, uint64_t now_ms) {
    (void)now_ms;
    HudState* st = (HudState*)app->user;
    hud_ensure_init(app, st);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst;
    (void)hPrev;
    (void)lpCmd;
    (void)nShow;

    HudState state;
    memset(&state, 0, sizeof(state));

    SkApp app;
    memset(&app, 0, sizeof(app));
    app.on_render = hud_on_render;
    app.on_mouse  = hud_on_mouse;
    app.on_key    = hud_on_key;
    app.on_tick   = hud_on_tick;
    app.user      = &state;

    return sk_run(
        &app,
        "SectiumTrayPopoverHudClass",
        "Sectium Control HUD",
        380,
        520,
        true,
        true,
        500
    );
}
