#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 06-node-graph-studio-shell
 * Blender / ComfyUI / Blueprint Node Graph Studio wired to B8GDisplayCtx
 * ============================================================================ */

#define STUDIO_W 1240
#define STUDIO_H 760
#define NUM_NODES 5
#define NUM_WIRES 6
#define MAX_PORTS 3

typedef struct GraphNode {
    const char* title;
    const char* subtitle;
    const char* actor_kind_str;
    COLORREF    accent;
    int         x;
    int         y;
    int         w;
    int         h;
    int         in_count;
    const char* in_ports[MAX_PORTS];
    int         out_count;
    const char* out_ports[MAX_PORTS];
    uint32_t    actor_id;
    uint32_t    surface_id;
    uint32_t    binding_id;
} GraphNode;

typedef struct GraphWire {
    int      from_node;
    int      from_port;
    int      to_node;
    int      to_port;
    COLORREF col;
} GraphWire;

typedef struct StudioState {
    bool      initialized;
    GraphNode nodes[NUM_NODES];
    GraphWire wires[NUM_WIRES];

    int       pan_x;
    int       pan_y;
    bool      panning;
    int       pan_start_mx;
    int       pan_start_my;
    int       pan_orig_x;
    int       pan_orig_y;

    int       selected_node;
    int       dragging_node;
    int       drag_off_x;
    int       drag_off_y;

    uint32_t  tick_counter;
} StudioState;

static const B8GSurface* find_surface(const B8GDisplayCtx* ctx, uint32_t surface_id) {
    for (uint32_t i = 0; i < B8G_DISPLAY_MAX_SURFACES; ++i) {
        if (ctx->surfaces[i].active && ctx->surfaces[i].surface_id == surface_id) {
            return &ctx->surfaces[i];
        }
    }
    return NULL;
}

static void ensure_init(SkApp* app) {
    StudioState* st = (StudioState*)app->user;
    if (st->initialized) return;
    st->initialized = true;

    st->selected_node = 0;
    st->dragging_node = -1;
    st->pan_x = 24;
    st->pan_y = 20;

    GraphNode init_nodes[NUM_NODES] = {
        {
            "Human Actor", "B8G_ACTOR_HUMAN", "HUMAN", SK_COL_ACCENT,
            40, 140, 196, 124,
            0, { NULL, NULL, NULL },
            2, { "Intent", "Pose", NULL },
            0, 0, 0
        },
        {
            "7-Tuple Stream", "Readable -> Transform", "WORKER", SK_COL_CYAN,
            290, 90, 206, 140,
            2, { "Intent", "Pose", NULL },
            2, { "EventStream", "Diagnostics", NULL },
            0, 0, 0
        },
        {
            "lumiBERT 2.8ms", "Auto-Repair Classifier", "AGENT", SK_COL_PURPLE,
            290, 320, 206, 128,
            1, { "Diagnostics", NULL, NULL },
            2, { "AutoPatch", "Score", NULL },
            0, 0, 0
        },
        {
            "holo-compiler", "Zig C11 + .b8p CAS", "AGENT", SK_COL_GREEN,
            560, 130, 206, 140,
            2, { "EventStream", "AutoPatch", NULL },
            2, { "B8P Pack", "ABI Bin", NULL },
            0, 0, 0
        },
        {
            "Sectium Surface", "B8G_ARENA_V1 Commit", "WORKER", SK_COL_AMBER,
            560, 340, 212, 152,
            3, { "B8P Pack", "ABI Bin", "Score" },
            1, { "FrameCommit", NULL, NULL },
            0, 0, 0
        }
    };
    memcpy(st->nodes, init_nodes, sizeof(init_nodes));

    GraphWire init_wires[NUM_WIRES] = {
        { 0, 0, 1, 0, SK_COL_ACCENT },
        { 0, 1, 1, 1, SK_COL_CYAN },
        { 1, 0, 3, 0, SK_COL_CYAN },
        { 1, 1, 2, 0, SK_COL_PURPLE },
        { 2, 0, 3, 1, SK_COL_PURPLE },
        { 3, 0, 4, 0, SK_COL_GREEN }
    };
    memcpy(st->wires, init_wires, sizeof(init_wires));

    uint32_t kinds[NUM_NODES] = {
        B8G_ACTOR_HUMAN, B8G_ACTOR_WORKER, B8G_ACTOR_AGENT, B8G_ACTOR_AGENT, B8G_ACTOR_WORKER
    };
    uint32_t streams[NUM_NODES] = {
        B8G_STREAM_AVATAR, B8G_STREAM_DIALOG, B8G_STREAM_WORKER_IO, B8G_STREAM_WORKER_IO, B8G_STREAM_SURFACE
    };
    uint32_t leases[NUM_NODES] = {
        B8G_LEASE_AUDIO,
        B8G_LEASE_NET_PUSH,
        B8G_LEASE_COMPUTE,
        B8G_LEASE_COMPUTE | B8G_LEASE_NET_PUSH,
        B8G_LEASE_AUDIO | B8G_LEASE_COMPUTE
    };

    for (int i = 0; i < NUM_NODES; ++i) {
        GraphNode* n = &st->nodes[i];
        sectium_actor_register(&app->display, kinds[i], n->title, &n->actor_id);
        sectium_surface_create(
            &app->display,
            n->actor_id,
            800 + (uint32_t)i * 128u,
            600,
            (i == 0) ? B8G_COMP_OPEN_WORLD_3D : B8G_COMP_TILING_2D,
            &n->surface_id
        );
        sectium_surface_attach_arena(
            &app->display,
            n->surface_id,
            (uint64_t)(i + 1) * 0x00200000u,
            (uint64_t)(800 + i * 128) * 600u * 4u,
            (uint32_t)(800 + i * 128) * 4u
        );
        sectium_actor_bind_stream(&app->display, n->actor_id, n->surface_id, streams[i], &n->binding_id);
        sectium_surface_set_leases(&app->display, n->surface_id, leases[i]);
        uint64_t seq = 0;
        sectium_surface_commit(&app->display, n->surface_id, app->now_ms, &seq);
    }
}

/* Layout helpers */
static int inspector_w(void) { return 340; }
static SkRect rect_canvas(int w, int h) { return sk_rect(12, 48, w - inspector_w() - 28, h - 60); }
static SkRect rect_inspector(int w, int h) { return sk_rect(w - inspector_w() - 12, 48, inspector_w(), h - 60); }
static SkRect rect_reset_btn(int w) { return sk_rect(w - inspector_w() - 132, 8, 104, 24); }

static POINT node_in_port_pt(const StudioState* st, SkRect canvas, int node_idx, int port_idx) {
    const GraphNode* n = &st->nodes[node_idx];
    POINT p;
    p.x = canvas.x + st->pan_x + n->x;
    p.y = canvas.y + st->pan_y + n->y + 54 + port_idx * 24;
    return p;
}

static POINT node_out_port_pt(const StudioState* st, SkRect canvas, int node_idx, int port_idx) {
    const GraphNode* n = &st->nodes[node_idx];
    POINT p;
    p.x = canvas.x + st->pan_x + n->x + n->w;
    p.y = canvas.y + st->pan_y + n->y + 54 + port_idx * 24;
    return p;
}

static SkRect rect_lease_btn(int w, int h, int idx) {
    SkRect ins = rect_inspector(w, h);
    int bw = (ins.w - 36) / 3;
    return sk_rect(ins.x + 12 + idx * (bw + 6), ins.y + 206, bw, 30);
}

static SkRect rect_mode_btn(int w, int h) {
    SkRect ins = rect_inspector(w, h);
    return sk_rect(ins.x + 12, ins.y + 246, (ins.w - 30) / 2, 30);
}

static SkRect rect_commit_btn(int w, int h) {
    SkRect ins = rect_inspector(w, h);
    int bw = (ins.w - 30) / 2;
    return sk_rect(ins.x + 18 + bw, ins.y + 246, bw, 30);
}

static void on_tick(SkApp* app, uint64_t now_ms) {
    (void)now_ms;
    ensure_init(app);
    StudioState* st = (StudioState*)app->user;
    st->tick_counter++;
}

static void on_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    ensure_init(app);
    StudioState* st = (StudioState*)app->user;
    SkRect canvas = rect_canvas(app->width, app->height);

    if (msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) {
        if (sk_pt_in_rect(x, y, canvas)) {
            st->panning = true;
            st->pan_start_mx = x;
            st->pan_start_my = y;
            st->pan_orig_x = st->pan_x;
            st->pan_orig_y = st->pan_y;
            SetCapture(app->hwnd);
        }
        return;
    }

    if (msg == WM_RBUTTONUP || msg == WM_MBUTTONUP) {
        if (st->panning) {
            st->panning = false;
            ReleaseCapture();
        }
        return;
    }

    if (msg == WM_LBUTTONDOWN) {
        if (sk_pt_in_rect(x, y, rect_reset_btn(app->width))) {
            st->pan_x = 24;
            st->pan_y = 20;
            return;
        }

        /* Inspector wake lease toggles */
        GraphNode* sel = &st->nodes[st->selected_node];
        const B8GSurface* surf = find_surface(&app->display, sel->surface_id);
        uint32_t lease_bits[3] = { B8G_LEASE_AUDIO, B8G_LEASE_NET_PUSH, B8G_LEASE_COMPUTE };
        for (int i = 0; i < 3; ++i) {
            if (sk_pt_in_rect(x, y, rect_lease_btn(app->width, app->height, i))) {
                uint32_t cur = surf ? surf->wake_leases : 0u;
                sectium_surface_set_leases(&app->display, sel->surface_id, cur ^ lease_bits[i]);
                return;
            }
        }

        /* Inspector mode toggle & commit */
        if (sk_pt_in_rect(x, y, rect_mode_btn(app->width, app->height))) {
            uint32_t next_mode = (surf && surf->compositor_mode == B8G_COMP_OPEN_WORLD_3D)
                                     ? B8G_COMP_TILING_2D
                                     : B8G_COMP_OPEN_WORLD_3D;
            B8GWorldPose pose = { 0.0f, 1.0f, -1.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
            sectium_surface_rebind_mode(&app->display, sel->surface_id, next_mode, &pose);
            return;
        }
        if (sk_pt_in_rect(x, y, rect_commit_btn(app->width, app->height))) {
            uint64_t seq = 0;
            sectium_surface_damage(&app->display, sel->surface_id, 0, 0, 800, 600);
            sectium_surface_commit(&app->display, sel->surface_id, sk_now_ms(), &seq);
            return;
        }

        /* Hit-test nodes (top-most first) */
        if (sk_pt_in_rect(x, y, canvas)) {
            for (int i = NUM_NODES - 1; i >= 0; --i) {
                GraphNode* n = &st->nodes[i];
                SkRect nr = sk_rect(canvas.x + st->pan_x + n->x, canvas.y + st->pan_y + n->y, n->w, n->h);
                if (sk_pt_in_rect(x, y, nr)) {
                    st->selected_node = i;
                    st->dragging_node = i;
                    st->drag_off_x = x - nr.x;
                    st->drag_off_y = y - nr.y;
                    return;
                }
            }
        }
        return;
    }

    if (msg == WM_LBUTTONUP) {
        st->dragging_node = -1;
        return;
    }

    if (msg == WM_MOUSEMOVE) {
        if (st->panning) {
            st->pan_x = st->pan_orig_x + (x - st->pan_start_mx);
            st->pan_y = st->pan_orig_y + (y - st->pan_start_my);
        } else if (st->dragging_node >= 0 && app->mouse_down) {
            GraphNode* n = &st->nodes[st->dragging_node];
            n->x = (x - st->drag_off_x) - canvas.x - st->pan_x;
            n->y = (y - st->drag_off_y) - canvas.y - st->pan_y;
        }
    }
}

static void on_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    ensure_init(app);
    StudioState* st = (StudioState*)app->user;
    if (msg == WM_KEYDOWN) {
        if (wp == VK_TAB) {
            st->selected_node = (st->selected_node + 1) % NUM_NODES;
        } else if (wp == 'R') {
            st->pan_x = 24;
            st->pan_y = 20;
        }
    }
}

static void draw_bezier_wire(HDC hdc, POINT p0, POINT p3, COLORREF col, uint32_t tick) {
    int dx = p3.x - p0.x;
    int ctrl = (dx < 0 ? -dx : dx) / 2;
    if (ctrl < 60) ctrl = 60;
    if (ctrl > 180) ctrl = 180;

    POINT pts[4];
    pts[0] = p0;
    pts[1].x = p0.x + ctrl;
    pts[1].y = p0.y;
    pts[2].x = p3.x - ctrl;
    pts[2].y = p3.y;
    pts[3] = p3;

    HPEN pen = CreatePen(PS_SOLID, 3, col);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    PolyBezier(hdc, pts, 4);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);

    /* Animated data packet dot along the cubic Bezier */
    float t = (float)((tick % 60u)) / 60.0f;
    float u = 1.0f - t;
    float bx = u * u * u * (float)pts[0].x + 3.0f * u * u * t * (float)pts[1].x +
               3.0f * u * t * t * (float)pts[2].x + t * t * t * (float)pts[3].x;
    float by = u * u * u * (float)pts[0].y + 3.0f * u * u * t * (float)pts[1].y +
               3.0f * u * t * t * (float)pts[2].y + t * t * t * (float)pts[3].y;
    sk_circle(hdc, (int)bx, (int)by, 4, SK_COL_TEXT, col);
}

static void on_render(SkApp* app, HDC hdc) {
    ensure_init(app);
    StudioState* st = (StudioState*)app->user;
    int w = app->width;
    int h = app->height;

    B8GDisplayStats stats;
    memset(&stats, 0, sizeof(stats));
    sectium_display_get_stats(&app->display, &stats);

    /* Top Toolbar */
    sk_fill_rect(hdc, sk_rect(0, 0, w, 40), SK_COL_SURFACE);
    sk_line(hdc, 0, 39, w, 39, SK_COL_BORDER, 1);
    sk_text(hdc, app->font_title, sk_rect(14, 0, 360, 40),
            "SECTIUM NODE GRAPH STUDIO", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    char pan_info[128];
    snprintf(pan_info, sizeof(pan_info),
             "Drag nodes (LMB)  |  Pan canvas (RMB / MMB)  |  Pan=(%d, %d)",
             st->pan_x, st->pan_y);
    sk_text(hdc, app->font_small, sk_rect(350, 0, w - inspector_w() - 500, 40),
            pan_info, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    sk_button(hdc, app, rect_reset_btn(w), "Reset View", false, SK_COL_ACCENT);

    /* Canvas */
    SkRect canvas = rect_canvas(w, h);
    sk_round_rect(hdc, canvas, 10, SK_COL_BG, SK_COL_BORDER);

    /* Clip to canvas interior */
    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, canvas.x + 2, canvas.y + 2, canvas.x + canvas.w - 2, canvas.y + canvas.h - 2);

    /* Pan-able grid */
    int step = 32;
    int ox = ((st->pan_x % step) + step) % step;
    int oy = ((st->pan_y % step) + step) % step;
    for (int gx = canvas.x + ox; gx < canvas.x + canvas.w; gx += step) {
        sk_line(hdc, gx, canvas.y, gx, canvas.y + canvas.h, SK_COL_SURFACE, 1);
    }
    for (int gy = canvas.y + oy; gy < canvas.y + canvas.h; gy += step) {
        sk_line(hdc, canvas.x, gy, canvas.x + canvas.w, gy, SK_COL_SURFACE, 1);
    }

    /* Draw Bezier wires */
    for (int i = 0; i < NUM_WIRES; ++i) {
        const GraphWire* gw = &st->wires[i];
        POINT p0 = node_out_port_pt(st, canvas, gw->from_node, gw->from_port);
        POINT p3 = node_in_port_pt(st, canvas, gw->to_node, gw->to_port);
        draw_bezier_wire(hdc, p0, p3, gw->col, st->tick_counter + (uint32_t)i * 11u);
    }

    /* Draw Nodes */
    for (int i = 0; i < NUM_NODES; ++i) {
        const GraphNode* n = &st->nodes[i];
        const B8GSurface* s = find_surface(&app->display, n->surface_id);
        bool is_sel = (i == st->selected_node);

        SkRect nr = sk_rect(canvas.x + st->pan_x + n->x, canvas.y + st->pan_y + n->y, n->w, n->h);
        sk_round_rect(hdc, nr, 8, SK_COL_SURFACE, is_sel ? n->accent : SK_COL_BORDER);

        /* Header strip */
        SkRect hdr = sk_rect(nr.x + 1, nr.y + 1, nr.w - 2, 34);
        sk_fill_rect(hdc, hdr, is_sel ? SK_COL_SURFACE3 : SK_COL_SURFACE2);
        sk_fill_rect(hdc, sk_rect(nr.x + 1, nr.y + 1, 5, 34), n->accent);

        sk_text(hdc, app->font_bold, sk_rect(nr.x + 12, nr.y + 3, nr.w - 20, 17),
                n->title, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
        sk_text(hdc, app->font_small, sk_rect(nr.x + 12, nr.y + 19, nr.w - 20, 14),
                n->subtitle, n->accent, DT_LEFT | DT_SINGLELINE);

        /* Input ports */
        for (int p = 0; p < n->in_count; ++p) {
            POINT pt = node_in_port_pt(st, canvas, i, p);
            sk_circle(hdc, pt.x, pt.y, 6, SK_COL_BG, n->accent);
            sk_text(hdc, app->font_small, sk_rect(nr.x + 12, pt.y - 8, nr.w / 2 - 8, 16),
                    n->in_ports[p], SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        /* Output ports */
        for (int p = 0; p < n->out_count; ++p) {
            POINT pt = node_out_port_pt(st, canvas, i, p);
            sk_circle(hdc, pt.x, pt.y, 6, n->accent, SK_COL_TEXT);
            sk_text(hdc, app->font_small, sk_rect(nr.x + nr.w / 2, pt.y - 8, nr.w / 2 - 12, 16),
                    n->out_ports[p], SK_COL_TEXT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        /* Footer ID strip */
        char foot[64];
        snprintf(foot, sizeof(foot), "A#%u | S#%u | L:0x%X",
                 n->actor_id, n->surface_id, s ? s->wake_leases : 0u);
        sk_text(hdc, app->font_mono, sk_rect(nr.x + 10, nr.y + nr.h - 20, nr.w - 20, 16),
                foot, SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
    }

    RestoreDC(hdc, saved);

    /* Right-hand Inspector Panel */
    SkRect ins = rect_inspector(w, h);
    sk_round_rect(hdc, ins, 10, SK_COL_SURFACE, SK_COL_BORDER);

    const GraphNode* sel = &st->nodes[st->selected_node];
    const B8GSurface* surf = find_surface(&app->display, sel->surface_id);

    sk_text(hdc, app->font_small, sk_rect(ins.x + 14, ins.y + 12, ins.w - 28, 16),
            "NODE INSPECTOR", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
    sk_text(hdc, app->font_title, sk_rect(ins.x + 14, ins.y + 30, ins.w - 28, 26),
            sel->title, sel->accent, DT_LEFT | DT_SINGLELINE);
    sk_text(hdc, app->font_small, sk_rect(ins.x + 14, ins.y + 56, ins.w - 28, 16),
            sel->subtitle, SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

    /* Actor & Surface telemetry card */
    SkRect card = sk_rect(ins.x + 12, ins.y + 80, ins.w - 24, 106);
    sk_round_rect(hdc, card, 6, SK_COL_SURFACE2, sel->accent);
    if (surf) {
        char r1[96], r2[96], r3[96], r4[96];
        snprintf(r1, sizeof(r1), "Actor ID: #%u (%s)", sel->actor_id, sel->actor_kind_str);
        snprintf(r2, sizeof(r2), "Surface ID: #%u  (%ux%u)", sel->surface_id, surf->width, surf->height);
        snprintf(r3, sizeof(r3), "Mode: %s  |  Rebinds: %u",
                 (surf->compositor_mode == B8G_COMP_OPEN_WORLD_3D) ? "OPEN_WORLD_3D" : "TILING_2D",
                 surf->rebind_count);
        snprintf(r4, sizeof(r4), "State: %s  |  Commits: %llu",
                 sk_surface_state_name(surf->state), (unsigned long long)surf->commit_seq);

        sk_text(hdc, app->font_mono, sk_rect(card.x + 10, card.y + 8, card.w - 20, 20), r1, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
        sk_text(hdc, app->font_mono, sk_rect(card.x + 10, card.y + 30, card.w - 20, 20), r2, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
        sk_text(hdc, app->font_mono, sk_rect(card.x + 10, card.y + 52, card.w - 20, 20), r3, SK_COL_CYAN, DT_LEFT | DT_SINGLELINE);
        sk_text(hdc, app->font_mono, sk_rect(card.x + 10, card.y + 74, card.w - 20, 20), r4,
                sk_surface_state_color(surf->state), DT_LEFT | DT_SINGLELINE);
    }

    /* Wake Lease Toggles */
    sk_text(hdc, app->font_small, sk_rect(ins.x + 14, ins.y + 190, ins.w - 28, 16),
            "WAKE LEASES (click to toggle)", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
    uint32_t cur_leases = surf ? surf->wake_leases : 0u;
    sk_button(hdc, app, rect_lease_btn(w, h, 0), "AUDIO", (cur_leases & B8G_LEASE_AUDIO) != 0, SK_COL_CYAN);
    sk_button(hdc, app, rect_lease_btn(w, h, 1), "NET_PUSH", (cur_leases & B8G_LEASE_NET_PUSH) != 0, SK_COL_PURPLE);
    sk_button(hdc, app, rect_lease_btn(w, h, 2), "COMPUTE", (cur_leases & B8G_LEASE_COMPUTE) != 0, SK_COL_GREEN);

    /* Mode & Commit buttons */
    bool is_3d = (surf && surf->compositor_mode == B8G_COMP_OPEN_WORLD_3D);
    sk_button(hdc, app, rect_mode_btn(w, h), is_3d ? "Mode: 3D -> 2D" : "Mode: 2D -> 3D", false, SK_COL_AMBER);
    sk_button(hdc, app, rect_commit_btn(w, h), "Commit Frame", true, sel->accent);

    /* Port Details */
    SkRect pbox = sk_rect(ins.x + 12, ins.y + 290, ins.w - 24, 240);
    sk_round_rect(hdc, pbox, 6, SK_COL_BG, SK_COL_BORDER);
    sk_text(hdc, app->font_bold, sk_rect(pbox.x + 10, pbox.y + 8, pbox.w - 20, 18),
            "PORT DETAILS", SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);

    int py = pbox.y + 32;
    sk_text(hdc, app->font_small, sk_rect(pbox.x + 10, py, pbox.w - 20, 16), "INPUTS:", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
    py += 18;
    if (sel->in_count == 0) {
        sk_text(hdc, app->font_mono, sk_rect(pbox.x + 16, py, pbox.w - 26, 18), "(source node)", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
        py += 20;
    } else {
        for (int i = 0; i < sel->in_count; ++i) {
            char pl[64];
            snprintf(pl, sizeof(pl), "-> [%d] %s", i, sel->in_ports[i]);
            sk_text(hdc, app->font_mono, sk_rect(pbox.x + 16, py, pbox.w - 26, 18), pl, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
            py += 20;
        }
    }

    py += 8;
    sk_text(hdc, app->font_small, sk_rect(pbox.x + 10, py, pbox.w - 20, 16), "OUTPUTS:", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
    py += 18;
    for (int i = 0; i < sel->out_count; ++i) {
        char pl[64];
        snprintf(pl, sizeof(pl), "<- [%d] %s", i, sel->out_ports[i]);
        sk_text(hdc, app->font_mono, sk_rect(pbox.x + 16, py, pbox.w - 26, 18), pl, sel->accent, DT_LEFT | DT_SINGLELINE);
        py += 20;
    }

    /* Global Display Stats Footer */
    SkRect fbox = sk_rect(ins.x + 12, ins.y + ins.h - 92, ins.w - 24, 80);
    sk_round_rect(hdc, fbox, 6, SK_COL_SURFACE2, SK_COL_BORDER);
    char s1[96], s2[96];
    snprintf(s1, sizeof(s1), "Actors: %u | Surfaces: %u | Binds: %u",
             stats.active_actors, stats.active_surfaces, stats.active_bindings);
    snprintf(s2, sizeof(s2), "Commits: %llu | Rebinds: %llu",
             (unsigned long long)stats.total_commits, (unsigned long long)stats.total_rebinds);
    sk_text(hdc, app->font_small, sk_rect(fbox.x + 10, fbox.y + 8, fbox.w - 20, 16),
            "B8G_DISPLAY_V1 GRAPH STATS", SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);
    sk_text(hdc, app->font_mono, sk_rect(fbox.x + 10, fbox.y + 28, fbox.w - 20, 18), s1, SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
    sk_text(hdc, app->font_mono, sk_rect(fbox.x + 10, fbox.y + 50, fbox.w - 20, 18), s2, SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show) {
    (void)hinst;
    (void)hprev;
    (void)cmdline;
    (void)show;

    StudioState state;
    memset(&state, 0, sizeof(state));

    SkApp app;
    memset(&app, 0, sizeof(app));
    app.user = &state;
    app.on_render = on_render;
    app.on_mouse = on_mouse;
    app.on_key = on_key;
    app.on_tick = on_tick;

    return sk_run(&app, "SectiumNodeGraphStudio", "Sectium Node Graph Studio", STUDIO_W, STUDIO_H, false, false, 33);
}
