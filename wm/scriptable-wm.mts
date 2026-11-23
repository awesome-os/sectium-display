import {
  LEASE_AUDIO,
  LEASE_COMPUTE,
  LEASE_NET_PUSH,
  LEASE_NONE,
} from './wm-types.mts';
import type {
  ActorKind,
  ActorRecord,
  CompositorMode,
  DialogMessage,
  RebindTarget,
  StreamBinding,
  StreamKind,
  SurfaceKind,
  SurfaceLayoutRect,
  SurfaceRecord,
  SurfaceState,
  ViewportSize,
  WmHookContext,
  WmKeyEvent,
  WmLayoutResult,
  WmScriptHooks,
  WorldPose,
} from './wm-types.mts';

export const DEFAULT_HUMAN_ACTOR_ID = 1n;
const LEASE_MASK = LEASE_AUDIO | LEASE_NET_PUSH | LEASE_COMPUTE;
const DEFAULT_CHECKPOINT_THRESHOLD_MS = 60_000;

export interface RegisterActorInput {
  actorId?: bigint;
  kind: ActorKind;
  name: string;
  avatarRef?: string;
  worldPose?: WorldPose;
  metadata?: Record<string, unknown>;
}

export interface OpenSurfaceInput {
  surfaceId?: string;
  arenaId?: bigint;
  actorId?: bigint;
  kind: SurfaceKind;
  title: string;
  mode?: CompositorMode;
  leases?: number;
  splitRatio?: number;
  worldPose?: WorldPose;
  tabGroup?: string;
  metadata?: Record<string, unknown>;
}

export interface TickIdleOptions {
  checkpointThresholdMs?: number;
}

export interface ScriptableWindowManagerOptions {
  hooks?: WmScriptHooks;
  viewport?: ViewportSize;
  humanName?: string;
}

function defaultHorizontalTilingLayout(
  surfaces: readonly SurfaceRecord[],
  viewport: ViewportSize,
): WmLayoutResult {
  const tabGroups: Record<string, string[]> = {};
  const tiled: SurfaceRecord[] = [];
  const worldSurfaces: SurfaceRecord[] = [];

  for (const surface of surfaces) {
    const group = surface.tabGroup ?? 'default';
    if (!tabGroups[group]) {
      tabGroups[group] = [];
    }
    tabGroups[group].push(surface.surfaceId);

    if (surface.mode === 'open-world-3d') {
      worldSurfaces.push(surface);
    } else {
      tiled.push(surface);
    }
  }

  const placements: SurfaceLayoutRect[] = [];
  const ratioSum = tiled.reduce(
    (sum, s) => sum + (s.splitRatio > 0 ? s.splitRatio : 1),
    0,
  );

  let cursorX = 0;
  for (const surface of tiled) {
    const weight = surface.splitRatio > 0 ? surface.splitRatio : 1;
    const normalized = ratioSum > 0 ? weight / ratioSum : 1 / Math.max(tiled.length, 1);
    const width = Math.round(viewport.width * normalized);
    placements.push({
      surfaceId: surface.surfaceId,
      arenaId: surface.arenaId,
      actorId: surface.actorId,
      mode: 'tiling-2d',
      x: cursorX,
      y: 0,
      width,
      height: viewport.height,
      splitRatio: normalized,
      tabGroup: surface.tabGroup,
    });
    cursorX += width;
  }

  for (const surface of worldSurfaces) {
    placements.push({
      surfaceId: surface.surfaceId,
      arenaId: surface.arenaId,
      actorId: surface.actorId,
      mode: 'open-world-3d',
      x: surface.worldPose?.x ?? 0,
      y: surface.worldPose?.y ?? 0,
      width: 0,
      height: 0,
      splitRatio: surface.splitRatio,
      tabGroup: surface.tabGroup,
      worldPose: surface.worldPose,
    });
  }

  const mode: CompositorMode =
    tiled.length === 0 && worldSurfaces.length > 0 ? 'open-world-3d' : 'tiling-2d';

  return { mode, tabGroups, placements };
}

export function createDefaultCodingWmScript(): WmScriptHooks {
  return {
    name: 'default-coding-wm',
    onSurfaceOpen(surface) {
      const tabGroupByKind: Record<SurfaceKind, string> = {
        vscode: 'editor',
        terminal: 'terminal',
        web: 'browser',
        game: 'stage',
        canvas: 'stage',
      };
      return {
        mode: surface.mode ?? 'tiling-2d',
        tabGroup: surface.tabGroup ?? tabGroupByKind[surface.kind],
        splitRatio: surface.splitRatio > 0 ? surface.splitRatio : 0.5,
      };
    },
    layout(surfaces, viewport) {
      return defaultHorizontalTilingLayout(surfaces, viewport);
    },
  };
}

export function createOpenWorldSwarmWmScript(): WmScriptHooks {
  return {
    name: 'open-world-swarm-wm',
    onSurfaceOpen(surface, ctx) {
      const actor = ctx.getActor(surface.actorId);
      const isAgentOrWorker = actor?.kind === 'agent' || actor?.kind === 'worker';
      if (isAgentOrWorker) {
        const deskIndex = Number(surface.actorId);
        const worldPose: WorldPose = surface.worldPose ?? {
          x: deskIndex * 4,
          y: 0,
          z: -3,
          yaw: 0,
          deskId: `desk-${surface.actorId.toString()}`,
        };
        return {
          mode: 'open-world-3d',
          tabGroup: surface.tabGroup ?? `actor-${surface.actorId.toString()}`,
          worldPose,
        };
      }
      return {
        mode: surface.mode ?? 'tiling-2d',
        tabGroup: surface.tabGroup ?? 'human-main',
      };
    },
    onFocusChange(focused, _previous, ctx) {
      if (focused && focused.mode === 'open-world-3d') {
        ctx.rebindSurface(focused.surfaceId, 'tiling-2d', {
          splitRatio: 0.5,
          tabGroup: 'pulled-focus',
        });
      }
    },
    layout(surfaces, viewport) {
      const result = defaultHorizontalTilingLayout(surfaces, viewport);
      const hasWorld = surfaces.some((s) => s.mode === 'open-world-3d');
      return {
        ...result,
        mode: hasWorld && !surfaces.some((s) => s.mode === 'tiling-2d')
          ? 'open-world-3d'
          : result.mode,
      };
    },
  };
}

export class ScriptableWindowManager {
  private readonly actors: Map<bigint, ActorRecord> = new Map();
  private readonly surfaces: Map<string, SurfaceRecord> = new Map();
  private readonly bindings: StreamBinding[] = [];
  private readonly dialogs: DialogMessage[] = [];
  private hooks: WmScriptHooks;
  private viewport: ViewportSize;
  private focusedSurfaceId: string | null = null;
  private lastLayout: WmLayoutResult;
  private nextActorId: bigint = 2n;
  private nextArenaId: bigint = 1000n;
  private nextSurfaceSeq = 1;
  private nextBindingSeq = 1;
  private nextDialogSeq = 1;
  private scriptGeneration = 1;

  constructor(options: ScriptableWindowManagerOptions = {}) {
    this.hooks = options.hooks ? { ...options.hooks } : createDefaultCodingWmScript();
    this.viewport = options.viewport ?? { width: 1920, height: 1080 };

    // Always register the default human actor (actorId = 1n) so legacy web pages
    // and solo games with zero agents still operate with a first-class actor.
    this.actors.set(DEFAULT_HUMAN_ACTOR_ID, {
      actorId: DEFAULT_HUMAN_ACTOR_ID,
      kind: 'human',
      name: options.humanName ?? 'human',
      createdAt: Date.now(),
    });

    this.lastLayout = this.computeLayout();
  }

  private createHookContext(): WmHookContext {
    return {
      getActor: (actorId) => this.actors.get(actorId),
      listActors: () => Array.from(this.actors.values()),
      getSurface: (surfaceId) => this.surfaces.get(surfaceId),
      listSurfaces: () => Array.from(this.surfaces.values()),
      getActorBindings: (actorId, streamKind) =>
        this.getActorBindings(actorId, streamKind),
      rebindSurface: (surfaceId, targetMode, target) =>
        this.rebindSurface(surfaceId, targetMode, target),
    };
  }

  getScriptGeneration(): number {
    return this.scriptGeneration;
  }

  getHooks(): Readonly<WmScriptHooks> {
    return this.hooks;
  }

  getViewport(): ViewportSize {
    return { ...this.viewport };
  }

  setViewport(viewport: ViewportSize): WmLayoutResult {
    this.viewport = { ...viewport };
    this.lastLayout = this.computeLayout();
    return this.lastLayout;
  }

  registerActor(input: RegisterActorInput): ActorRecord {
    const actorId = input.actorId ?? this.allocateActorId();
    if (this.actors.has(actorId)) {
      throw new Error(`Actor ${actorId.toString()} is already registered`);
    }
    if (actorId >= this.nextActorId) {
      this.nextActorId = actorId + 1n;
    }

    const record: ActorRecord = {
      actorId,
      kind: input.kind,
      name: input.name,
      avatarRef: input.avatarRef,
      worldPose: input.worldPose ? { ...input.worldPose } : undefined,
      metadata: input.metadata ? { ...input.metadata } : undefined,
      createdAt: Date.now(),
    };
    this.actors.set(actorId, record);

    if (input.avatarRef) {
      this.bindStream(actorId, 'avatar', input.avatarRef);
    }

    return record;
  }

  getActor(actorId: bigint): ActorRecord | undefined {
    return this.actors.get(actorId);
  }

  listActors(): ActorRecord[] {
    return Array.from(this.actors.values());
  }

  bindStream(actorId: bigint, streamKind: StreamKind, refId: string): StreamBinding {
    if (!this.actors.has(actorId)) {
      throw new Error(`Cannot bind stream: unknown actorId ${actorId.toString()}`);
    }

    const existing = this.bindings.find(
      (b) =>
        b.actorId === actorId &&
        b.streamKind === streamKind &&
        b.refId === refId,
    );
    if (existing) {
      return existing;
    }

    const binding: StreamBinding = {
      bindingId: `bind-${this.nextBindingSeq++}`,
      actorId,
      streamKind,
      refId,
      createdAt: Date.now(),
    };
    this.bindings.push(binding);
    return binding;
  }

  getActorBindings(actorId: bigint, streamKind?: StreamKind): StreamBinding[] {
    return this.bindings.filter(
      (b) =>
        b.actorId === actorId &&
        (streamKind === undefined || b.streamKind === streamKind),
    );
  }

  listBindings(): StreamBinding[] {
    return [...this.bindings];
  }

  openSurface(input: OpenSurfaceInput): SurfaceRecord {
    const actorId = input.actorId ?? DEFAULT_HUMAN_ACTOR_ID;
    if (!this.actors.has(actorId)) {
      throw new Error(`Cannot open surface: unknown actorId ${actorId.toString()}`);
    }

    const surfaceId = input.surfaceId ?? `surface-${this.nextSurfaceSeq++}`;
    if (this.surfaces.has(surfaceId)) {
      throw new Error(`Surface ${surfaceId} already exists`);
    }

    const arenaId = input.arenaId ?? this.nextArenaId++;
    if (arenaId >= this.nextArenaId) {
      this.nextArenaId = arenaId + 1n;
    }

    const surface: SurfaceRecord = {
      surfaceId,
      arenaId,
      actorId,
      kind: input.kind,
      title: input.title,
      mode: input.mode ?? 'tiling-2d',
      leases: (input.leases ?? LEASE_NONE) & LEASE_MASK,
      state: 'ACTIVE',
      splitRatio: input.splitRatio ?? 0.5,
      worldPose: input.worldPose ? { ...input.worldPose } : undefined,
      tabGroup: input.tabGroup,
      idleMs: 0,
      focused: false,
      checkpointCount: 0,
      metadata: input.metadata ? { ...input.metadata } : undefined,
    };

    this.surfaces.set(surfaceId, surface);
    this.bindStream(actorId, 'surface', surfaceId);

    const patch = this.hooks.onSurfaceOpen?.(surface, this.createHookContext());
    if (patch) {
      if (patch.mode !== undefined) surface.mode = patch.mode;
      if (patch.splitRatio !== undefined) surface.splitRatio = patch.splitRatio;
      if (patch.tabGroup !== undefined) surface.tabGroup = patch.tabGroup;
      if (patch.worldPose !== undefined) surface.worldPose = { ...patch.worldPose };
      if (patch.leases !== undefined) surface.leases = patch.leases & LEASE_MASK;
    }

    this.lastLayout = this.computeLayout();
    return surface;
  }

  getSurface(surfaceId: string): SurfaceRecord | undefined {
    return this.surfaces.get(surfaceId);
  }

  listSurfaces(): SurfaceRecord[] {
    return Array.from(this.surfaces.values());
  }

  getFocusedSurface(): SurfaceRecord | null {
    if (!this.focusedSurfaceId) return null;
    return this.surfaces.get(this.focusedSurfaceId) ?? null;
  }

  getLayout(): WmLayoutResult {
    return this.lastLayout;
  }

  computeLayout(): WmLayoutResult {
    const surfaces = Array.from(this.surfaces.values());
    if (this.hooks.layout) {
      this.lastLayout = this.hooks.layout(surfaces, this.viewport);
    } else {
      this.lastLayout = defaultHorizontalTilingLayout(surfaces, this.viewport);
    }
    return this.lastLayout;
  }

  rebindSurface(
    surfaceId: string,
    targetMode: CompositorMode,
    target?: RebindTarget,
  ): SurfaceRecord {
    const surface = this.requireSurface(surfaceId);
    const previousMode = surface.mode;
    const preservedArenaId = surface.arenaId;

    if (target?.actorId !== undefined) {
      if (!this.actors.has(target.actorId)) {
        throw new Error(
          `Cannot rebind surface: unknown actorId ${target.actorId.toString()}`,
        );
      }
      surface.actorId = target.actorId;
      this.bindStream(target.actorId, 'surface', surfaceId);
    }

    surface.mode = targetMode;
    if (target?.splitRatio !== undefined) {
      surface.splitRatio = target.splitRatio;
    }
    if (target?.tabGroup !== undefined) {
      surface.tabGroup = target.tabGroup;
    }
    if (target?.worldPose !== undefined) {
      surface.worldPose = { ...target.worldPose };
    }

    // Zero-copy invariant: arenaId MUST remain identical across 3D <-> 2D rebinds.
    surface.arenaId = preservedArenaId;

    this.hooks.onSurfaceRebind?.(surface, previousMode, targetMode, target);
    this.lastLayout = this.computeLayout();
    return surface;
  }

  setLeases(surfaceId: string, leases: number): SurfaceRecord {
    const surface = this.requireSurface(surfaceId);
    surface.leases = leases & LEASE_MASK;
    return surface;
  }

  tickIdle(
    surfaceId: string,
    idleMs: number,
    options: TickIdleOptions = {},
  ): SurfaceState {
    const surface = this.requireSurface(surfaceId);
    surface.idleMs += Math.max(0, idleMs);

    const customState = this.hooks.onIdle?.(
      surface,
      surface.leases,
      surface.idleMs,
    );

    let nextState: SurfaceState;
    if (customState !== undefined) {
      nextState = customState;
    } else if (surface.leases !== LEASE_NONE) {
      // Background leases held: freeze pixels only, keep background worker alive.
      nextState = 'DISPLAY_SLEEP';
    } else {
      const checkpointThresholdMs =
        options.checkpointThresholdMs ?? DEFAULT_CHECKPOINT_THRESHOLD_MS;
      if (
        surface.state === 'FROZEN' ||
        surface.state === 'CHECKPOINTED' ||
        surface.idleMs >= checkpointThresholdMs
      ) {
        nextState = 'CHECKPOINTED';
      } else {
        nextState = 'FROZEN';
      }
    }

    if (nextState === 'CHECKPOINTED' && surface.state !== 'CHECKPOINTED') {
      surface.checkpointCount += 1;
    }
    surface.state = nextState;
    return surface.state;
  }

  focusSurface(surfaceId: string): SurfaceRecord {
    const surface = this.requireSurface(surfaceId);
    const previous = this.getFocusedSurface();
    if (previous && previous.surfaceId !== surface.surfaceId) {
      previous.focused = false;
    }

    surface.focused = true;
    surface.idleMs = 0;
    surface.state = 'ACTIVE';
    this.focusedSurfaceId = surface.surfaceId;

    this.hooks.onFocusChange?.(surface, previous, this.createHookContext());
    this.lastLayout = this.computeLayout();
    return surface;
  }

  dispatchKey(event: WmKeyEvent): boolean {
    const handled = this.hooks.onKey?.(event, this.createHookContext());
    return Boolean(handled);
  }

  sendDialog(
    fromActorId: bigint,
    toActorId: bigint,
    text: string,
    surfaceId?: string,
  ): DialogMessage {
    if (!this.actors.has(fromActorId)) {
      throw new Error(`Unknown sender actorId ${fromActorId.toString()}`);
    }
    if (!this.actors.has(toActorId)) {
      throw new Error(`Unknown recipient actorId ${toActorId.toString()}`);
    }

    const message: DialogMessage = {
      messageId: `dlg-${this.nextDialogSeq++}`,
      fromActorId,
      toActorId,
      text,
      timestamp: Date.now(),
      surfaceId,
    };
    this.dialogs.push(message);
    this.hooks.onActorSpeak?.(message, this.createHookContext());
    return message;
  }

  getDialogHistory(actorId?: bigint): readonly DialogMessage[] {
    if (actorId === undefined) {
      return [...this.dialogs];
    }
    return this.dialogs.filter(
      (m) => m.fromActorId === actorId || m.toActorId === actorId,
    );
  }

  reloadScript(newHooks: WmScriptHooks): WmLayoutResult {
    this.hooks = { ...newHooks };
    this.scriptGeneration += 1;
    this.lastLayout = this.computeLayout();
    return this.lastLayout;
  }

  private allocateActorId(): bigint {
    while (this.actors.has(this.nextActorId)) {
      this.nextActorId += 1n;
    }
    const allocated = this.nextActorId;
    this.nextActorId += 1n;
    return allocated;
  }

  private requireSurface(surfaceId: string): SurfaceRecord {
    const surface = this.surfaces.get(surfaceId);
    if (!surface) {
      throw new Error(`Unknown surfaceId: ${surfaceId}`);
    }
    return surface;
  }
}
