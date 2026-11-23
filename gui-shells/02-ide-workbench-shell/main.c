#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 02 — IDE Workbench Shell (Activity Bar + Explorer + Editor + Drawer + Inspector)
 * ============================================================================ */

#define IDE_FILE_COUNT     5
#define IDE_DRAWER_TABS    3
#define IDE_ACTIVITY_W     50
#define IDE_EXPLORER_W     224
#define IDE_INSPECTOR_W    248
#define IDE_TITLEBAR_H     36
#define IDE_TABBAR_H       34
#define IDE_STATUSBAR_H    26
#define IDE_SPLITTER_H     6

typedef enum IdeActivity {
    IDE_ACT_EXPLORER = 0,
    IDE_ACT_SEARCH   = 1,
    IDE_ACT_SWARM    = 2,
    IDE_ACT_DEBUG    = 3
} IdeActivity;

typedef struct IdeFile {
    const char* name;
    const char* path;
    const char* lang;
    const char* lines[14];
    int         line_count;
    uint32_t    surface_id;
    bool        open_tab;
} IdeFile;

typedef struct IdeState {
    bool     initialized;
    int      activity;
    bool     sidebar_open;
    bool     inspector_open;
    bool     drawer_open;
    int      active_file;
    int      active_line;
    int      active_drawer_tab;
    int      drawer_height;
    bool     dragging_drawer;
    IdeFile  files[IDE_FILE_COUNT];
    uint32_t drawer_surfaces[IDE_DRAWER_TABS];

    /* Hit-test rects */
    SkRect   act_rects[4];
    SkRect   file_rects[IDE_FILE_COUNT];
    SkRect   tab_rects[IDE_FILE_COUNT];
    SkRect   drawer_tab_rects[IDE_DRAWER_TABS];
    SkRect   drawer_splitter_rect;
    SkRect   editor_rect;
    SkRect   btn_freeze_bg;
    SkRect   btn_toggle_lease;
    SkRect   btn_toggle_inspector;
    SkRect   btn_toggle_drawer;
} IdeState;

static B8GSurface* ide_find_surface(B8GDisplayCtx* ctx, uint32_t surface_id) {
    for (uint32_t i = 0; i < B8G_DISPLAY_MAX_SURFACES; i++) {
        if (ctx->surfaces[i].active && ctx->surfaces[i].surface_id == surface_id) {
            return &ctx->surfaces[i];
        }
    }
    return NULL;
}

static void ide_ensure_init(SkApp* app, IdeState* st) {
    if (st->initialized) return;
    st->initialized = true;
    st->activity = IDE_ACT_EXPLORER;
    st->sidebar_open = true;
    st->inspector_open = true;
    st->drawer_open = true;
    st->active_file = 0;
    st->active_line = 4;
    st->active_drawer_tab = 0;
    st->drawer_height = 178;

    const IdeFile seed[IDE_FILE_COUNT] = {
        {
            "sectium_display.h",
            "sections/sectium-display/sectium_display.h",
            "C11",
            {
                "#ifndef SECTIUM_DISPLAY_H",
                "#define SECTIUM_DISPLAY_H",
                "/* B8G_DISPLAY_V1 — Zero-dependency C11 Wayland alternative */",
                "#define B8G_DISPLAY_MAGIC_V1      0x31444742u",
                "#define B8G_DISPLAY_MAX_SURFACES  128u",
                "typedef enum B8GSurfaceState {",
                "    B8G_SURFACE_ACTIVE        = 0,",
                "    B8G_SURFACE_IDLE          = 1,",
                "    B8G_SURFACE_DISPLAY_SLEEP = 2,",
                "    B8G_SURFACE_FROZEN        = 3,",
                "    B8G_SURFACE_CHECKPOINTED  = 4",
                "} B8GSurfaceState;",
                "int32_t sectium_surface_thaw(B8GDisplayCtx* ctx, uint32_t id, uint64_t now);",
                "#endif"
            },
            14, 0, true
        },
        {
            "sectium_display.c",
            "sections/sectium-display/sectium_display.c",
            "C11",
            {
                "#include \"sectium_display.h\"",
                "_Static_assert(sizeof(B8GDisplayStats) == 80u, \"ABI mismatch\");",
                "int32_t sectium_surface_evaluate_idle(B8GDisplayCtx* ctx, uint32_t id,",
                "    uint64_t now_ms, uint64_t idle_ms, uint64_t freeze_ms, uint32_t* out) {",
                "    B8GSurface* s = find_surface(ctx, id);",
                "    uint64_t elapsed = now_ms - s->last_active_ms;",
                "    if (elapsed >= freeze_ms) {",
                "        if (s->wake_leases != B8G_LEASE_NONE) s->state = B8G_SURFACE_DISPLAY_SLEEP;",
                "        else if (s->state == B8G_SURFACE_FROZEN) s->state = B8G_SURFACE_CHECKPOINTED;",
                "        else s->state = B8G_SURFACE_FROZEN;",
                "    }",
                "    return B8G_DISPLAY_OK;",
                "}",
                ""
            },
            13, 0, true
        },
        {
            "sectium_shell_kit.h",
            "gui-shells/common/sectium_shell_kit.h",
            "C11",
            {
                "/* Sectium Shell Kit — Minimal Zero-Bloat Win32 + B8G_DISPLAY_V1 */",
                "#define SK_COL_BG       SK_RGB(15, 18, 26)",
                "#define SK_COL_SURFACE  SK_RGB(22, 27, 38)",
                "#define SK_COL_ACCENT   SK_RGB(88, 166, 255)",
                "static inline int sk_run(SkApp* app, const char* cls, const char* title,",
                "    int width, int height, bool frameless, bool topmost, UINT tick_ms) {",
                "    sectium_display_init(&app->display);",
                "    sectium_actor_register(&app->display, B8G_ACTOR_HUMAN, \"human-primary\", &id);",
                "    /* Double-buffered 32-bit DIBSection (zero flicker) */",
                "    return (int)msg.wParam;",
                "}",
                "", "", ""
            },
            11, 0, true
        },
        {
            "index.mts",
            "intersections/holo-compiler/index.mts",
            "TypeScript",
            {
                "// holo-compiler — Tier-A < 1s native C11 / WASI / .b8p CAS builder",
                "export interface CompileTarget {",
                "  readonly triple: 'x86_64-windows-gnu' | 'x86_64-linux-musl' | 'wasm32-wasi';",
                "  readonly optLevel: '-O2' | '-O3';",
                "}",
                "export async function buildPack(entry: string, target: CompileTarget) {",
                "  const hash = await merkleHashFile(entry);",
                "  return emitB8pObjectPack(hash, target);",
                "}",
                "", "", "", "", ""
            },
            9, 0, false
        },
        {
            "ARCHITECTURE.md",
            "ARCHITECTURE.md",
            "Markdown",
            {
                "# Awesome-OS — Collapsed 4-Layer Architecture",
                "",
                "1. Layer 1: Sectium (D:\\Sectium) + Sectium Canvas (BSD-3)",
                "2. Layer 2: sectium-cloud-control-plane (5 KB B8G_ABI_MAGIC_V3)",
                "3. Layer 3: sections/sectium-device (dual opaque NDJSON channels)",
                "4. Layer 4: holo-compiler polyglot zero-copy runtimes",
                "",
                "All native shells render directly over B8G_DISPLAY_V1 surfaces.",
                "", "", "", "", "", ""
            },
            8, 0, false
        }
    };

    for (int i = 0; i < IDE_FILE_COUNT; i++) {
        st->files[i] = seed[i];
        uint32_t sid = 0;
        sectium_surface_create(&app->display, app->human_actor_id, 900u, 520u, B8G_COMP_TILING_2D, &sid);
        st->files[i].surface_id = sid;
        uint32_t bid = 0;
        sectium_actor_bind_stream(&app->display, app->human_actor_id, sid, B8G_STREAM_SURFACE, &bid);
        uint64_t seq = 0;
        sectium_surface_commit(&app->display, sid, app->now_ms, &seq);
    }

    for (int i = 0; i < IDE_DRAWER_TABS; i++) {
        uint32_t sid = 0;
        sectium_surface_create(&app->display, app->human_actor_id, 900u, 180u, B8G_COMP_TILING_2D, &sid);
        st->drawer_surfaces[i] = sid;
        if (i == 0) {
            sectium_surface_set_leases(&app->display, sid, B8G_LEASE_COMPUTE);
        }
        uint32_t bid = 0;
        sectium_actor_bind_stream(&app->display, app->human_actor_id, sid, B8G_STREAM_WORKER_IO, &bid);
        uint64_t seq = 0;
        sectium_surface_commit(&app->display, sid, app->now_ms, &seq);
    }
}

static void ide_open_file(SkApp* app, IdeState* st, int idx) {
    if (idx < 0 || idx >= IDE_FILE_COUNT) return;
    st->files[idx].open_tab = true;
    st->active_file = idx;
    if (st->active_line >= st->files[idx].line_count) st->active_line = 0;
    sectium_surface_thaw(&app->display, st->files[idx].surface_id, app->now_ms);
    uint64_t seq = 0;
    sectium_surface_commit(&app->display, st->files[idx].surface_id, app->now_ms, &seq);
}

static void ide_freeze_bg(SkApp* app, IdeState* st) {
    uint64_t future = app->now_ms + 120000ULL;
    for (int i = 0; i < IDE_FILE_COUNT; i++) {
        if (i == st->active_file) continue;
        uint32_t s = 0;
        sectium_surface_evaluate_idle(&app->display, st->files[i].surface_id, future, 10000ULL, 30000ULL, &s);
        sectium_surface_evaluate_idle(&app->display, st->files[i].surface_id, future + 30000ULL, 10000ULL, 30000ULL, &s);
    }
    for (int i = 0; i < IDE_DRAWER_TABS; i++) {
        if (i == st->active_drawer_tab) continue;
        uint32_t s = 0;
        sectium_surface_evaluate_idle(&app->display, st->drawer_surfaces[i], future, 10000ULL, 30000ULL, &s);
        sectium_surface_evaluate_idle(&app->display, st->drawer_surfaces[i], future + 30000ULL, 10000ULL, 30000ULL, &s);
    }
}

static COLORREF ide_syntax_color(const char* line) {
    if (!line || !line[0]) return SK_COL_TEXT;
    if (line[0] == '/' || line[0] == '*') return SK_COL_MUTED;
    if (line[0] == '#') return SK_COL_AMBER;
    if (strstr(line, "typedef") || strstr(line, "export") || strstr(line, "return") || strstr(line, "static")) return SK_COL_PURPLE;
    if (strstr(line, "B8G_") || strstr(line, "SK_COL") || strstr(line, "uint")) return SK_COL_CYAN;
    if (strchr(line, '"') || strchr(line, '\'')) return SK_COL_GREEN;
    return SK_COL_TEXT;
}

static void ide_render(SkApp* app, HDC hdc) {
    IdeState* st = (IdeState*)app->user;
    ide_ensure_init(app, st);

    int w = app->width;
    int h = app->height;

    /* 1. Top Workbench bar */
    sk_fill_rect(hdc, sk_rect(0, 0, w, IDE_TITLEBAR_H), SK_COL_SURFACE);
    sk_line(hdc, 0, IDE_TITLEBAR_H - 1, w, IDE_TITLEBAR_H - 1, SK_COL_BORDER, 1);
    sk_text(hdc, app->font_bold, sk_rect(14, 0, 360, IDE_TITLEBAR_H),
            "SECTIUM IDE WORKBENCH  —  e:\\awesome-os", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    st->btn_toggle_drawer    = sk_rect(w - 252, 5, 114, 26);
    st->btn_toggle_inspector = sk_rect(w - 130, 5, 118, 26);
    sk_button(hdc, app, st->btn_toggle_drawer,    st->drawer_open ? "Hide Drawer" : "Show Drawer", st->drawer_open, SK_COL_CYAN);
    sk_button(hdc, app, st->btn_toggle_inspector, st->inspector_open ? "Hide Inspector" : "Inspector", st->inspector_open, SK_COL_ACCENT);

    int body_y = IDE_TITLEBAR_H;
    int body_h = h - IDE_TITLEBAR_H - IDE_STATUSBAR_H;

    /* 2. Far-left Activity Bar */
    sk_fill_rect(hdc, sk_rect(0, body_y, IDE_ACTIVITY_W, body_h), SK_COL_BG);
    sk_line(hdc, IDE_ACTIVITY_W - 1, body_y, IDE_ACTIVITY_W - 1, body_y + body_h, SK_COL_BORDER, 1);

    const char* act_labels[4] = { "EXP", "SRC", "SWM", "DBG" };
    for (int i = 0; i < 4; i++) {
        SkRect ar = sk_rect(5, body_y + 10 + i * 48, IDE_ACTIVITY_W - 10, 38);
        st->act_rects[i] = ar;
        bool active = (st->sidebar_open && st->activity == i);
        sk_round_rect(hdc, ar, 6, active ? SK_COL_SURFACE3 : SK_COL_BG, active ? SK_COL_ACCENT : SK_COL_BG);
        if (active) {
            sk_fill_rect(hdc, sk_rect(0, ar.y + 4, 3, ar.h - 8), SK_COL_ACCENT);
        }
        sk_text(hdc, active ? app->font_bold : app->font_small, ar, act_labels[i],
                active ? SK_COL_ACCENT : SK_COL_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    int left_x = IDE_ACTIVITY_W;

    /* 3. Collapsible Sidebar (Explorer / Search / Swarm / Debug) */
    if (st->sidebar_open) {
        SkRect sb = sk_rect(left_x, body_y, IDE_EXPLORER_W, body_h);
        sk_fill_rect(hdc, sb, SK_COL_SURFACE);
        sk_line(hdc, left_x + IDE_EXPLORER_W - 1, body_y, left_x + IDE_EXPLORER_W - 1, body_y + body_h, SK_COL_BORDER, 1);

        const char* headers[4] = { "EXPLORER", "SEARCH", "SWARM BOARD", "DEBUGGER" };
        sk_text(hdc, app->font_bold, sk_rect(left_x + 12, body_y + 10, IDE_EXPLORER_W - 24, 22),
                headers[st->activity], SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (st->activity == IDE_ACT_EXPLORER) {
            sk_text(hdc, app->font_small, sk_rect(left_x + 12, body_y + 34, IDE_EXPLORER_W - 24, 18),
                    "v AWESOME-OS", SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);
            for (int i = 0; i < IDE_FILE_COUNT; i++) {
                SkRect fr = sk_rect(left_x + 8, body_y + 58 + i * 32, IDE_EXPLORER_W - 16, 28);
                st->file_rects[i] = fr;
                bool active = (i == st->active_file);
                bool hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, fr);
                sk_round_rect(hdc, fr, 4, active ? SK_COL_SURFACE3 : (hover ? SK_COL_SURFACE2 : SK_COL_SURFACE),
                              active ? SK_COL_ACCENT : SK_COL_SURFACE);
                B8GSurface* s = ide_find_surface(&app->display, st->files[i].surface_id);
                COLORREF dot = sk_surface_state_color(s ? s->state : B8G_SURFACE_ACTIVE);
                sk_circle(hdc, fr.x + 10, fr.y + 14, 3, dot, dot);
                sk_text(hdc, active ? app->font_bold : app->font_ui,
                        sk_rect(fr.x + 20, fr.y, fr.w - 24, fr.h),
                        st->files[i].name, active ? SK_COL_TEXT : SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        } else {
            for (int i = 0; i < IDE_FILE_COUNT; i++) st->file_rects[i] = sk_rect(0, 0, 0, 0);
            const char* info[3] = {
                st->activity == IDE_ACT_SEARCH ? "Query: B8G_DISPLAY_V1\n14 matches across 4 files" :
                st->activity == IDE_ACT_SWARM  ? "Swarm: awesome-os\n@shells-a: ACTIVE\n@coordinator: ONLINE" :
                                                 "Target: x86_64-windows-gnu\nBreakpoints: 2\nHeap Arena: 64B aligned",
                "Click EXP icon to return to File Explorer.",
                ""
            };
            sk_text(hdc, app->font_mono, sk_rect(left_x + 12, body_y + 42, IDE_EXPLORER_W - 24, 140),
                    info[0], SK_COL_TEXT, DT_LEFT | DT_WORDBREAK);
            sk_text(hdc, app->font_small, sk_rect(left_x + 12, body_y + 190, IDE_EXPLORER_W - 24, 40),
                    info[1], SK_COL_MUTED, DT_LEFT | DT_WORDBREAK);
        }
        left_x += IDE_EXPLORER_W;
    } else {
        for (int i = 0; i < IDE_FILE_COUNT; i++) st->file_rects[i] = sk_rect(0, 0, 0, 0);
    }

    int right_w = st->inspector_open ? IDE_INSPECTOR_W : 0;
    int center_w = w - left_x - right_w;
    if (center_w < 200) center_w = 200;

    int drawer_h = st->drawer_open ? st->drawer_height : 0;
    if (drawer_h > body_h - 140) drawer_h = body_h - 140;
    int editor_total_h = body_h - drawer_h;

    /* 4. Multi-tab Code Editor */
    SkRect tabbar = sk_rect(left_x, body_y, center_w, IDE_TABBAR_H);
    sk_fill_rect(hdc, tabbar, SK_COL_BG);
    sk_line(hdc, left_x, body_y + IDE_TABBAR_H - 1, left_x + center_w, body_y + IDE_TABBAR_H - 1, SK_COL_BORDER, 1);

    int tx = left_x + 4;
    for (int i = 0; i < IDE_FILE_COUNT; i++) {
        if (!st->files[i].open_tab) {
            st->tab_rects[i] = sk_rect(0, 0, 0, 0);
            continue;
        }
        int tw = 148;
        SkRect tr = sk_rect(tx, body_y + 3, tw, IDE_TABBAR_H - 4);
        st->tab_rects[i] = tr;
        bool active = (i == st->active_file);
        sk_fill_rect(hdc, tr, active ? SK_COL_SURFACE : SK_COL_BG);
        if (active) {
            sk_fill_rect(hdc, sk_rect(tr.x, tr.y, tr.w, 2), SK_COL_ACCENT);
        }
        B8GSurface* s = ide_find_surface(&app->display, st->files[i].surface_id);
        COLORREF dot = sk_surface_state_color(s ? s->state : B8G_SURFACE_ACTIVE);
        sk_circle(hdc, tr.x + 12, tr.y + tr.h / 2, 3, dot, dot);
        sk_text(hdc, active ? app->font_bold : app->font_small,
                sk_rect(tr.x + 22, tr.y, tr.w - 26, tr.h),
                st->files[i].name, active ? SK_COL_TEXT : SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        tx += tw + 2;
    }

    st->editor_rect = sk_rect(left_x, body_y + IDE_TABBAR_H, center_w, editor_total_h - IDE_TABBAR_H);
    sk_fill_rect(hdc, st->editor_rect, SK_COL_BG);

    IdeFile* af = &st->files[st->active_file];
    /* Breadcrumb */
    sk_text(hdc, app->font_small, sk_rect(st->editor_rect.x + 12, st->editor_rect.y + 4, st->editor_rect.w - 24, 20),
            af->path, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int code_y = st->editor_rect.y + 28;
    int line_h = 22;
    for (int i = 0; i < af->line_count; i++) {
        int ly = code_y + i * line_h;
        if (ly + line_h > st->editor_rect.y + st->editor_rect.h - 4) break;
        if (i == st->active_line) {
            sk_fill_rect(hdc, sk_rect(st->editor_rect.x, ly, st->editor_rect.w, line_h), SK_COL_SURFACE2);
        }
        char lnum[12];
        snprintf(lnum, sizeof(lnum), "%3d", i + 1);
        sk_text(hdc, app->font_mono, sk_rect(st->editor_rect.x + 6, ly, 36, line_h),
                lnum, i == st->active_line ? SK_COL_ACCENT : SK_COL_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        sk_text(hdc, app->font_mono, sk_rect(st->editor_rect.x + 54, ly, st->editor_rect.w - 64, line_h),
                af->lines[i], ide_syntax_color(af->lines[i]), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* 5. Draggable Bottom Drawer (Terminal / Problems / Output) */
    if (st->drawer_open && drawer_h > 40) {
        int dy = body_y + editor_total_h;
        st->drawer_splitter_rect = sk_rect(left_x, dy, center_w, IDE_SPLITTER_H);
        bool sp_hot = st->dragging_drawer || sk_pt_in_rect(app->mouse_x, app->mouse_y, st->drawer_splitter_rect);
        sk_fill_rect(hdc, st->drawer_splitter_rect, sp_hot ? SK_COL_ACCENT : SK_COL_BORDER);

        SkRect drawer = sk_rect(left_x, dy + IDE_SPLITTER_H, center_w, drawer_h - IDE_SPLITTER_H);
        sk_fill_rect(hdc, drawer, SK_COL_SURFACE);

        const char* dtabs[IDE_DRAWER_TABS] = { "TERMINAL [COMPUTE]", "PROBLEMS (0)", "OUTPUT" };
        int dtx = drawer.x + 10;
        for (int i = 0; i < IDE_DRAWER_TABS; i++) {
            SkRect dtr = sk_rect(dtx, drawer.y + 4, 148, 24);
            st->drawer_tab_rects[i] = dtr;
            bool act = (i == st->active_drawer_tab);
            sk_round_rect(hdc, dtr, 4, act ? SK_COL_SURFACE3 : SK_COL_SURFACE, act ? SK_COL_CYAN : SK_COL_SURFACE);
            sk_text(hdc, act ? app->font_bold : app->font_small, dtr, dtabs[i],
                    act ? SK_COL_TEXT : SK_COL_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dtx += 156;
        }
        sk_line(hdc, drawer.x, drawer.y + 32, drawer.x + drawer.w, drawer.y + 32, SK_COL_BORDER, 1);

        const char* term_lines[] = {
            "PS E:\\awesome-os> .\\bin\\zig.cmd cc -std=c11 -O2 -Wall -Wextra -Werror ...",
            "[holo-compiler] 0 warnings, 0 errors | B8G_DISPLAY_V1 static link OK",
            "[b8g-arena]     64-byte aligned arena attached; wake_lease=COMPUTE active"
        };
        const char* prob_lines[] = {
            "0 Errors, 0 Warnings across workspace.",
            "All B8G_DISPLAY_V1 _Static_assert checks verified at compile time.",
            "bin/ atomic triplet parity: PASS"
        };
        const char* out_lines[] = {
            "[sectium-device] channel=command ttyS0 connected",
            "[sectium-cp]     B8G_ABI_MAGIC_V3 (0x33564742) ready",
            "[gui-shell-02]   IDE Workbench surface commits streaming"
        };
        const char** src = st->active_drawer_tab == 0 ? term_lines : (st->active_drawer_tab == 1 ? prob_lines : out_lines);
        for (int i = 0; i < 3; i++) {
            sk_text(hdc, app->font_mono, sk_rect(drawer.x + 14, drawer.y + 42 + i * 22, drawer.w - 28, 20),
                    src[i], i == 0 ? SK_COL_GREEN : SK_COL_TEXT, DT_LEFT | DT_SINGLELINE);
        }
    } else {
        st->drawer_splitter_rect = sk_rect(0, 0, 0, 0);
        for (int i = 0; i < IDE_DRAWER_TABS; i++) st->drawer_tab_rects[i] = sk_rect(0, 0, 0, 0);
    }

    /* 6. Right Inspector Panel */
    if (st->inspector_open) {
        int ix = w - IDE_INSPECTOR_W;
        SkRect ins = sk_rect(ix, body_y, IDE_INSPECTOR_W, body_h);
        sk_fill_rect(hdc, ins, SK_COL_SURFACE);
        sk_line(hdc, ix, body_y, ix, body_y + body_h, SK_COL_BORDER, 1);

        sk_text(hdc, app->font_bold, sk_rect(ix + 14, body_y + 12, IDE_INSPECTOR_W - 28, 22),
                "SURFACE INSPECTOR", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);

        B8GSurface* s = ide_find_surface(&app->display, af->surface_id);
        uint32_t state = s ? s->state : B8G_SURFACE_ACTIVE;
        bool has_comp = s && ((s->wake_leases & B8G_LEASE_COMPUTE) != 0);

        SkRect card = sk_rect(ix + 12, body_y + 40, IDE_INSPECTOR_W - 24, 152);
        sk_round_rect(hdc, card, 6, SK_COL_BG, SK_COL_BORDER);

        char meta[256];
        snprintf(meta, sizeof(meta),
                 "File:    %s\nLang:    %s\nSurface: #%u\nState:   %s\nCommits: %llu\nEpoch:   %llu\nLeases:  %s",
                 af->name, af->lang, af->surface_id, sk_surface_state_name(state),
                 (unsigned long long)(s ? s->commit_seq : 0),
                 (unsigned long long)(s ? s->checkpoint_epoch : 0),
                 has_comp ? "COMPUTE" : "NONE");
        sk_text(hdc, app->font_mono, sk_rect(card.x + 10, card.y + 10, card.w - 20, card.h - 20),
                meta, SK_COL_TEXT, DT_LEFT);

        st->btn_freeze_bg    = sk_rect(ix + 12, body_y + 204, IDE_INSPECTOR_W - 24, 32);
        st->btn_toggle_lease = sk_rect(ix + 12, body_y + 244, IDE_INSPECTOR_W - 24, 32);
        sk_button(hdc, app, st->btn_freeze_bg,    "Freeze Idle Surfaces", false, SK_COL_PURPLE);
        sk_button(hdc, app, st->btn_toggle_lease, has_comp ? "Compute Lease: ON" : "Add Compute Lease", has_comp, SK_COL_CYAN);

        sk_text(hdc, app->font_bold, sk_rect(ix + 14, body_y + 294, IDE_INSPECTOR_W - 28, 20),
                "OUTLINE", SK_COL_MUTED, DT_LEFT | DT_SINGLELINE);
        const char* syms[] = { "B8GDisplayCtx", "B8GSurface", "sectium_surface_create", "sectium_surface_evaluate_idle", "sectium_surface_thaw" };
        for (int i = 0; i < 5; i++) {
            sk_text(hdc, app->font_mono, sk_rect(ix + 14, body_y + 320 + i * 22, IDE_INSPECTOR_W - 28, 20),
                    syms[i], i < 2 ? SK_COL_CYAN : SK_COL_ACCENT, DT_LEFT | DT_SINGLELINE);
        }
    } else {
        st->btn_freeze_bg = sk_rect(0, 0, 0, 0);
        st->btn_toggle_lease = sk_rect(0, 0, 0, 0);
    }

    /* 7. Bottom Status Bar with live B8GDisplayStats */
    B8GDisplayStats stats;
    sectium_display_get_stats(&app->display, &stats);
    SkRect sb_bar = sk_rect(0, h - IDE_STATUSBAR_H, w, IDE_STATUSBAR_H);
    sk_fill_rect(hdc, sb_bar, SK_COL_ACCENT_DIM);
    char sbuf[256];
    snprintf(sbuf, sizeof(sbuf),
             " main*  |  %s  Ln %d  |  B8G_DISPLAY_V1: %u surfaces (%u active, %u sleep, %u checkpointed)  |  Commits: %llu",
             af->lang, st->active_line + 1, stats.active_surfaces, stats.surfaces_active,
             stats.surfaces_display_sleep, stats.surfaces_checkpointed, (unsigned long long)stats.total_commits);
    sk_text(hdc, app->font_small, sk_rect(8, sb_bar.y, w - 16, IDE_STATUSBAR_H),
            sbuf, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void ide_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    IdeState* st = (IdeState*)app->user;
    ide_ensure_init(app, st);

    if (msg == WM_LBUTTONDOWN) {
        if (sk_pt_in_rect(x, y, st->btn_toggle_drawer)) {
            st->drawer_open = !st->drawer_open;
            return;
        }
        if (sk_pt_in_rect(x, y, st->btn_toggle_inspector)) {
            st->inspector_open = !st->inspector_open;
            return;
        }
        if (sk_pt_in_rect(x, y, st->btn_freeze_bg)) {
            ide_freeze_bg(app, st);
            return;
        }
        if (sk_pt_in_rect(x, y, st->btn_toggle_lease)) {
            B8GSurface* s = ide_find_surface(&app->display, st->files[st->active_file].surface_id);
            if (s) {
                uint32_t next = (s->wake_leases & B8G_LEASE_COMPUTE) ? B8G_LEASE_NONE : B8G_LEASE_COMPUTE;
                sectium_surface_set_leases(&app->display, s->surface_id, next);
            }
            return;
        }
        for (int i = 0; i < 4; i++) {
            if (sk_pt_in_rect(x, y, st->act_rects[i])) {
                if (st->sidebar_open && st->activity == i) {
                    st->sidebar_open = false;
                } else {
                    st->activity = i;
                    st->sidebar_open = true;
                }
                return;
            }
        }
        for (int i = 0; i < IDE_FILE_COUNT; i++) {
            if (sk_pt_in_rect(x, y, st->file_rects[i]) || sk_pt_in_rect(x, y, st->tab_rects[i])) {
                ide_open_file(app, st, i);
                return;
            }
        }
        for (int i = 0; i < IDE_DRAWER_TABS; i++) {
            if (sk_pt_in_rect(x, y, st->drawer_tab_rects[i])) {
                st->active_drawer_tab = i;
                sectium_surface_thaw(&app->display, st->drawer_surfaces[i], app->now_ms);
                return;
            }
        }
        if (st->drawer_open && sk_pt_in_rect(x, y, st->drawer_splitter_rect)) {
            st->dragging_drawer = true;
            return;
        }
        if (sk_pt_in_rect(x, y, st->editor_rect)) {
            int rel = y - (st->editor_rect.y + 28);
            if (rel >= 0) {
                int ln = rel / 22;
                if (ln < st->files[st->active_file].line_count) {
                    st->active_line = ln;
                }
            }
            return;
        }
    } else if (msg == WM_MOUSEMOVE) {
        if (st->dragging_drawer && st->drawer_open) {
            int new_h = (app->height - IDE_STATUSBAR_H) - y;
            if (new_h < 90) new_h = 90;
            if (new_h > app->height - 240) new_h = app->height - 240;
            st->drawer_height = new_h;
        }
    } else if (msg == WM_LBUTTONUP) {
        st->dragging_drawer = false;
    }
}

static void ide_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_KEYDOWN) return;
    IdeState* st = (IdeState*)app->user;
    ide_ensure_init(app, st);

    if (wp == VK_UP && st->active_line > 0) {
        st->active_line--;
    } else if (wp == VK_DOWN && st->active_line + 1 < st->files[st->active_file].line_count) {
        st->active_line++;
    } else if (wp == 'F') {
        ide_freeze_bg(app, st);
    } else if (wp >= '1' && wp <= '5') {
        ide_open_file(app, st, (int)(wp - '1'));
    }
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show) {
    (void)hinst;
    (void)hprev;
    (void)cmdline;
    (void)show;

    static IdeState state;
    memset(&state, 0, sizeof(state));

    static SkApp app;
    memset(&app, 0, sizeof(app));
    app.on_render = ide_render;
    app.on_mouse  = ide_mouse;
    app.on_key    = ide_key;
    app.user      = &state;

    return sk_run(
        &app,
        "SectiumIdeWorkbenchShell",
        "Sectium Shell 02 — IDE Workbench (B8G_DISPLAY_V1)",
        1380,
        840,
        false,
        false,
        250
    );
}
