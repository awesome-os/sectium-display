export type ActorKind = 'human' | 'agent' | 'worker';

export type StreamKind = 'surface' | 'avatar' | 'dialog' | 'worker-io';

export const LEASE_NONE = 0;
export const LEASE_AUDIO = 1;
export const LEASE_NET_PUSH = 2;
export const LEASE_COMPUTE = 4;

export const WakeLeaseFlag = {
  LEASE_NONE,
  LEASE_AUDIO,
  LEASE_NET_PUSH,
  LEASE_COMPUTE,
} as const;

export type WakeLeaseFlagKey = keyof typeof WakeLeaseFlag;
export type WakeLeaseFlagValue = (typeof WakeLeaseFlag)[WakeLeaseFlagKey];

export type SurfaceState =
  | 'ACTIVE'
  | 'IDLE'
  | 'DISPLAY_SLEEP'
  | 'FROZEN'
  | 'CHECKPOINTED';

export type CompositorMode = 'tiling-2d' | 'open-world-3d';

export type SurfaceKind = 'web' | 'vscode' | 'terminal' | 'game' | 'canvas';

export interface WorldPose {
  x: number;
  y: number;
  z: number;
  yaw?: number;
  pitch?: number;
  roll?: number;
  scale?: number;
  deskId?: string;
}

export interface ActorRecord {
  actorId: bigint;
  kind: ActorKind;
  name: string;
  avatarRef?: string;
  worldPose?: WorldPose;
  metadata?: Record<string, unknown>;
  createdAt: number;
}

export interface SurfaceRecord {
  surfaceId: string;
  arenaId: bigint;
  actorId: bigint;
  kind: SurfaceKind;
  title: string;
  mode: CompositorMode;
  leases: number;
  state: SurfaceState;
  splitRatio: number;
  worldPose?: WorldPose;
  tabGroup?: string;
  idleMs: number;
  focused: boolean;
  checkpointCount: number;
  metadata?: Record<string, unknown>;
}

export interface StreamBinding {
  bindingId: string;
  actorId: bigint;
  streamKind: StreamKind;
  refId: string;
  createdAt: number;
}

export interface DialogMessage {
  messageId: string;
  fromActorId: bigint;
  toActorId: bigint;
  text: string;
  timestamp: number;
  surfaceId?: string;
}

export interface ViewportSize {
  width: number;
  height: number;
}

export interface SurfaceLayoutRect {
  surfaceId: string;
  arenaId: bigint;
  actorId: bigint;
  mode: CompositorMode;
  x: number;
  y: number;
  width: number;
  height: number;
  splitRatio: number;
  tabGroup?: string;
  worldPose?: WorldPose;
}

export interface WmLayoutResult {
  mode: CompositorMode;
  tabGroups: Record<string, string[]>;
  placements: SurfaceLayoutRect[];
}

export interface RebindTarget {
  splitRatio?: number;
  tabGroup?: string;
  worldPose?: WorldPose;
  actorId?: bigint;
}

export interface WmKeyEvent {
  key: string;
  modifiers?: {
    ctrl?: boolean;
    alt?: boolean;
    shift?: boolean;
    meta?: boolean;
  };
  surfaceId?: string;
}

export interface WmHookContext {
  getActor(actorId: bigint): ActorRecord | undefined;
  listActors(): readonly ActorRecord[];
  getSurface(surfaceId: string): SurfaceRecord | undefined;
  listSurfaces(): readonly SurfaceRecord[];
  getActorBindings(actorId: bigint, streamKind?: StreamKind): readonly StreamBinding[];
  rebindSurface(
    surfaceId: string,
    targetMode: CompositorMode,
    target?: RebindTarget,
  ): SurfaceRecord;
}

export type SurfaceOpenPatch = Partial<
  Pick<SurfaceRecord, 'mode' | 'splitRatio' | 'tabGroup' | 'worldPose' | 'leases'>
>;

export interface WmScriptHooks {
  name?: string;
  onSurfaceOpen?: (
    surface: SurfaceRecord,
    ctx: WmHookContext,
  ) => SurfaceOpenPatch | void;
  onFocusChange?: (
    focused: SurfaceRecord | null,
    previous: SurfaceRecord | null,
    ctx: WmHookContext,
  ) => void;
  onKey?: (event: WmKeyEvent, ctx: WmHookContext) => boolean | void;
  layout?: (
    surfaces: readonly SurfaceRecord[],
    viewport: ViewportSize,
  ) => WmLayoutResult;
  onIdle?: (
    surface: SurfaceRecord,
    leases: number,
    idleMs: number,
  ) => SurfaceState | void;
  onActorSpeak?: (message: DialogMessage, ctx: WmHookContext) => void;
  onSurfaceRebind?: (
    surface: SurfaceRecord,
    previousMode: CompositorMode,
    targetMode: CompositorMode,
    target?: RebindTarget,
  ) => void;
}
