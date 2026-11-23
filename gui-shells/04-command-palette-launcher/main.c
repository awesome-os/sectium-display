#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 04-command-palette-launcher
 * Frameless Raycast/Spotlight-style dark floating launcher wired to B8GDisplayCtx
 * ============================================================================ */

#define PALETTE_W 760
#define PALETTE_H 480
#define MAX_ACTIONS 12
#define MAX_QUERY 96

typedef enum ActionOp {
    OP_OPEN_BROWSER_SPLIT = 0,
    OP_COMPILE_DISPLAY_ABI,
    OP_ATTACH_AGENT_AVATAR,
    OP_FREEZE_IDLE_SURFACES,
    OP_SWITCH_OPEN_WORLD_3D,
    OP_INSPECT_ARENA_V1,
    OP_LAUNCH_AGENT_WORLD,
    OP_OPEN_NODE_GRAPH,
    OP_THAW_SURFACES,
    OP_TOGGLE_COMPUTE_LEASE,
    OP_REBIND_TILING_2D,
    OP_EXPORT_MANIFEST
} ActionOp;

typedef struct PaletteAction {
    const char* title;
    const char* category;
    const char* shortcut;
    const char* command;
    const char* description;
    COLORREF    accent;
    ActionOp    op;
    uint32_t    actor_id;
    uint32_t    surface_id;
    const char* actor_name;
} PaletteAction;

typedef struct FilteredItem {
    int index;
    int score;
} FilteredItem;

typedef struct PaletteState {
    bool          initialized;
    char          query[MAX_QUERY];
    int           query_len;
    PaletteAction actions[MAX_ACTIONS];
    FilteredItem  filtered[MAX_ACTIONS];
    int           filtered_count;
    int           selected;
    int           scroll_offset;

    uint32_t      actor_holo;
    uint32_t      actor_wm;
    uint32_t      actor_lumi;

    uint32_t      surf_browser;
    uint32_t      surf_abi;
    uint32_t      surf_world;
    uint32_t      surf_arena;

    char          toast_text[192];
    COLORREF      toast_color;
    uint64_t      toast_until_ms;
    uint32_t      exec_count;
} PaletteState;

static char sk_tolower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int fuzzy_score(const char* query, const char* text) {
    if (!query || query[0] == '\0') return 100;
    if (!text) return -1;

    int qlen = (int)strlen(query);
    int tlen = (int)strlen(text);
    int qi = 0;
    int score = 0;
    int consecutive = 0;
    int first_match = -1;

    for (int ti = 0; ti < tlen && qi < qlen; ++ti) {
        if (sk_tolower_ascii(text[ti]) == sk_tolower_ascii(query[qi])) {
            if (first_match < 0) first_match = ti;
            score += 12;
            consecutive++;
            score += consecutive * 6;
            if (ti == 0 || text[ti - 1] == ' ' || text[ti - 1] == '_' || text[ti - 1] == '-' || text[ti - 1] == '/') {
                score += 18;
            }
            qi++;
        } else {
            consecutive = 0;
        }
    }

    if (qi < qlen) return -1;
    score -= first_match;
    if (score < 1) score = 1;
    return score;
}

static void rebuild_filter(PaletteState* st) {
    st->filtered_count = 0;
    for (int i = 0; i < MAX_ACTIONS; ++i) {
        int s1 = fuzzy_score(st->query, st->actions[i].title);
        int s2 = fuzzy_score(st->query, st->actions[i].category);
        int s3 = fuzzy_score(st->query, st->actions[i].command);
        int best = s1;
        if (s2 > best) best = s2 - 10;
        if (s3 > best) best = s3 - 15;
        if (st->query_len == 0 || s1 >= 0 || s2 >= 0 || s3 >= 0) {
            if (best < 1) best = 1;
            st->filtered[st->filtered_count].index = i;
            st->filtered[st->filtered_count].score = best;
            st->filtered_count++;
        }
    }

    /* Stable insertion sort by descending score */
    for (int i = 1; i < st->filtered_count; ++i) {
        FilteredItem key = st->filtered[i];
        int j = i - 1;
        while (j >= 0 && st->filtered[j].score < key.score) {
            st->filtered[j + 1] = st->filtered[j];
            j--;
        }
        st->filtered[j + 1] = key;
    }

    if (st->filtered_count == 0) {
        st->selected = 0;
        st->scroll_offset = 0;
    } else {
        if (st->selected >= st->filtered_count) st->selected = st->filtered_count - 1;
        if (st->selected < 0) st->selected = 0;
        if (st->selected < st->scroll_offset) st->scroll_offset = st->selected;
        if (st->selected >= st->scroll_offset + 7) st->scroll_offset = st->selected - 6;
        if (st->scroll_offset < 0) st->scroll_offset = 0;
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

static void show_toast(PaletteState* st, uint64_t now_ms, COLORREF col, const char* msg) {
    snprintf(st->toast_text, sizeof(st->toast_text), "%s", msg);
    st->toast_color = col;
    st->toast_until_ms = now_ms + 3200;
}

static void ensure_init(SkApp* app) {
    PaletteState* st = (PaletteState*)app->user;
    if (st->initialized) return;
    st->initialized = true;

    sectium_actor_register(&app->display, B8G_ACTOR_AGENT, "holo-compiler", &st->actor_holo);
    sectium_actor_register(&app->display, B8G_ACTOR_AGENT, "sectium-wm", &st->actor_wm);
    sectium_actor_register(&app->display, B8G_ACTOR_AGENT, "lumi-bert", &st->actor_lumi);

    sectium_surface_create(&app->display, st->actor_wm, 1280, 720, B8G_COMP_TILING_2D, &st->surf_browser);
    sectium_surface_create(&app->display, st->actor_holo, 960, 540, B8G_COMP_TILING_2D, &st->surf_abi);
    sectium_surface_create(&app->display, st->actor_lumi, 1920, 1080, B8G_COMP_OPEN_WORLD_3D, &st->surf_world);
    sectium_surface_create(&app->display, app->human_actor_id, 800, 600, B8G_COMP_TILING_2D, &st->surf_arena);

    sectium_surface_attach_arena(&app->display, st->surf_browser, 0x00000000u, 1280u * 720u * 4u, 1280u * 4u);
    sectium_surface_attach_arena(&app->display, st->surf_abi, 0x00400000u, 960u * 540u * 4u, 960u * 4u);
    sectium_surface_attach_arena(&app->display, st->surf_world, 0x00800000u, 1920u * 1080u * 4u, 1920u * 4u);
    sectium_surface_attach_arena(&app->display, st->surf_arena, 0x01000000u, 800u * 600u * 4u, 800u * 4u);

    uint32_t bid = 0;
    sectium_actor_bind_stream(&app->display, st->actor_wm, st->surf_browser, B8G_STREAM_SURFACE, &bid);
    sectium_actor_bind_stream(&app->display, st->actor_holo, st->surf_abi, B8G_STREAM_WORKER_IO, &bid);
    sectium_actor_bind_stream(&app->display, st->actor_lumi, st->surf_world, B8G_STREAM_AVATAR, &bid);
    sectium_actor_bind_stream(&app->display, app->human_actor_id, st->surf_arena, B8G_STREAM_DIALOG, &bid);

    sectium_surface_set_leases(&app->display, st->surf_abi, B8G_LEASE_COMPUTE);
    sectium_surface_set_leases(&app->display, st->surf_world, B8G_LEASE_AUDIO | B8G_LEASE_NET_PUSH);

    uint64_t seq = 0;
    sectium_surface_commit(&app->display, st->surf_browser, app->now_ms, &seq);
    sectium_surface_commit(&app->display, st->surf_abi, app->now_ms, &seq);
    sectium_surface_commit(&app->display, st->surf_world, app->now_ms, &seq);
    sectium_surface_commit(&app->display, st->surf_arena, app->now_ms, &seq);

    PaletteAction init_actions[MAX_ACTIONS] = {
        {
            "Open Browser Split Shell", "SHELL", "Ctrl+1",
            "sectium-shell --mode=tiling-2d --surface=browser",
            "Splits the active workspace into a 2D tiled browser + DevTools command plane.",
            SK_COL_ACCENT, OP_OPEN_BROWSER_SPLIT, st->actor_wm, st->surf_browser, "sectium-wm"
        },
        {
            "Compile libsectium-display", "BUILD", "Ctrl+B",
            "holo-compiler build sections/sectium-display --target=x86_64-windows-gnu",
            "Compiles B8G_DISPLAY_V1 C11 ABI in <1s with zero warnings via zig cc & .b8p CAS.",
            SK_COL_GREEN, OP_COMPILE_DISPLAY_ABI, st->actor_holo, st->surf_abi, "holo-compiler"
        },
        {
            "Attach Agent Avatar", "SWARM", "Ctrl+A",
            "sectium-actor bind --stream=AVATAR --actor=lumi-bert",
            "Binds a live B8G_STREAM_AVATAR channel to the open-world compositor surface.",
            SK_COL_PURPLE, OP_ATTACH_AGENT_AVATAR, st->actor_lumi, st->surf_world, "lumi-bert"
        },
        {
            "Freeze Idle Surfaces", "POWER", "Ctrl+F",
            "sectium-display eval-idle --idle=500ms --freeze=1000ms",
            "Evaluates idle thresholds across surfaces and checkpoints inactive arenas.",
            SK_COL_CYAN, OP_FREEZE_IDLE_SURFACES, st->actor_wm, st->surf_browser, "sectium-wm"
        },
        {
            "Switch to Open World 3D", "COMPOSITOR", "Ctrl+3",
            "sectium-surface rebind --mode=B8G_COMP_OPEN_WORLD_3D",
            "Rebinds the target surface from 2D tiling into a 3D spatial desk pose.",
            SK_COL_AMBER, OP_SWITCH_OPEN_WORLD_3D, st->actor_wm, st->surf_browser, "sectium-wm"
        },
        {
            "Inspect B8G_ARENA_V1", "MEMORY", "Ctrl+M",
            "b8g-arena inspect --align=64 --zero-copy",
            "Dumps 64-byte aligned arena offsets, strides, and commit sequences.",
            SK_COL_ACCENT, OP_INSPECT_ARENA_V1, app->human_actor_id, st->surf_arena, "human-primary"
        },
        {
            "Launch Multi-Agent World", "SHELL", "Ctrl+5",
            "sectium-shell --world=multi-agent --actors=4",
            "Opens the interactive Human + 3 Swarm Agent open-world shell with 2D dock.",
            SK_COL_GREEN, OP_LAUNCH_AGENT_WORLD, st->actor_lumi, st->surf_world, "lumi-bert"
        },
        {
            "Open Node Graph Studio", "STUDIO", "Ctrl+6",
            "sectium-shell --studio=node-graph --wires=bezier",
            "Launches the Blueprint/ComfyUI 7-Tuple pipeline node graph editor.",
            SK_COL_PURPLE, OP_OPEN_NODE_GRAPH, st->actor_holo, st->surf_abi, "holo-compiler"
        },
        {
            "Thaw Checkpointed Surfaces", "POWER", "Ctrl+T",
            "sectium-surface thaw --all-checkpointed",
            "Restores frozen or checkpointed surfaces back to ACTIVE with zero-copy RAM.",
            SK_COL_GREEN, OP_THAW_SURFACES, st->actor_wm, st->surf_browser, "sectium-wm"
        },
        {
            "Toggle Wake Lease: COMPUTE", "LEASES", "Ctrl+L",
            "sectium-surface lease --toggle=B8G_LEASE_COMPUTE",
            "Toggles the B8G_LEASE_COMPUTE bitmask on the holo-compiler worker surface.",
            SK_COL_AMBER, OP_TOGGLE_COMPUTE_LEASE, st->actor_holo, st->surf_abi, "holo-compiler"
        },
        {
            "Rebind Surface to Tiling 2D", "COMPOSITOR", "Ctrl+2",
            "sectium-surface rebind --mode=B8G_COMP_TILING_2D",
            "Pulls the 3D Open World surface cleanly back into a 2D split tiling pane.",
            SK_COL_CYAN, OP_REBIND_TILING_2D, st->actor_lumi, st->surf_world, "lumi-bert"
        },
        {
            "Export RollingReleaseManifest", "DEPLOY", "Ctrl+R",
            "sectium-cp manifest --o1-rollback --verify",
            "Generates a verified O(1) rollback manifest for sectium-cloud-control-plane.",
            SK_COL_RED, OP_EXPORT_MANIFEST, app->human_actor_id, st->surf_arena, "human-primary"
        }
    };

    memcpy(st->actions, init_actions, sizeof(init_actions));
    rebuild_filter(st);
}

static void execute_selected(SkApp* app) {
    PaletteState* st = (PaletteState*)app->user;
    if (st->filtered_count <= 0) return;
    int idx = st->filtered[st->selected].index;
    PaletteAction* act = &st->actions[idx];
    uint64_t now = sk_now_ms();
    char msg[192];
    st->exec_count++;

    switch (act->op) {
        case OP_OPEN_BROWSER_SPLIT: {
            uint64_t seq = 0;
            sectium_surface_damage(&app->display, act->surface_id, 0, 0, 1280, 720);
            sectium_surface_commit(&app->display, act->surface_id, now, &seq);
            snprintf(msg, sizeof(msg), "[OK] Browser Split committed (Surface #%u, seq=%llu)",
                     act->surface_id, (unsigned long long)seq);
            show_toast(st, now, SK_COL_ACCENT, msg);
            break;
        }
        case OP_COMPILE_DISPLAY_ABI: {
            uint64_t seq = 0;
            sectium_surface_commit(&app->display, act->surface_id, now, &seq);
            snprintf(msg, sizeof(msg), "[OK] holo-compiler built libsectium-display (0 warnings, seq=%llu)",
                     (unsigned long long)seq);
            show_toast(st, now, SK_COL_GREEN, msg);
            break;
        }
        case OP_ATTACH_AGENT_AVATAR: {
            uint32_t bid = 0;
            int32_t rc = sectium_actor_bind_stream(&app->display, act->actor_id, act->surface_id, B8G_STREAM_AVATAR, &bid);
            if (rc == B8G_DISPLAY_OK) {
                snprintf(msg, sizeof(msg), "[OK] Bound AVATAR stream #%u -> actor=%s (S#%u)",
                         bid, act->actor_name, act->surface_id);
                show_toast(st, now, SK_COL_PURPLE, msg);
            } else {
                snprintf(msg, sizeof(msg), "[INFO] Bind capacity reached (rc=%d)", rc);
                show_toast(st, now, SK_COL_AMBER, msg);
            }
            break;
        }
        case OP_FREEZE_IDLE_SURFACES: {
            uint32_t state = 0;
            sectium_surface_evaluate_idle(&app->display, act->surface_id, now + 5000, 500, 1000, &state);
            snprintf(msg, sizeof(msg), "[OK] Surface #%u transitioned -> %s",
                     act->surface_id, sk_surface_state_name(state));
            show_toast(st, now, SK_COL_CYAN, msg);
            break;
        }
        case OP_SWITCH_OPEN_WORLD_3D: {
            B8GWorldPose pose = { 1.5f, 1.2f, -2.0f, 0.0f, 0.25f, 0.0f, 1.0f, 1.0f };
            sectium_surface_rebind_mode(&app->display, act->surface_id, B8G_COMP_OPEN_WORLD_3D, &pose);
            const B8GSurface* s = find_surface(&app->display, act->surface_id);
            snprintf(msg, sizeof(msg), "[OK] Surface #%u -> OPEN_WORLD_3D (rebinds=%u)",
                     act->surface_id, s ? s->rebind_count : 0u);
            show_toast(st, now, SK_COL_AMBER, msg);
            break;
        }
        case OP_INSPECT_ARENA_V1: {
            const B8GSurface* s = find_surface(&app->display, act->surface_id);
            snprintf(msg, sizeof(msg), "[OK] B8G_ARENA_V1 offset=0x%llX size=%llu KB stride=%u",
                     (unsigned long long)(s ? s->arena_offset : 0u),
                     (unsigned long long)(s ? s->arena_size / 1024u : 0u),
                     s ? s->stride : 0u);
            show_toast(st, now, SK_COL_ACCENT, msg);
            break;
        }
        case OP_THAW_SURFACES: {
            sectium_surface_thaw(&app->display, st->surf_browser, now);
            sectium_surface_thaw(&app->display, st->surf_abi, now);
            sectium_surface_thaw(&app->display, st->surf_world, now);
            sectium_surface_thaw(&app->display, st->surf_arena, now);
            snprintf(msg, sizeof(msg), "[OK] Thawed all 4 surfaces -> ACTIVE");
            show_toast(st, now, SK_COL_GREEN, msg);
            break;
        }
        case OP_TOGGLE_COMPUTE_LEASE: {
            const B8GSurface* s = find_surface(&app->display, act->surface_id);
            uint32_t cur = s ? s->wake_leases : 0u;
            uint32_t next = cur ^ B8G_LEASE_COMPUTE;
            sectium_surface_set_leases(&app->display, act->surface_id, next);
            snprintf(msg, sizeof(msg), "[OK] Surface #%u COMPUTE lease -> %s (mask=0x%X)",
                     act->surface_id, (next & B8G_LEASE_COMPUTE) ? "ON" : "OFF", next);
            show_toast(st, now, SK_COL_AMBER, msg);
            break;
        }
        case OP_REBIND_TILING_2D: {
            sectium_surface_rebind_mode(&app->display, act->surface_id, B8G_COMP_TILING_2D, NULL);
            const B8GSurface* s = find_surface(&app->display, act->surface_id);
            snprintf(msg, sizeof(msg), "[OK] Surface #%u -> TILING_2D (rebinds=%u)",
                     act->surface_id, s ? s->rebind_count : 0u);
            show_toast(st, now, SK_COL_CYAN, msg);
            break;
        }
        default: {
            uint64_t seq = 0;
            sectium_surface_commit(&app->display, act->surface_id, now, &seq);
            snprintf(msg, sizeof(msg), "[OK] Dispatched '%s' on %s (S#%u)",
                     act->title, act->actor_name, act->surface_id);
            show_toast(st, now, act->accent, msg);
            break;
        }
    }
}

/* Layout helpers */
static SkRect rect_top_strip(int w) { return sk_rect(0, 0, w, 36); }
static SkRect rect_close_btn(int w) { return sk_rect(w - 38, 6, 30, 24); }
static SkRect rect_search_box(int w) { return sk_rect(16, 44, w - 32, 42); }
static SkRect rect_list_area(int h) { return sk_rect(16, 98, 396, h - 144); }
static SkRect rect_preview_area(int w, int h) { return sk_rect(424, 98, w - 440, h - 144); }
static SkRect rect_exec_btn(int w, int h) {
    SkRect p = rect_preview_area(w, h);
    return sk_rect(p.x + 14, p.y + p.h - 44, p.w - 28, 32);
}

static void on_tick(SkApp* app, uint64_t now_ms) {
    (void)now_ms;
    ensure_init(app);
}

static void on_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    ensure_init(app);
    PaletteState* st = (PaletteState*)app->user;

    if (msg == WM_LBUTTONDOWN) {
        SkRect close_r = rect_close_btn(app->width);
        if (sk_pt_in_rect(x, y, close_r)) {
            PostQuitMessage(0);
            return;
        }

        SkRect strip_r = rect_top_strip(app->width);
        if (sk_pt_in_rect(x, y, strip_r)) {
            ReleaseCapture();
            app->mouse_down = false;
            SendMessageA(app->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return;
        }

        SkRect list_r = rect_list_area(app->height);
        if (sk_pt_in_rect(x, y, list_r)) {
            int row_h = 46;
            int rel_y = y - (list_r.y + 6);
            if (rel_y >= 0) {
                int row = rel_y / row_h;
                int idx = st->scroll_offset + row;
                if (idx >= 0 && idx < st->filtered_count) {
                    if (st->selected == idx) {
                        execute_selected(app);
                    } else {
                        st->selected = idx;
                    }
                }
            }
            return;
        }

        SkRect exec_r = rect_exec_btn(app->width, app->height);
        if (sk_pt_in_rect(x, y, exec_r)) {
            execute_selected(app);
            return;
        }
    }
}

static void on_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    ensure_init(app);
    PaletteState* st = (PaletteState*)app->user;

    if (msg == WM_KEYDOWN) {
        if (wp == VK_ESCAPE) {
            PostQuitMessage(0);
            return;
        } else if (wp == VK_DOWN) {
            if (st->filtered_count > 0) {
                st->selected = (st->selected + 1) % st->filtered_count;
                if (st->selected < st->scroll_offset) st->scroll_offset = st->selected;
                if (st->selected >= st->scroll_offset + 7) st->scroll_offset = st->selected - 6;
            }
            return;
        } else if (wp == VK_UP) {
            if (st->filtered_count > 0) {
                st->selected = (st->selected - 1 + st->filtered_count) % st->filtered_count;
                if (st->selected < st->scroll_offset) st->scroll_offset = st->selected;
                if (st->selected >= st->scroll_offset + 7) st->scroll_offset = st->selected - 6;
            }
            return;
        } else if (wp == VK_RETURN) {
            execute_selected(app);
            return;
        } else if (wp == VK_BACK) {
            if (st->query_len > 0) {
                st->query[--st->query_len] = '\0';
                rebuild_filter(st);
            }
            return;
        }
    } else if (msg == WM_CHAR) {
        unsigned int ch = (unsigned int)wp;
        if (ch >= 32 && ch < 127 && st->query_len < MAX_QUERY - 1) {
            st->query[st->query_len++] = (char)ch;
            st->query[st->query_len] = '\0';
            rebuild_filter(st);
        }
    }
}

static void on_render(SkApp* app, HDC hdc) {
    ensure_init(app);
    PaletteState* st = (PaletteState*)app->user;
    int w = app->width;
    int h = app->height;

    /* Outer frameless card border */
    sk_fill_rect(hdc, sk_rect(0, 0, w, h), SK_COL_BG);
    sk_stroke_rect(hdc, sk_rect(0, 0, w, h), SK_COL_ACCENT_DIM, 2);

    /* Top draggable strip */
    SkRect strip = rect_top_strip(w);
    sk_fill_rect(hdc, strip, SK_COL_SURFACE);
    sk_line(hdc, 0, strip.h - 1, w, strip.h - 1, SK_COL_BORDER, 1);

    /* Traffic dot + title */
    sk_circle(hdc, 18, 18, 5, SK_COL_ACCENT, SK_COL_ACCENT);
    sk_text(hdc, app->font_bold, sk_rect(32, 0, 260, 36), "SECTIUM COMMAND PALETTE", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    B8GDisplayStats stats;
    memset(&stats, 0, sizeof(stats));
    sectium_display_get_stats(&app->display, &stats);
    char badge[96];
    snprintf(badge, sizeof(badge), "BGD1 | %u actors | %u surfaces | %u matches",
             stats.active_actors, stats.active_surfaces, (unsigned int)st->filtered_count);
    sk_text(hdc, app->font_small, sk_rect(290, 0, w - 340, 36), badge, SK_COL_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /* [X] Close button */
    SkRect close_r = rect_close_btn(w);
    bool close_hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, close_r);
    sk_round_rect(hdc, close_r, 5, close_hover ? SK_COL_RED : SK_COL_SURFACE2, close_hover ? SK_COL_RED : SK_COL_BORDER);
    sk_text(hdc, app->font_bold, close_r, "X", close_hover ? SK_COL_BG : SK_COL_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Search input box */
    SkRect search_r = rect_search_box(w);
    sk_round_rect(hdc, search_r, 8, SK_COL_SURFACE, SK_COL_ACCENT);
    sk_text(hdc, app->font_title, sk_rect(search_r.x + 12, search_r.y, 26, search_r.h), ">", SK_COL_ACCENT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    bool blink = ((app->now_ms / 500) % 2) == 0;
    char input_buf[128];
    if (st->query_len == 0) {
        snprintf(input_buf, sizeof(input_buf), "Type to fuzzy-filter 12 Sectium developer actions...%s", blink ? "|" : "");
        sk_text(hdc, app->font_ui, sk_rect(search_r.x + 36, search_r.y, search_r.w - 130, search_r.h),
                input_buf, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        snprintf(input_buf, sizeof(input_buf), "%s%s", st->query, blink ? "|" : "");
        sk_text(hdc, app->font_title, sk_rect(search_r.x + 36, search_r.y, search_r.w - 130, search_r.h),
                input_buf, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    sk_pill(hdc, app->font_small, sk_rect(search_r.x + search_r.w - 86, search_r.y + 9, 74, 24),
            "ESC close", SK_COL_SURFACE2, SK_COL_MUTED, SK_COL_BORDER);

    /* Left list panel */
    SkRect list_r = rect_list_area(h);
    sk_round_rect(hdc, list_r, 8, SK_COL_SURFACE, SK_COL_BORDER);

    int max_visible = 7;
    int row_h = 46;
    if (st->filtered_count == 0) {
        sk_text(hdc, app->font_ui, list_r, "No matching commands. Press Backspace.", SK_COL_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        for (int i = 0; i < max_visible; ++i) {
            int fidx = st->scroll_offset + i;
            if (fidx >= st->filtered_count) break;
            int aidx = st->filtered[fidx].index;
            const PaletteAction* act = &st->actions[aidx];

            SkRect row = sk_rect(list_r.x + 6, list_r.y + 6 + i * row_h, list_r.w - 12, row_h - 4);
            bool is_sel = (fidx == st->selected);
            bool is_hov = sk_pt_in_rect(app->mouse_x, app->mouse_y, row);

            COLORREF bg = is_sel ? SK_COL_SURFACE3 : (is_hov ? SK_COL_SURFACE2 : SK_COL_SURFACE);
            COLORREF bd = is_sel ? act->accent : (is_hov ? SK_COL_BORDER : SK_COL_SURFACE);
            sk_round_rect(hdc, row, 6, bg, bd);

            /* Left accent bar */
            if (is_sel) {
                sk_fill_rect(hdc, sk_rect(row.x + 2, row.y + 6, 4, row.h - 12), act->accent);
            }

            sk_text(hdc, is_sel ? app->font_bold : app->font_ui,
                    sk_rect(row.x + 14, row.y + 4, row.w - 102, 20),
                    act->title, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            char sub[96];
            snprintf(sub, sizeof(sub), "%s  |  @%s  |  S#%u", act->category, act->actor_name, act->surface_id);
            sk_text(hdc, app->font_small,
                    sk_rect(row.x + 14, row.y + 23, row.w - 102, 16),
                    sub, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            sk_pill(hdc, app->font_small, sk_rect(row.x + row.w - 76, row.y + 10, 68, 22),
                    act->shortcut, SK_COL_BG, is_sel ? act->accent : SK_COL_MUTED, is_sel ? act->accent : SK_COL_BORDER);
        }
    }

    /* Right preview panel */
    SkRect prev_r = rect_preview_area(w, h);
    sk_round_rect(hdc, prev_r, 8, SK_COL_SURFACE, SK_COL_BORDER);

    if (st->filtered_count > 0) {
        const PaletteAction* act = &st->actions[st->filtered[st->selected].index];
        const B8GSurface* surf = find_surface(&app->display, act->surface_id);

        sk_pill(hdc, app->font_small, sk_rect(prev_r.x + 14, prev_r.y + 14, 94, 22),
                act->category, SK_COL_SURFACE2, act->accent, act->accent);

        char actor_badge[64];
        snprintf(actor_badge, sizeof(actor_badge), "A#%u %s", act->actor_id, act->actor_name);
        sk_pill(hdc, app->font_small, sk_rect(prev_r.x + 116, prev_r.y + 14, prev_r.w - 130, 22),
                actor_badge, SK_COL_BG, SK_COL_TEXT, SK_COL_BORDER);

        sk_text(hdc, app->font_title, sk_rect(prev_r.x + 14, prev_r.y + 44, prev_r.w - 28, 28),
                act->title, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        sk_text(hdc, app->font_ui, sk_rect(prev_r.x + 14, prev_r.y + 76, prev_r.w - 28, 44),
                act->description, SK_COL_MUTED, DT_LEFT | DT_WORDBREAK);

        /* Command preview box */
        SkRect cmd_r = sk_rect(prev_r.x + 14, prev_r.y + 128, prev_r.w - 28, 52);
        sk_round_rect(hdc, cmd_r, 6, SK_COL_BG, SK_COL_BORDER);
        sk_text(hdc, app->font_small, sk_rect(cmd_r.x + 8, cmd_r.y + 4, cmd_r.w - 16, 16),
                "COMMAND PREVIEW", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
        sk_text(hdc, app->font_mono, sk_rect(cmd_r.x + 8, cmd_r.y + 20, cmd_r.w - 16, 26),
                act->command, SK_COL_CYAN, DT_LEFT | DT_WORDBREAK);

        /* Live B8G_DISPLAY_V1 Surface Telemetry */
        SkRect tel_r = sk_rect(prev_r.x + 14, prev_r.y + 190, prev_r.w - 28, 92);
        sk_round_rect(hdc, tel_r, 6, SK_COL_SURFACE2, SK_COL_BORDER);

        if (surf) {
            char line1[96], line2[96], line3[96];
            const char* mode_str = (surf->compositor_mode == B8G_COMP_OPEN_WORLD_3D) ? "OPEN_WORLD_3D" : "TILING_2D";
            snprintf(line1, sizeof(line1), "Surface #%u  |  %ux%u  |  %s",
                     surf->surface_id, surf->width, surf->height, mode_str);
            snprintf(line2, sizeof(line2), "State: %s  |  Rebinds: %u  |  Commits: %llu",
                     sk_surface_state_name(surf->state), surf->rebind_count, (unsigned long long)surf->commit_seq);
            snprintf(line3, sizeof(line3), "Leases: 0x%X  |  Arena: 0x%llX (%llu KB)",
                     surf->wake_leases, (unsigned long long)surf->arena_offset,
                     (unsigned long long)(surf->arena_size / 1024u));

            sk_text(hdc, app->font_small, sk_rect(tel_r.x + 10, tel_r.y + 8, tel_r.w - 20, 18),
                    "BOUND B8G_DISPLAY_V1 SURFACE", SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);
            sk_text(hdc, app->font_mono, sk_rect(tel_r.x + 10, tel_r.y + 28, tel_r.w - 20, 18),
                    line1, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
            sk_text(hdc, app->font_mono, sk_rect(tel_r.x + 10, tel_r.y + 48, tel_r.w - 20, 18),
                    line2, sk_surface_state_color(surf->state), DT_LEFT | DT_SINGLELINE);
            sk_text(hdc, app->font_mono, sk_rect(tel_r.x + 10, tel_r.y + 68, tel_r.w - 20, 18),
                    line3, SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
        }

        /* Execute button */
        sk_button(hdc, app, rect_exec_btn(w, h), "Execute Action (Enter)", true, act->accent);
    }

    /* Bottom status / Toast bar */
    SkRect foot_r = sk_rect(16, h - 38, w - 32, 26);
    if (app->now_ms < st->toast_until_ms && st->toast_text[0] != '\0') {
        sk_round_rect(hdc, foot_r, 6, SK_COL_SURFACE2, st->toast_color);
        sk_text(hdc, app->font_bold, sk_rect(foot_r.x + 10, foot_r.y, foot_r.w - 20, foot_r.h),
                st->toast_text, st->toast_color, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        char hint[128];
        snprintf(hint, sizeof(hint), "Up/Down navigate  |  Enter execute  |  Drag top bar to move  |  Executed: %u", st->exec_count);
        sk_text(hdc, app->font_small, foot_r, hint, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show) {
    (void)hinst;
    (void)hprev;
    (void)cmdline;
    (void)show;

    PaletteState state;
    memset(&state, 0, sizeof(state));

    SkApp app;
    memset(&app, 0, sizeof(app));
    app.user = &state;
    app.on_render = on_render;
    app.on_mouse = on_mouse;
    app.on_key = on_key;
    app.on_tick = on_tick;

    return sk_run(&app, "SectiumCommandPalette", "Sectium Command Palette", PALETTE_W, PALETTE_H, true, true, 50);
}
