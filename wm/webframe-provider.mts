/**
 * @file sections/sectium-wm/webframe-provider.mts
 * @description Pluggable WebFrame Surface Provider for `ScriptableWindowManager`.
 * Treats a headless web renderer (Chromium/Blink/Sectium headless frame) as a pure
 * `kind: 'web'` surface feeding dirty-rect frames into `B8G_ARENA_V1`, completely
 * decoupled from legacy GN browser chrome.
 */
import { LEASE_AUDIO, LEASE_NET_PUSH, LEASE_NONE, type CompositorMode, type SurfaceRecord } from './wm-types.mts';
import { ScriptableWindowManager } from './scriptable-wm.mts';

export interface WebFrameTransport {
  navigate(surfaceId: string, url: string): void;
  resize(surfaceId: string, width: number, height: number): void;
  suspend(surfaceId: string): void;
  resume(surfaceId: string): void;
  close(surfaceId: string): void;
}

export interface WebFrameMountOptions {
  readonly url: string;
  readonly title?: string;
  readonly actorId?: bigint;
  readonly mode?: CompositorMode;
  readonly width?: number;
  readonly height?: number;
  readonly playingAudio?: boolean;
  readonly hasActivePush?: boolean;
}

export interface MountedWebFrame {
  readonly surfaceId: string;
  readonly arenaId: string;
  url: string;
  width: number;
  height: number;
  suspended: boolean;
  frameSeq: number;
}

/**
 * Default in-memory headless WebFrame transport (for tests, dry-runs, and
 * environments where no external Blink/CDP pipe is attached yet).
 */
export class InMemoryWebFrameTransport implements WebFrameTransport {
  readonly log: string[] = [];

  navigate(surfaceId: string, url: string): void {
    this.log.push(`navigate:${surfaceId}:${url}`);
  }
  resize(surfaceId: string, width: number, height: number): void {
    this.log.push(`resize:${surfaceId}:${width}x${height}`);
  }
  suspend(surfaceId: string): void {
    this.log.push(`suspend:${surfaceId}`);
  }
  resume(surfaceId: string): void {
    this.log.push(`resume:${surfaceId}`);
  }
  close(surfaceId: string): void {
    this.log.push(`close:${surfaceId}`);
  }
}

export class WebFrameSurfaceProvider {
  readonly wm: ScriptableWindowManager;
  readonly transport: WebFrameTransport;
  readonly #frames = new Map<string, MountedWebFrame>();

  constructor(wm: ScriptableWindowManager, transport: WebFrameTransport = new InMemoryWebFrameTransport()) {
    this.wm = wm;
    this.transport = transport;
  }

  mountWebFrame(opts: WebFrameMountOptions): MountedWebFrame {
    const width = opts.width ?? 1280;
    const height = opts.height ?? 720;
    const surface: SurfaceRecord = this.wm.openSurface({
      title: opts.title ?? opts.url,
      kind: 'web',
      actorId: opts.actorId,
      mode: opts.mode ?? 'tiling-2d',
    });

    let leases = LEASE_NONE;
    if (opts.playingAudio) leases |= LEASE_AUDIO;
    if (opts.hasActivePush) leases |= LEASE_NET_PUSH;
    if (leases !== LEASE_NONE) {
      this.wm.setLeases(surface.surfaceId, leases);
    }

    this.transport.resize(surface.surfaceId, width, height);
    this.transport.navigate(surface.surfaceId, opts.url);

    const mounted: MountedWebFrame = {
      surfaceId: surface.surfaceId,
      arenaId: surface.arenaId,
      url: opts.url,
      width,
      height,
      suspended: false,
      frameSeq: 0,
    };
    this.#frames.set(surface.surfaceId, mounted);
    return mounted;
  }

  navigate(surfaceId: string, url: string): void {
    const frame = this.#requireFrame(surfaceId);
    frame.url = url;
    if (frame.suspended) {
      this.wm.focusSurface(surfaceId);
      this.transport.resume(surfaceId);
      frame.suspended = false;
    }
    this.transport.navigate(surfaceId, url);
  }

  deliverFrame(surfaceId: string): number {
    const frame = this.#requireFrame(surfaceId);
    if (frame.suspended) return frame.frameSeq;
    frame.frameSeq++;
    return frame.frameSeq;
  }

  evaluateIdle(surfaceId: string, idleMs: number): SurfaceRecord {
    const frame = this.#requireFrame(surfaceId);
    const nextState = this.wm.tickIdle(surfaceId, idleMs);
    if (nextState === 'FROZEN' || nextState === 'CHECKPOINTED') {
      if (!frame.suspended) {
        this.transport.suspend(surfaceId);
        frame.suspended = true;
      }
    }
    return this.wm.getSurface(surfaceId)!;
  }

  focusAndResume(surfaceId: string): SurfaceRecord {
    const frame = this.#requireFrame(surfaceId);
    const updated = this.wm.focusSurface(surfaceId);
    if (frame.suspended) {
      this.transport.resume(surfaceId);
      frame.suspended = false;
    }
    return updated;
  }

  rebindToMode(surfaceId: string, targetMode: CompositorMode): SurfaceRecord {
    this.#requireFrame(surfaceId);
    return this.wm.rebindSurface(surfaceId, targetMode);
  }

  unmount(surfaceId: string): void {
    this.#requireFrame(surfaceId);
    this.transport.close(surfaceId);
    this.#frames.delete(surfaceId);
  }

  listFrames(): MountedWebFrame[] {
    return [...this.#frames.values()];
  }

  #requireFrame(surfaceId: string): MountedWebFrame {
    const frame = this.#frames.get(surfaceId);
    if (!frame) throw new Error(`WebFrameSurfaceProvider: unknown surfaceId ${surfaceId}`);
    return frame;
  }
}
