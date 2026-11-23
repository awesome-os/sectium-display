#ifndef SECTIUM_SHELL_KIT_H
#define SECTIUM_SHELL_KIT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTIUM_DISPLAY_STATIC
#include "../../sections/sectium-display/sectium_display.h"

/* ============================================================================
 * Sectium Shell Kit — Minimal Zero-Bloat Win32 + B8G_DISPLAY_V1 Micro-Toolkit
 * Shared boilerplate foundation for E:\awesome-os\gui-shells\*.exe
 * ============================================================================ */

/* Awesome-OS Live Theme Tokens (COLORREF = 0x00BBGGRR) — Press Ctrl+T to cycle */
#define SK_RGB(r, g, b) ((COLORREF)(((uint32_t)(r)) | (((uint32_t)(g)) << 8) | (((uint32_t)(b)) << 16)))

typedef struct SkThemeTokens {
    const char* name;
    COLORREF bg;
    COLORREF surface;
    COLORREF surface2;
    COLORREF surface3;
    COLORREF border;
    COLORREF text;
    COLORREF muted;
    COLORREF accent;
    COLORREF accent_dim;
    COLORREF green;
    COLORREF amber;
    COLORREF purple;
    COLORREF red;
    COLORREF cyan;
} SkThemeTokens;

static const SkThemeTokens SK_THEMES[6] = {
    { "Awesome-OS Midnight",
      SK_RGB(15, 18, 26),  SK_RGB(22, 27, 38),  SK_RGB(30, 36, 50),  SK_RGB(40, 48, 66),  SK_RGB(52, 62, 84),
      SK_RGB(232, 238, 248), SK_RGB(142, 154, 178), SK_RGB(88, 166, 255), SK_RGB(36, 74, 128),
      SK_RGB(63, 185, 120), SK_RGB(240, 180, 64), SK_RGB(178, 126, 255), SK_RGB(244, 96, 108), SK_RGB(72, 214, 224) },
    { "Tokyo Night",
      SK_RGB(26, 27, 38),  SK_RGB(31, 35, 53),  SK_RGB(36, 40, 59),  SK_RGB(47, 53, 77),  SK_RGB(65, 72, 104),
      SK_RGB(192, 202, 245), SK_RGB(134, 145, 184), SK_RGB(122, 162, 247), SK_RGB(46, 60, 100),
      SK_RGB(158, 206, 106), SK_RGB(224, 175, 104), SK_RGB(187, 154, 247), SK_RGB(247, 118, 142), SK_RGB(125, 207, 255) },
    { "Catppuccin Mocha",
      SK_RGB(30, 30, 46),  SK_RGB(24, 24, 37),  SK_RGB(49, 50, 68),  SK_RGB(69, 71, 90),  SK_RGB(88, 91, 112),
      SK_RGB(205, 214, 244), SK_RGB(166, 173, 200), SK_RGB(137, 180, 250), SK_RGB(56, 72, 110),
      SK_RGB(166, 227, 161), SK_RGB(249, 226, 175), SK_RGB(203, 166, 247), SK_RGB(243, 139, 168), SK_RGB(148, 226, 213) },
    { "Gruvbox Dark",
      SK_RGB(40, 40, 40),  SK_RGB(50, 48, 47),  SK_RGB(60, 56, 54),  SK_RGB(80, 73, 69),  SK_RGB(102, 92, 84),
      SK_RGB(235, 219, 178), SK_RGB(168, 153, 132), SK_RGB(254, 128, 25), SK_RGB(108, 60, 24),
      SK_RGB(184, 187, 38), SK_RGB(250, 189, 47), SK_RGB(211, 134, 155), SK_RGB(251, 73, 52), SK_RGB(142, 192, 124) },
    { "Nord",
      SK_RGB(46, 52, 64),  SK_RGB(59, 66, 82),  SK_RGB(67, 76, 94),  SK_RGB(76, 86, 106), SK_RGB(94, 106, 130),
      SK_RGB(236, 239, 244), SK_RGB(168, 178, 194), SK_RGB(136, 192, 208), SK_RGB(62, 92, 108),
      SK_RGB(163, 190, 140), SK_RGB(235, 203, 139), SK_RGB(180, 142, 173), SK_RGB(191, 97, 106), SK_RGB(143, 188, 187) },
    { "Rose Pine",
      SK_RGB(25, 23, 36),  SK_RGB(31, 29, 46),  SK_RGB(38, 35, 58),  SK_RGB(54, 50, 80),  SK_RGB(82, 79, 103),
      SK_RGB(224, 222, 244), SK_RGB(144, 140, 170), SK_RGB(235, 188, 186), SK_RGB(98, 68, 76),
      SK_RGB(156, 207, 216), SK_RGB(246, 193, 119), SK_RGB(196, 167, 231), SK_RGB(235, 111, 146), SK_RGB(49, 116, 143) }
};

static uint32_t sk_theme_index = 0;
static uint64_t sk_theme_toast_until_ms = 0;

static inline const SkThemeTokens* sk_theme(void) {
    return &SK_THEMES[sk_theme_index % 6u];
}

static inline void sk_cycle_theme(uint64_t now_ms) {
    sk_theme_index = (sk_theme_index + 1u) % 6u;
    sk_theme_toast_until_ms = now_ms + 2200u;
}

#define SK_COL_BG         (sk_theme()->bg)
#define SK_COL_SURFACE    (sk_theme()->surface)
#define SK_COL_SURFACE2   (sk_theme()->surface2)
#define SK_COL_SURFACE3   (sk_theme()->surface3)
#define SK_COL_BORDER     (sk_theme()->border)
#define SK_COL_TEXT       (sk_theme()->text)
#define SK_COL_MUTED      (sk_theme()->muted)
#define SK_COL_ACCENT     (sk_theme()->accent)
#define SK_COL_ACCENT_DIM (sk_theme()->accent_dim)
#define SK_COL_GREEN      (sk_theme()->green)
#define SK_COL_AMBER      (sk_theme()->amber)
#define SK_COL_PURPLE     (sk_theme()->purple)
#define SK_COL_RED        (sk_theme()->red)
#define SK_COL_CYAN       (sk_theme()->cyan)

typedef struct SkRect {
    int x;
    int y;
    int w;
    int h;
} SkRect;

static inline SkRect sk_rect(int x, int y, int w, int h) {
    SkRect r = { x, y, w, h };
    return r;
}

static inline bool sk_pt_in_rect(int px, int py, SkRect r) {
    return px >= r.x && px < (r.x + r.w) && py >= r.y && py < (r.y + r.h);
}

typedef struct SkApp SkApp;

typedef void (*SkRenderFn)(SkApp* app, HDC hdc);
typedef void (*SkMouseFn)(SkApp* app, UINT msg, int x, int y, WPARAM wp);
typedef void (*SkKeyFn)(SkApp* app, UINT msg, WPARAM wp, LPARAM lp);
typedef void (*SkTickFn)(SkApp* app, uint64_t now_ms);

struct SkApp {
    HWND          hwnd;
    int           width;
    int           height;
    int           mouse_x;
    int           mouse_y;
    bool          mouse_down;
    bool          frameless;
    bool          always_on_top;
    uint64_t      start_ms;
    uint64_t      now_ms;
    uint32_t      human_actor_id;
    uint32_t      root_surface_id;
    B8GDisplayCtx display;

    /* Shared fonts */
    HFONT         font_ui;
    HFONT         font_bold;
    HFONT         font_title;
    HFONT         font_mono;
    HFONT         font_small;

    /* Callbacks & user state */
    SkRenderFn    on_render;
    SkMouseFn     on_mouse;
    SkKeyFn       on_key;
    SkTickFn      on_tick;
    void*         user;
};

static inline uint64_t sk_now_ms(void) {
    return (uint64_t)GetTickCount64();
}

/* ---------- Drawing Primitives ---------- */

static inline void sk_fill_rect(HDC hdc, SkRect r, COLORREF col) {
    if (r.w <= 0 || r.h <= 0) return;
    HBRUSH br = CreateSolidBrush(col);
    RECT wr = { r.x, r.y, r.x + r.w, r.y + r.h };
    FillRect(hdc, &wr, br);
    DeleteObject(br);
}

static inline void sk_stroke_rect(HDC hdc, SkRect r, COLORREF col, int thickness) {
    if (r.w <= 0 || r.h <= 0) return;
    HPEN pen = CreatePen(PS_SOLID, thickness, col);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_br = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.x, r.y, r.x + r.w, r.y + r.h);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static inline void sk_round_rect(HDC hdc, SkRect r, int radius, COLORREF fill, COLORREF border) {
    if (r.w <= 0 || r.h <= 0) return;
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_br = SelectObject(hdc, br);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    RoundRect(hdc, r.x, r.y, r.x + r.w, r.y + r.h, radius * 2, radius * 2);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(pen);
    DeleteObject(br);
}

static inline void sk_circle(HDC hdc, int cx, int cy, int rad, COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, border);
    HGDIOBJ old_br = SelectObject(hdc, br);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    Ellipse(hdc, cx - rad, cy - rad, cx + rad, cy + rad);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(pen);
    DeleteObject(br);
}

static inline void sk_line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF col, int thickness) {
    HPEN pen = CreatePen(PS_SOLID, thickness, col);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, NULL);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static inline void sk_text(HDC hdc, HFONT font, SkRect r, const char* text, COLORREF col, UINT fmt) {
    if (!text) return;
    HGDIOBJ old_f = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, col);
    RECT wr = { r.x, r.y, r.x + r.w, r.y + r.h };
    DrawTextA(hdc, text, -1, &wr, fmt | DT_END_ELLIPSIS);
    SelectObject(hdc, old_f);
}

static inline void sk_pill(HDC hdc, HFONT font, SkRect r, const char* label, COLORREF bg, COLORREF fg, COLORREF border) {
    sk_round_rect(hdc, r, r.h / 2, bg, border);
    sk_text(hdc, font, r, label, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static inline void sk_button(HDC hdc, SkApp* app, SkRect r, const char* label, bool active, COLORREF accent) {
    bool hover = sk_pt_in_rect(app->mouse_x, app->mouse_y, r);
    COLORREF bg = active ? accent : (hover ? SK_COL_SURFACE3 : SK_COL_SURFACE2);
    COLORREF bd = active ? accent : (hover ? SK_COL_ACCENT : SK_COL_BORDER);
    COLORREF fg = active ? SK_COL_BG : SK_COL_TEXT;
    sk_round_rect(hdc, r, 6, bg, bd);
    sk_text(hdc, active ? app->font_bold : app->font_ui, r, label, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static inline const char* sk_surface_state_name(uint32_t state) {
    switch (state) {
        case B8G_SURFACE_ACTIVE:        return "ACTIVE";
        case B8G_SURFACE_IDLE:          return "IDLE";
        case B8G_SURFACE_DISPLAY_SLEEP: return "DISPLAY_SLEEP";
        case B8G_SURFACE_FROZEN:        return "FROZEN";
        case B8G_SURFACE_CHECKPOINTED:  return "CHECKPOINTED";
        default:                        return "UNKNOWN";
    }
}

static inline COLORREF sk_surface_state_color(uint32_t state) {
    switch (state) {
        case B8G_SURFACE_ACTIVE:        return SK_COL_GREEN;
        case B8G_SURFACE_IDLE:          return SK_COL_AMBER;
        case B8G_SURFACE_DISPLAY_SLEEP: return SK_COL_CYAN;
        case B8G_SURFACE_FROZEN:        return SK_COL_PURPLE;
        case B8G_SURFACE_CHECKPOINTED:  return SK_COL_MUTED;
        default:                        return SK_COL_MUTED;
    }
}

/* ---------- Window Procedure & Main Loop ---------- */

static LRESULT CALLBACK sk_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SkApp* app = (SkApp*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTA* cs = (CREATESTRUCTA*)lp;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return TRUE;
    }
    if (!app) return DefWindowProcA(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_SIZE:
            app->width = LOWORD(lp);
            app->height = HIWORD(lp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_TIMER:
            app->now_ms = sk_now_ms();
            if (app->on_tick) app->on_tick(app, app->now_ms);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            app->mouse_x = x;
            app->mouse_y = y;
            if (msg == WM_LBUTTONDOWN) {
                app->mouse_down = true;
                SetCapture(hwnd);
            } else if (msg == WM_LBUTTONUP) {
                app->mouse_down = false;
                ReleaseCapture();
            }
            if (app->on_mouse) app->on_mouse(app, msg, x, y, wp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_KEYDOWN:
            if (wp == 'T' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                sk_cycle_theme(sk_now_ms());
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (app->on_key) app->on_key(app, msg, wp, lp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_CHAR:
            if (wp == 0x14) return 0; /* swallow Ctrl+T control char */
            if (app->on_key) app->on_key(app, msg, wp, lp);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            int w = app->width > 0 ? app->width : 1;
            int h = app->height > 0 ? app->height : 1;

            /* Double-buffered 32-bit DIBSection (zero flicker, B8G_ARENA_V1 layout) */
            HDC mem_dc = CreateCompatibleDC(hdc);
            BITMAPINFO bmi;
            memset(&bmi, 0, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h; /* top-down */
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = NULL;
            HBITMAP bmp = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
            HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

            sk_fill_rect(mem_dc, sk_rect(0, 0, w, h), SK_COL_BG);
            if (app->on_render) {
                app->on_render(app, mem_dc);
            }
            if (sk_now_ms() < sk_theme_toast_until_ms) {
                char toast[96];
                snprintf(toast, sizeof(toast), "Theme: %s  (Ctrl+T)", sk_theme()->name);
                SkRect tr = sk_rect(w - 270, 10, 258, 28);
                if (tr.x < 8) { tr.x = 8; tr.w = w - 16; }
                sk_pill(mem_dc, app->font_bold, tr, toast, SK_COL_SURFACE2, SK_COL_ACCENT, SK_COL_ACCENT);
            }

            /* Record commit into B8G_DISPLAY_V1 context */
            if (app->root_surface_id != 0) {
                uint64_t seq = 0;
                sectium_surface_damage(&app->display, app->root_surface_id, 0, 0, (uint32_t)w, (uint32_t)h);
                sectium_surface_commit(&app->display, app->root_surface_id, sk_now_ms(), &seq);
            }

            BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_bmp);
            DeleteObject(bmp);
            DeleteDC(mem_dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static inline int sk_run(
    SkApp* app,
    const char* class_name,
    const char* title,
    int width,
    int height,
    bool frameless,
    bool always_on_top,
    UINT tick_ms
) {
    SetProcessDPIAware();

    app->width = width;
    app->height = height;
    app->frameless = frameless;
    app->always_on_top = always_on_top;
    app->start_ms = sk_now_ms();
    app->now_ms = app->start_ms;

    /* Initialize B8G_DISPLAY_V1 + default human actor + root surface */
    sectium_display_init(&app->display);
    sectium_actor_register(&app->display, B8G_ACTOR_HUMAN, "human-primary", &app->human_actor_id);
    sectium_surface_create(
        &app->display,
        app->human_actor_id,
        (uint32_t)width,
        (uint32_t)height,
        B8G_COMP_TILING_2D,
        &app->root_surface_id
    );
    uint32_t bind_id = 0;
    sectium_actor_bind_stream(
        &app->display,
        app->human_actor_id,
        app->root_surface_id,
        B8G_STREAM_SURFACE,
        &bind_id
    );

    app->font_ui    = CreateFontA(-14, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app->font_bold  = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app->font_title = CreateFontA(-18, 0, 0, 0, FW_BOLD,     FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app->font_mono  = CreateFontA(-13, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    app->font_small = CreateFontA(-12, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    HINSTANCE hinst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = sk_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hinst, MAKEINTRESOURCEA(101));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = class_name;
    RegisterClassExA(&wc);

    DWORD style = frameless ? (WS_POPUP | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    DWORD ex_style = always_on_top ? WS_EX_TOPMOST : 0;

    int sx = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    if (sx < 40) sx = 40;
    if (sy < 40) sy = 40;

    app->hwnd = CreateWindowExA(
        ex_style,
        class_name,
        title,
        style,
        sx, sy, width, height,
        NULL, NULL, hinst, app
    );

    if (tick_ms > 0) {
        SetTimer(app->hwnd, 1, tick_ms, NULL);
    }

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    DeleteObject(app->font_ui);
    DeleteObject(app->font_bold);
    DeleteObject(app->font_title);
    DeleteObject(app->font_mono);
    DeleteObject(app->font_small);
    return (int)msg.wParam;
}

#endif /* SECTIUM_SHELL_KIT_H */
