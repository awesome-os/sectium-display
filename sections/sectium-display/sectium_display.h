#ifndef SECTIUM_DISPLAY_H
#define SECTIUM_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * B8G_DISPLAY_V1 — Cross-Platform Display & Actor-Stream Protocol ABI
 * Zero-dependency C11 Wayland alternative for Windows, Linux, macOS, and WASI.
 * ============================================================================ */

#define B8G_DISPLAY_MAGIC_V1      0x31444742u /* "BGD1" (little-endian) */
#define B8G_DISPLAY_ABI_VERSION   1u

#define B8G_DISPLAY_MAX_ACTORS    64u
#define B8G_DISPLAY_MAX_SURFACES  128u
#define B8G_DISPLAY_MAX_BINDINGS  256u
#define B8G_DISPLAY_MAX_NAME      64u
#define B8G_DISPLAY_MAX_DAMAGE    16u
#define B8G_DISPLAY_ARENA_ALIGN   64u

#if defined(SECTIUM_DISPLAY_STATIC)
#  define B8G_DISPLAY_EXPORT
#elif defined(_WIN32) || defined(__CYGWIN__)
#  define B8G_DISPLAY_EXPORT __declspec(dllexport)
#else
#  define B8G_DISPLAY_EXPORT __attribute__((visibility("default")))
#endif

typedef enum B8GDisplayError {
    B8G_DISPLAY_OK                =  0,
    B8G_DISPLAY_ERR_INVALID_ARG   = -1,
    B8G_DISPLAY_ERR_NOT_FOUND     = -2,
    B8G_DISPLAY_ERR_CAPACITY      = -3,
    B8G_DISPLAY_ERR_INVALID_STATE = -4,
    B8G_DISPLAY_ERR_UNALIGNED     = -5
} B8GDisplayError;

typedef enum B8GActorKind {
    B8G_ACTOR_HUMAN  = 1,
    B8G_ACTOR_AGENT  = 2,
    B8G_ACTOR_WORKER = 3
} B8GActorKind;

typedef enum B8GStreamKind {
    B8G_STREAM_SURFACE   = 1,
    B8G_STREAM_AVATAR    = 2,
    B8G_STREAM_DIALOG    = 3,
    B8G_STREAM_WORKER_IO = 4
} B8GStreamKind;

typedef enum B8GWakeLease {
    B8G_LEASE_NONE     = 0u,
    B8G_LEASE_AUDIO    = (1u << 0),
    B8G_LEASE_NET_PUSH = (1u << 1),
    B8G_LEASE_COMPUTE  = (1u << 2)
} B8GWakeLease;

#define B8G_LEASE_ALL_MASK (B8G_LEASE_AUDIO | B8G_LEASE_NET_PUSH | B8G_LEASE_COMPUTE)

typedef enum B8GSurfaceState {
    B8G_SURFACE_ACTIVE        = 0,
    B8G_SURFACE_IDLE          = 1,
    B8G_SURFACE_DISPLAY_SLEEP = 2,
    B8G_SURFACE_FROZEN        = 3,
    B8G_SURFACE_CHECKPOINTED  = 4
} B8GSurfaceState;

typedef enum B8GCompositorMode {
    B8G_COMP_TILING_2D     = 0,
    B8G_COMP_OPEN_WORLD_3D = 1
} B8GCompositorMode;

typedef struct B8GDamageRect {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
} B8GDamageRect;

typedef struct B8GWorldPose {
    float pos_x;
    float pos_y;
    float pos_z;
    float rot_x;
    float rot_y;
    float rot_z;
    float rot_w;
    float scale;
} B8GWorldPose;

typedef struct B8GActor {
    uint8_t  active;
    uint8_t  _pad[3];
    uint32_t actor_id;
    uint32_t kind;
    uint32_t binding_count;
    char     name[B8G_DISPLAY_MAX_NAME];
} B8GActor;

typedef struct B8GSurface {
    uint8_t       active;
    uint8_t       _pad[3];
    uint32_t      surface_id;
    uint32_t      owner_actor_id;
    uint32_t      width;
    uint32_t      height;
    uint32_t      stride;
    uint32_t      state;
    uint32_t      compositor_mode;
    uint32_t      wake_leases;
    uint32_t      damage_count;
    uint32_t      rebind_count;
    uint64_t      arena_offset;
    uint64_t      arena_size;
    uint64_t      commit_seq;
    uint64_t      last_active_ms;
    uint64_t      checkpoint_epoch;
    B8GWorldPose  pose;
    B8GDamageRect damage[B8G_DISPLAY_MAX_DAMAGE];
} B8GSurface;

typedef struct B8GStreamBinding {
    uint8_t  active;
    uint8_t  _pad[3];
    uint32_t binding_id;
    uint32_t actor_id;
    uint32_t surface_id;
    uint32_t stream_kind;
} B8GStreamBinding;

typedef struct B8GDisplayStats {
    uint32_t abi_magic;
    uint32_t abi_version;
    uint32_t active_actors;
    uint32_t active_surfaces;
    uint32_t active_bindings;
    uint32_t surfaces_active;
    uint32_t surfaces_idle;
    uint32_t surfaces_display_sleep;
    uint32_t surfaces_frozen;
    uint32_t surfaces_checkpointed;
    uint32_t surfaces_2d;
    uint32_t surfaces_3d;
    uint64_t total_commits;
    uint64_t total_damage_rects;
    uint64_t total_rebinds;
    uint64_t total_checkpoints;
} B8GDisplayStats;

typedef struct B8GDisplayCtx {
    uint32_t         abi_magic;
    uint32_t         abi_version;
    uint32_t         next_actor_id;
    uint32_t         next_surface_id;
    uint32_t         next_binding_id;
    uint32_t         _pad;
    uint64_t         checkpoint_counter;
    uint64_t         total_commits;
    uint64_t         total_damage_rects;
    uint64_t         total_rebinds;
    uint64_t         total_checkpoints;
    B8GActor         actors[B8G_DISPLAY_MAX_ACTORS];
    B8GSurface       surfaces[B8G_DISPLAY_MAX_SURFACES];
    B8GStreamBinding bindings[B8G_DISPLAY_MAX_BINDINGS];
} B8GDisplayCtx;

/* ============================================================================
 * Public C11 ABI Functions
 * ============================================================================ */

B8G_DISPLAY_EXPORT int32_t sectium_display_init(B8GDisplayCtx* ctx);

B8G_DISPLAY_EXPORT int32_t sectium_actor_register(
    B8GDisplayCtx* ctx,
    uint32_t kind,
    const char* name,
    uint32_t* out_actor_id
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_create(
    B8GDisplayCtx* ctx,
    uint32_t owner_actor_id,
    uint32_t width,
    uint32_t height,
    uint32_t compositor_mode,
    uint32_t* out_surface_id
);

B8G_DISPLAY_EXPORT int32_t sectium_actor_bind_stream(
    B8GDisplayCtx* ctx,
    uint32_t actor_id,
    uint32_t surface_id,
    uint32_t stream_kind,
    uint32_t* out_binding_id
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_attach_arena(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    uint64_t arena_offset,
    uint64_t arena_size,
    uint32_t stride
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_damage(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_commit(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    uint64_t now_ms,
    uint64_t* out_commit_seq
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_set_leases(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    uint32_t lease_mask
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_evaluate_idle(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    uint64_t now_ms,
    uint64_t idle_threshold_ms,
    uint64_t freeze_threshold_ms,
    uint32_t* out_state
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_thaw(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    uint64_t now_ms
);

B8G_DISPLAY_EXPORT int32_t sectium_surface_rebind_mode(
    B8GDisplayCtx* ctx,
    uint32_t surface_id,
    uint32_t new_mode,
    const B8GWorldPose* pose
);

B8G_DISPLAY_EXPORT int32_t sectium_display_get_stats(
    const B8GDisplayCtx* ctx,
    B8GDisplayStats* out_stats
);

#ifdef __cplusplus
}
#endif

#endif /* SECTIUM_DISPLAY_H */
