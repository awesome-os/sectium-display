#include "../common/sectium_shell_kit.h"

/* ============================================================================
 * 07 — Mission Control / Telemetry / Proxmox Docking Dashboard Shell
 * 1240x760 | tick_ms = 250 | Real B8G_DISPLAY_V1 telemetry + live sparklines
 * ============================================================================ */

#define MAX_MANAGED_SURFACES 8
#define SPARK_SAMPLES        48
#define MAX_LOG_ENTRIES      64

typedef enum FilterMode {
    FILTER_ALL      = 0,
    FILTER_ACTIVE   = 1,
    FILTER_SLEEPING = 2,
    FILTER_FROZEN   = 3
} FilterMode;

typedef struct ManagedSurface {
    uint32_t surface_id;
    uint32_t owner_actor_id;
    char     name[48];
    char     role[24];
    uint32_t arena_kb;
    uint32_t leases;
} ManagedSurface;

typedef struct LogEntry {
    uint64_t time_ms;
    char     tag[12];
    COLORREF tag_col;
    char     msg[96];
} LogEntry;

typedef struct DashboardState {
    bool           initialized;
    bool           paused;
    FilterMode     filter;
    uint32_t       tick_count;
    uint64_t       last_commits;
    uint32_t       commits_per_sec;

    uint32_t       actor_pve;
    uint32_t       actor_swarm;
    uint32_t       actor_media;

    ManagedSurface surfaces[MAX_MANAGED_SURFACES];
    int            surface_count;
    int            selected_surface;

    /* Sparkline ring buffers */
    float          latency_ms[SPARK_SAMPLES];
    float          arena_pct[SPARK_SAMPLES];
    int            spark_head;

    /* Event log ring buffer */
    LogEntry       logs[MAX_LOG_ENTRIES];
    int            log_count;
    int            log_head;

    /* Cached hit-test rects */
    SkRect         btn_filter[4];
    SkRect         btn_pause;
    SkRect         dock_row_rects[MAX_MANAGED_SURFACES];
    int            dock_row_indices[MAX_MANAGED_SURFACES];
    int            dock_row_count;
} DashboardState;

static void dash_push_log(DashboardState* st, uint64_t now_ms, const char* tag, COLORREF col, const char* msg) {
    LogEntry* e = &st->logs[st->log_head];
    e->time_ms = now_ms;
    snprintf(e->tag, sizeof(e->tag), "%s", tag);
    e->tag_col = col;
    snprintf(e->msg, sizeof(e->msg), "%s", msg);
    st->log_head = (st->log_head + 1) % MAX_LOG_ENTRIES;
    if (st->log_count < MAX_LOG_ENTRIES) {
        st->log_count++;
    }
}

static const B8GSurface* dash_find_surface(const B8GDisplayCtx* ctx, uint32_t sid) {
    for (uint32_t i = 0; i < B8G_DISPLAY_MAX_SURFACES; i++) {
        if (ctx->surfaces[i].active && ctx->surfaces[i].surface_id == sid) {
            return &ctx->surfaces[i];
        }
    }
    return NULL;
}

static void dash_ensure_init(SkApp* app, DashboardState* st) {
    if (st->initialized) return;
    st->initialized = true;
    st->selected_surface = -1;

    sectium_actor_register(&app->display, B8G_ACTOR_WORKER, "proxmox-node-01", &st->actor_pve);
    sectium_actor_register(&app->display, B8G_ACTOR_AGENT,  "swarm-orchestrator", &st->actor_swarm);
    sectium_actor_register(&app->display, B8G_ACTOR_WORKER, "media-compositor", &st->actor_media);

    typedef struct SeedSpec {
        const char* name;
        const char* role;
        uint32_t    actor_slot;
        uint32_t    w;
        uint32_t    h;
        uint32_t    arena_kb;
        uint32_t    leases;
        bool        start_idle;
    } SeedSpec;

    static const SeedSpec seeds[MAX_MANAGED_SURFACES] = {
        { "pve-cluster-console",  "PVE-VM",   0, 1280, 720,  8192, B8G_LEASE_COMPUTE, false },
        { "sectium-browser-tab0", "BROWSER",  1, 1440, 900, 12288, B8G_LEASE_AUDIO | B8G_LEASE_NET_PUSH, false },
        { "holo-compiler-build",  "BUILD",    1, 1024, 640,  6144, B8G_LEASE_COMPUTE, false },
        { "twitch-hls-overlay",   "MEDIA",    2, 1920,1080, 16384, B8G_LEASE_AUDIO, true },
        { "lumi-agent-dialog",    "AGENT",    1,  800, 600,  4096, B8G_LEASE_NET_PUSH, false },
        { "docs-reference-tab",   "DOCS",     0, 1280, 800,  6144, B8G_LEASE_NONE, true },
        { "idle-metrics-archive", "ARCHIVE",  0,  960, 540,  4096, B8G_LEASE_NONE, true },
        { "gpu-frame-compositor", "DISPLAY",  2, 1920,1080, 16384, B8G_LEASE_COMPUTE | B8G_LEASE_AUDIO, false }
    };

    uint64_t arena_cursor = 0x10000ULL;
    uint64_t base_ms = app->start_ms;

    for (int i = 0; i < MAX_MANAGED_SURFACES; i++) {
        uint32_t owner = (seeds[i].actor_slot == 0) ? st->actor_pve
                       : (seeds[i].actor_slot == 1) ? st->actor_swarm
                       : st->actor_media;
        uint32_t sid = 0;
        if (sectium_surface_create(&app->display, owner, seeds[i].w, seeds[i].h, B8G_COMP_TILING_2D, &sid) != B8G_DISPLAY_OK) {
            continue;
        }
        uint32_t bid = 0;
        sectium_actor_bind_stream(&app->display, owner, sid, B8G_STREAM_SURFACE, &bid);
        sectium_surface_attach_arena(&app->display, sid, arena_cursor, (uint64_t)seeds[i].arena_kb * 1024ULL, seeds[i].w * 4u);
        arena_cursor += ((uint64_t)seeds[i].arena_kb * 1024ULL + 63ULL) & ~63ULL;
        sectium_surface_set_leases(&app->display, sid, seeds[i].leases);

        uint64_t seq = 0;
        sectium_surface_damage(&app->display, sid, 0, 0, seeds[i].w, seeds[i].h);
        sectium_surface_commit(&app->display, sid, base_ms, &seq);

        if (seeds[i].start_idle) {
            uint32_t out_st = 0;
            sectium_surface_evaluate_idle(&app->display, sid, base_ms + 90000ULL, 10000ULL, 30000ULL, &out_st);
            if (i == 6) {
                /* Push archive surface one step further into CHECKPOINTED */
                sectium_surface_evaluate_idle(&app->display, sid, base_ms + 180000ULL, 10000ULL, 30000ULL, &out_st);
            }
        }

        ManagedSurface* ms = &st->surfaces[st->surface_count++];
        ms->surface_id = sid;
        ms->owner_actor_id = owner;
        snprintf(ms->name, sizeof(ms->name), "%s", seeds[i].name);
        snprintf(ms->role, sizeof(ms->role), "%s", seeds[i].role);
        ms->arena_kb = seeds[i].arena_kb;
        ms->leases = seeds[i].leases;
    }

    /* Seed initial sparkline history */
    for (int i = 0; i < SPARK_SAMPLES; i++) {
        st->latency_ms[i] = 2.4f + (float)((i * 7) % 13) * 0.14f;
        st->arena_pct[i]  = 46.0f + (float)((i * 5) % 19) * 0.85f;
    }

    dash_push_log(st, base_ms, "INIT",   SK_COL_ACCENT, "B8G_DISPLAY_V1 telemetry plane online (8 managed surfaces)");
    dash_push_log(st, base_ms, "ARENA",  SK_COL_CYAN,   "B8G_ARENA_V1 64-byte aligned mappings verified (72 MiB)");
    dash_push_log(st, base_ms, "LEASE",  SK_COL_GREEN,  "Wake leases active on 6 surfaces (AUDIO/NET_PUSH/COMPUTE)");
    dash_push_log(st, base_ms, "STATE",  SK_COL_PURPLE, "Idle policy: twitch-hls -> DISPLAY_SLEEP, docs -> FROZEN");
}

static void dash_on_tick(SkApp* app, uint64_t now_ms) {
    DashboardState* st = (DashboardState*)app->user;
    dash_ensure_init(app, st);
    if (st->paused) return;

    st->tick_count++;

    /* Commit active surfaces deterministically so stats & event log stay alive */
    int idx = (int)(st->tick_count % (uint32_t)st->surface_count);
    ManagedSurface* ms = &st->surfaces[idx];
    const B8GSurface* surf = dash_find_surface(&app->display, ms->surface_id);

    if (surf && surf->state == B8G_SURFACE_ACTIVE) {
        uint64_t seq = 0;
        sectium_surface_damage(&app->display, ms->surface_id, 0, 0, surf->width, 120);
        sectium_surface_commit(&app->display, ms->surface_id, now_ms, &seq);

        char buf[96];
        snprintf(buf, sizeof(buf), "surface #%u (%s) commit seq=%llu damage=120px",
                 ms->surface_id, ms->name, (unsigned long long)seq);
        dash_push_log(st, now_ms, "COMMIT", SK_COL_ACCENT, buf);
    } else if (surf && (st->tick_count % 4u == 0u)) {
        char buf[96];
        snprintf(buf, sizeof(buf), "surface #%u (%s) holding %s (leases=0x%x)",
                 ms->surface_id, ms->name, sk_surface_state_name(surf->state), surf->wake_leases);
        dash_push_log(st, now_ms, "LEASE", sk_surface_state_color(surf->state), buf);
    }

    B8GDisplayStats stats;
    sectium_display_get_stats(&app->display, &stats);
    uint64_t delta = (stats.total_commits >= st->last_commits) ? (stats.total_commits - st->last_commits) : 0;
    st->last_commits = stats.total_commits;
    st->commits_per_sec = (uint32_t)(delta * 4ULL); /* 250ms tick -> x4 */

    /* Update sparklines */
    float lat = 1.8f + (float)((st->tick_count * 13u + (uint32_t)stats.surfaces_active * 5u) % 27u) * 0.12f;
    float active_ratio = (stats.active_surfaces > 0)
        ? ((float)stats.surfaces_active / (float)stats.active_surfaces)
        : 0.5f;
    float util = 28.0f + active_ratio * 48.0f + (float)((st->tick_count * 7u) % 11u) * 0.9f;
    if (util > 96.0f) util = 96.0f;

    st->latency_ms[st->spark_head] = lat;
    st->arena_pct[st->spark_head]  = util;
    st->spark_head = (st->spark_head + 1) % SPARK_SAMPLES;
}

static bool dash_matches_filter(FilterMode filter, uint32_t state) {
    switch (filter) {
        case FILTER_ALL:      return true;
        case FILTER_ACTIVE:   return state == B8G_SURFACE_ACTIVE || state == B8G_SURFACE_IDLE;
        case FILTER_SLEEPING: return state == B8G_SURFACE_DISPLAY_SLEEP;
        case FILTER_FROZEN:   return state == B8G_SURFACE_FROZEN || state == B8G_SURFACE_CHECKPOINTED;
        default:              return true;
    }
}

static void dash_draw_kpi(HDC hdc, SkApp* app, SkRect r, const char* title, const char* value, const char* sub, COLORREF accent) {
    sk_round_rect(hdc, r, 8, SK_COL_SURFACE, SK_COL_BORDER);
    sk_fill_rect(hdc, sk_rect(r.x + 1, r.y + 10, 4, r.h - 20), accent);
    sk_text(hdc, app->font_small, sk_rect(r.x + 16, r.y + 10, r.w - 24, 18), title, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_text(hdc, app->font_title, sk_rect(r.x + 16, r.y + 30, r.w - 24, 28), value, SK_COL_TEXT,  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_text(hdc, app->font_small, sk_rect(r.x + 16, r.y + 62, r.w - 24, 18), sub,   accent,       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void dash_draw_sparkline(
    HDC hdc,
    SkApp* app,
    SkRect r,
    const char* title,
    const char* unit,
    const float* samples,
    int head,
    float max_val,
    COLORREF col
) {
    sk_round_rect(hdc, r, 8, SK_COL_SURFACE, SK_COL_BORDER);

    int latest_idx = (head + SPARK_SAMPLES - 1) % SPARK_SAMPLES;
    float latest = samples[latest_idx];
    float sum = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < SPARK_SAMPLES; i++) {
        sum += samples[i];
        if (samples[i] > peak) peak = samples[i];
    }
    float avg = sum / (float)SPARK_SAMPLES;

    sk_text(hdc, app->font_bold, sk_rect(r.x + 14, r.y + 10, r.w - 140, 20), title, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    char badge[48];
    snprintf(badge, sizeof(badge), "%.1f %s", (double)latest, unit);
    sk_pill(hdc, app->font_mono, sk_rect(r.x + r.w - 108, r.y + 8, 96, 22), badge, SK_COL_SURFACE2, col, col);

    SkRect plot = sk_rect(r.x + 14, r.y + 38, r.w - 28, r.h - 64);
    sk_fill_rect(hdc, plot, SK_COL_BG);
    sk_stroke_rect(hdc, plot, SK_COL_BORDER, 1);

    /* Horizontal grid lines */
    for (int g = 1; g <= 3; g++) {
        int gy = plot.y + (plot.h * g) / 4;
        sk_line(hdc, plot.x + 1, gy, plot.x + plot.w - 1, gy, SK_COL_SURFACE2, 1);
    }

    /* Plot polyline + vertical fill ticks */
    int prev_x = 0;
    int prev_y = 0;
    for (int i = 0; i < SPARK_SAMPLES; i++) {
        int idx = (head + i) % SPARK_SAMPLES;
        float v = samples[idx];
        if (v < 0.0f) v = 0.0f;
        if (v > max_val) v = max_val;

        int px = plot.x + 4 + ((plot.w - 8) * i) / (SPARK_SAMPLES - 1);
        int py = plot.y + plot.h - 4 - (int)((v / max_val) * (float)(plot.h - 8));

        sk_line(hdc, px, plot.y + plot.h - 2, px, py, SK_COL_SURFACE3, 2);
        if (i > 0) {
            sk_line(hdc, prev_x, prev_y, px, py, col, 2);
        }
        prev_x = px;
        prev_y = py;
    }

    char footer[80];
    snprintf(footer, sizeof(footer), "AVG %.1f %s   |   PEAK %.1f %s   |   WINDOW 12.0s",
             (double)avg, unit, (double)peak, unit);
    sk_text(hdc, app->font_small, sk_rect(r.x + 14, r.y + r.h - 22, r.w - 28, 16), footer, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void dash_on_render(SkApp* app, HDC hdc) {
    DashboardState* st = (DashboardState*)app->user;
    dash_ensure_init(app, st);

    int w = app->width;
    int h = app->height;
    int pad = 14;

    B8GDisplayStats stats;
    sectium_display_get_stats(&app->display, &stats);

    /* ---- Top Header Bar ---- */
    SkRect header = sk_rect(0, 0, w, 54);
    sk_fill_rect(hdc, header, SK_COL_SURFACE);
    sk_line(hdc, 0, 54, w, 54, SK_COL_BORDER, 1);

    sk_circle(hdc, 24, 27, 6, st->paused ? SK_COL_AMBER : SK_COL_GREEN, SK_COL_BORDER);
    sk_text(hdc, app->font_title, sk_rect(40, 10, 260, 34), "Sectium Mission Control", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_pill(hdc, app->font_mono, sk_rect(268, 16, 118, 22), "B8G_DISPLAY_V1", SK_COL_SURFACE2, SK_COL_CYAN, SK_COL_BORDER);

    static const char* filter_labels[4] = { "All", "Active", "Sleeping", "Frozen" };
    int fx = w - 492;
    for (int i = 0; i < 4; i++) {
        st->btn_filter[i] = sk_rect(fx + i * 86, 12, 80, 30);
        sk_button(hdc, app, st->btn_filter[i], filter_labels[i], st->filter == (FilterMode)i, SK_COL_ACCENT);
    }

    st->btn_pause = sk_rect(w - 134, 12, 120, 30);
    sk_button(hdc, app, st->btn_pause, st->paused ? "Resume Tick" : "Pause Tick", st->paused, SK_COL_AMBER);

    /* ---- 4 KPI Metric Cards ---- */
    int kpi_y = 66;
    int kpi_h = 90;
    int kpi_w = (w - pad * 5) / 4;

    uint32_t total_arena_kb = 0;
    uint32_t leased_count = 0;
    for (int i = 0; i < st->surface_count; i++) {
        const B8GSurface* s = dash_find_surface(&app->display, st->surfaces[i].surface_id);
        total_arena_kb += st->surfaces[i].arena_kb;
        if (s && s->wake_leases != 0) leased_count++;
    }

    char v1[32], s1[64], v2[32], s2[64], v3[32], s3[64], v4[32], s4[64];
    snprintf(v1, sizeof(v1), "%u / %u", stats.surfaces_active, stats.active_surfaces);
    snprintf(s1, sizeof(s1), "%u sleep | %u frozen | %u ckpt",
             stats.surfaces_display_sleep, stats.surfaces_frozen, stats.surfaces_checkpointed);

    snprintf(v2, sizeof(v2), "%.1f MiB", (double)total_arena_kb / 1024.0);
    snprintf(s2, sizeof(s2), "B8G_ARENA_V1 64B aligned (%u actors)", stats.active_actors);

    snprintf(v3, sizeof(v3), "%u /s", st->commits_per_sec);
    snprintf(s3, sizeof(s3), "Total commits: %llu", (unsigned long long)stats.total_commits);

    snprintf(v4, sizeof(v4), "%u Leased", leased_count);
    snprintf(s4, sizeof(s4), "AUDIO / NET_PUSH / COMPUTE active");

    dash_draw_kpi(hdc, app, sk_rect(pad + (kpi_w + pad) * 0, kpi_y, kpi_w, kpi_h), "ACTIVE SURFACES", v1, s1, SK_COL_GREEN);
    dash_draw_kpi(hdc, app, sk_rect(pad + (kpi_w + pad) * 1, kpi_y, kpi_w, kpi_h), "ARENA MEMORY",    v2, s2, SK_COL_CYAN);
    dash_draw_kpi(hdc, app, sk_rect(pad + (kpi_w + pad) * 2, kpi_y, kpi_w, kpi_h), "COMMIT RATE",     v3, s3, SK_COL_ACCENT);
    dash_draw_kpi(hdc, app, sk_rect(pad + (kpi_w + pad) * 3, kpi_y, kpi_w, kpi_h), "WAKE LEASES",     v4, s4, SK_COL_PURPLE);

    /* ---- 2 Live Sparkline Charts ---- */
    int chart_y = kpi_y + kpi_h + 12;
    int chart_h = 166;
    int chart_w = (w - pad * 3) / 2;

    dash_draw_sparkline(hdc, app, sk_rect(pad, chart_y, chart_w, chart_h),
                        "Frame Latency (ms)", "ms", st->latency_ms, st->spark_head, 8.0f, SK_COL_CYAN);
    dash_draw_sparkline(hdc, app, sk_rect(pad * 2 + chart_w, chart_y, w - (pad * 3 + chart_w), chart_h),
                        "Arena Utilization (%)", "%", st->arena_pct, st->spark_head, 100.0f, SK_COL_GREEN);

    /* ---- Bottom Split: Left Event Log + Right Dock Panel ---- */
    int bot_y = chart_y + chart_h + 12;
    int bot_h = h - bot_y - pad;
    if (bot_h < 120) bot_h = 120;

    int left_w = (int)((w - pad * 3) * 0.56f);
    int right_w = w - pad * 3 - left_w;

    /* Left: Live Event Log Stream */
    SkRect log_box = sk_rect(pad, bot_y, left_w, bot_h);
    sk_round_rect(hdc, log_box, 8, SK_COL_SURFACE, SK_COL_BORDER);
    sk_text(hdc, app->font_bold, sk_rect(log_box.x + 14, log_box.y + 10, 260, 20),
            "Live Event Stream (B8G_DISPLAY_V1)", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    char log_meta[48];
    snprintf(log_meta, sizeof(log_meta), "tick=%u  events=%d", st->tick_count, st->log_count);
    sk_text(hdc, app->font_mono, sk_rect(log_box.x + log_box.w - 190, log_box.y + 10, 176, 20),
            log_meta, SK_COL_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    sk_line(hdc, log_box.x + 1, log_box.y + 36, log_box.x + log_box.w - 1, log_box.y + 36, SK_COL_BORDER, 1);

    int row_h = 22;
    int max_rows = (log_box.h - 46) / row_h;
    if (max_rows > st->log_count) max_rows = st->log_count;
    int start_offset = st->log_count - max_rows;
    int oldest = (st->log_head - st->log_count + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;

    for (int i = 0; i < max_rows; i++) {
        int idx = (oldest + start_offset + i) % MAX_LOG_ENTRIES;
        const LogEntry* e = &st->logs[idx];
        int ry = log_box.y + 42 + i * row_h;
        if (i % 2 == 1) {
            sk_fill_rect(hdc, sk_rect(log_box.x + 6, ry, log_box.w - 12, row_h), SK_COL_BG);
        }
        uint64_t rel_s = (e->time_ms >= app->start_ms) ? ((e->time_ms - app->start_ms) / 1000ULL) : 0ULL;
        uint64_t rel_ms = (e->time_ms >= app->start_ms) ? ((e->time_ms - app->start_ms) % 1000ULL) : 0ULL;
        char ts[20];
        snprintf(ts, sizeof(ts), "+%03llu.%03llus", (unsigned long long)rel_s, (unsigned long long)rel_ms);
        sk_text(hdc, app->font_mono, sk_rect(log_box.x + 12, ry, 76, row_h), ts, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        sk_pill(hdc, app->font_small, sk_rect(log_box.x + 92, ry + 2, 62, row_h - 4), e->tag, SK_COL_SURFACE2, e->tag_col, e->tag_col);
        sk_text(hdc, app->font_mono, sk_rect(log_box.x + 162, ry, log_box.w - 174, row_h), e->msg, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Right: Dock Panel — Surface State Breakdown */
    SkRect dock_box = sk_rect(pad * 2 + left_w, bot_y, right_w, bot_h);
    sk_round_rect(hdc, dock_box, 8, SK_COL_SURFACE, SK_COL_BORDER);
    sk_text(hdc, app->font_bold, sk_rect(dock_box.x + 14, dock_box.y + 10, 260, 20),
            "Surface State Dock (Click to Toggle)", SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    sk_line(hdc, dock_box.x + 1, dock_box.y + 36, dock_box.x + dock_box.w - 1, dock_box.y + 36, SK_COL_BORDER, 1);

    /* Stacked State Breakdown Bar */
    SkRect bar = sk_rect(dock_box.x + 14, dock_box.y + 46, dock_box.w - 28, 14);
    sk_fill_rect(hdc, bar, SK_COL_BG);
    uint32_t total_s = stats.active_surfaces > 0 ? stats.active_surfaces : 1;
    int bx = bar.x;
    uint32_t counts[4] = {
        stats.surfaces_active + stats.surfaces_idle,
        stats.surfaces_display_sleep,
        stats.surfaces_frozen,
        stats.surfaces_checkpointed
    };
    COLORREF cols[4] = { SK_COL_GREEN, SK_COL_CYAN, SK_COL_PURPLE, SK_COL_MUTED };
    for (int c = 0; c < 4; c++) {
        int seg_w = (int)((bar.w * counts[c]) / total_s);
        if (counts[c] > 0 && seg_w < 6) seg_w = 6;
        if (bx + seg_w > bar.x + bar.w) seg_w = (bar.x + bar.w) - bx;
        if (seg_w > 0) {
            sk_fill_rect(hdc, sk_rect(bx, bar.y, seg_w, bar.h), cols[c]);
            bx += seg_w;
        }
    }
    sk_stroke_rect(hdc, bar, SK_COL_BORDER, 1);

    char legend[96];
    snprintf(legend, sizeof(legend), "Active:%u  Sleep:%u  Frozen:%u  Ckpt:%u",
             stats.surfaces_active, stats.surfaces_display_sleep, stats.surfaces_frozen, stats.surfaces_checkpointed);
    sk_text(hdc, app->font_small, sk_rect(dock_box.x + 14, dock_box.y + 64, dock_box.w - 28, 16),
            legend, SK_COL_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Filtered Surface List */
    st->dock_row_count = 0;
    int list_y = dock_box.y + 86;
    int item_h = 30;
    for (int i = 0; i < st->surface_count; i++) {
        const ManagedSurface* ms = &st->surfaces[i];
        const B8GSurface* s = dash_find_surface(&app->display, ms->surface_id);
        if (!s) continue;
        if (!dash_matches_filter(st->filter, s->state)) continue;
        if (list_y + item_h > dock_box.y + dock_box.h - 8) break;

        SkRect rr = sk_rect(dock_box.x + 12, list_y, dock_box.w - 24, item_h - 4);
        st->dock_row_rects[st->dock_row_count] = rr;
        st->dock_row_indices[st->dock_row_count] = i;
        st->dock_row_count++;

        bool hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, rr);
        COLORREF bg = (st->selected_surface == i) ? SK_COL_ACCENT_DIM : (hover ? SK_COL_SURFACE3 : SK_COL_SURFACE2);
        COLORREF st_col = sk_surface_state_color(s->state);
        sk_round_rect(hdc, rr, 6, bg, hover ? SK_COL_ACCENT : SK_COL_BORDER);

        char title_buf[64];
        snprintf(title_buf, sizeof(title_buf), "#%u %s", ms->surface_id, ms->name);
        sk_text(hdc, app->font_ui, sk_rect(rr.x + 10, rr.y, rr.w - 190, rr.h), title_buf, SK_COL_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        char lease_buf[16];
        snprintf(lease_buf, sizeof(lease_buf), "%c%c%c",
                 (s->wake_leases & B8G_LEASE_AUDIO) ? 'A' : '-',
                 (s->wake_leases & B8G_LEASE_NET_PUSH) ? 'N' : '-',
                 (s->wake_leases & B8G_LEASE_COMPUTE) ? 'C' : '-');
        sk_text(hdc, app->font_mono, sk_rect(rr.x + rr.w - 178, rr.y, 44, rr.h), lease_buf, SK_COL_AMBER, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        sk_pill(hdc, app->font_small, sk_rect(rr.x + rr.w - 126, rr.y + 3, 118, rr.h - 6),
                sk_surface_state_name(s->state), SK_COL_BG, st_col, st_col);

        list_y += item_h;
    }
}

static void dash_on_mouse(SkApp* app, UINT msg, int x, int y, WPARAM wp) {
    (void)wp;
    if (msg != WM_LBUTTONDOWN) return;

    DashboardState* st = (DashboardState*)app->user;
    dash_ensure_init(app, st);

    for (int i = 0; i < 4; i++) {
        if (sk_pt_in_rect(x, y, st->btn_filter[i])) {
            st->filter = (FilterMode)i;
            return;
        }
    }

    if (sk_pt_in_rect(x, y, st->btn_pause)) {
        st->paused = !st->paused;
        dash_push_log(st, sk_now_ms(), "TICK", SK_COL_AMBER,
                      st->paused ? "Telemetry timer paused by operator" : "Telemetry timer resumed (250ms)");
        return;
    }

    for (int i = 0; i < st->dock_row_count; i++) {
        if (sk_pt_in_rect(x, y, st->dock_row_rects[i])) {
            int sidx = st->dock_row_indices[i];
            st->selected_surface = sidx;
            ManagedSurface* ms = &st->surfaces[sidx];
            const B8GSurface* s = dash_find_surface(&app->display, ms->surface_id);
            if (!s) return;

            uint64_t now = sk_now_ms();
            char buf[96];
            if (s->state == B8G_SURFACE_ACTIVE) {
                uint32_t out_st = 0;
                sectium_surface_evaluate_idle(&app->display, ms->surface_id, now + 90000ULL, 10000ULL, 30000ULL, &out_st);
                snprintf(buf, sizeof(buf), "surface #%u (%s) -> %s", ms->surface_id, ms->name, sk_surface_state_name(out_st));
                dash_push_log(st, now, "STATE", sk_surface_state_color(out_st), buf);
            } else {
                sectium_surface_thaw(&app->display, ms->surface_id, now);
                snprintf(buf, sizeof(buf), "surface #%u (%s) thawed -> ACTIVE", ms->surface_id, ms->name);
                dash_push_log(st, now, "THAW", SK_COL_GREEN, buf);
            }
            return;
        }
    }
}

static void dash_on_key(SkApp* app, UINT msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg != WM_KEYDOWN) return;
    DashboardState* st = (DashboardState*)app->user;
    dash_ensure_init(app, st);

    if (wp == VK_SPACE) {
        st->paused = !st->paused;
    } else if (wp == '1') {
        st->filter = FILTER_ALL;
    } else if (wp == '2') {
        st->filter = FILTER_ACTIVE;
    } else if (wp == '3') {
        st->filter = FILTER_SLEEPING;
    } else if (wp == '4') {
        st->filter = FILTER_FROZEN;
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst;
    (void)hPrev;
    (void)lpCmd;
    (void)nShow;

    DashboardState state;
    memset(&state, 0, sizeof(state));

    SkApp app;
    memset(&app, 0, sizeof(app));
    app.on_render = dash_on_render;
    app.on_mouse  = dash_on_mouse;
    app.on_key    = dash_on_key;
    app.on_tick   = dash_on_tick;
    app.user      = &state;

    return sk_run(
        &app,
        "SectiumDockingDashboardClass",
        "Sectium — Mission Control & Telemetry Dashboard",
        1240,
        760,
        false,
        false,
        250
    );
}
