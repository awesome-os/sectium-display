import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import {
  DEFAULT_HUMAN_ACTOR_ID,
  ScriptableWindowManager,
  createDefaultCodingWmScript,
  createOpenWorldSwarmWmScript,
} from './scriptable-wm.mts';
import {
  LEASE_AUDIO,
  LEASE_COMPUTE,
  LEASE_NET_PUSH,
  LEASE_NONE,
  WakeLeaseFlag,
} from './wm-types.mts';
import type { DialogMessage, SurfaceRecord, WmScriptHooks } from './wm-types.mts';

describe('sectium-wm ScriptableWindowManager', () => {
  it('auto-registers default human actor (1n) for legacy web pages and solo games', () => {
    const wm = new ScriptableWindowManager({
      hooks: createDefaultCodingWmScript(),
      viewport: { width: 1600, height: 900 },
    });

    const human = wm.getActor(DEFAULT_HUMAN_ACTOR_ID);
    assert.ok(human);
    assert.equal(human.actorId, 1n);
    assert.equal(human.kind, 'human');
    assert.equal(wm.listActors().length, 1);

    const web = wm.openSurface({
      kind: 'web',
      title: 'Legacy Docs',
      splitRatio: 0.4,
    });
    const game = wm.openSurface({
      kind: 'game',
      title: 'Solo RPG',
      splitRatio: 0.6,
    });

    assert.equal(web.actorId, 1n);
    assert.equal(game.actorId, 1n);
    assert.equal(web.tabGroup, 'browser');
    assert.equal(game.tabGroup, 'stage');
    assert.notEqual(web.arenaId, game.arenaId);

    const layout = wm.getLayout();
    assert.equal(layout.mode, 'tiling-2d');
    assert.equal(layout.placements.length, 2);
    assert.equal(layout.placements[0].width, 640);
    assert.equal(layout.placements[1].x, 640);
    assert.equal(layout.placements[1].width, 960);
    assert.deepEqual(layout.tabGroups.browser, [web.surfaceId]);
    assert.deepEqual(layout.tabGroups.stage, [game.surfaceId]);
  });

  it('binds avatar + surface + dialog streams to a single agent actor_id and routes human<->agent dialogue', () => {
    const spoken: DialogMessage[] = [];
    const wm = new ScriptableWindowManager({
      hooks: {
        ...createDefaultCodingWmScript(),
        onActorSpeak(message) {
          spoken.push(message);
        },
      },
    });

    const coderAgent = wm.registerActor({
      kind: 'agent',
      name: 'lumi-coder',
      avatarRef: 'avatar://lumi-coder-v1',
    });
    assert.equal(coderAgent.kind, 'agent');
    assert.ok(coderAgent.actorId >= 2n);

    const term = wm.openSurface({
      actorId: coderAgent.actorId,
      kind: 'terminal',
      title: 'lumi-coder tty',
    });
    wm.bindStream(coderAgent.actorId, 'dialog', 'dialog://human-lumi-coder');
    wm.bindStream(coderAgent.actorId, 'worker-io', 'pipe://lumi-coder-io');

    const bindings = wm.getActorBindings(coderAgent.actorId);
    const kinds = new Set(bindings.map((b) => b.streamKind));
    assert.deepEqual(kinds, new Set(['avatar', 'surface', 'dialog', 'worker-io']));
    assert.equal(
      wm.getActorBindings(coderAgent.actorId, 'surface')[0].refId,
      term.surfaceId,
    );

    const msg1 = wm.sendDialog(
      DEFAULT_HUMAN_ACTOR_ID,
      coderAgent.actorId,
      'Please run the test suite.',
      term.surfaceId,
    );
    const msg2 = wm.sendDialog(
      coderAgent.actorId,
      DEFAULT_HUMAN_ACTOR_ID,
      'All 12 tests passed.',
      term.surfaceId,
    );

    assert.equal(spoken.length, 2);
    assert.equal(spoken[0].messageId, msg1.messageId);
    assert.equal(spoken[1].messageId, msg2.messageId);
    assert.equal(wm.getDialogHistory(coderAgent.actorId).length, 2);
    assert.equal(wm.getDialogHistory(DEFAULT_HUMAN_ACTOR_ID).length, 2);
  });

  it('rebinds surfaces zero-copy between open-world-3d and tiling-2d while preserving arenaId', () => {
    const rebindLog: Array<{ from: string; to: string; arenaId: bigint }> = [];
    const openWorldHooks = createOpenWorldSwarmWmScript();
    const wm = new ScriptableWindowManager({
      hooks: {
        ...openWorldHooks,
        onSurfaceRebind(surface, previousMode, targetMode) {
          rebindLog.push({
            from: previousMode,
            to: targetMode,
            arenaId: surface.arenaId,
          });
        },
      },
    });

    const agent = wm.registerActor({
      kind: 'agent',
      name: 'architect-agent',
    });

    const deskSurface = wm.openSurface({
      actorId: agent.actorId,
      kind: 'vscode',
      title: 'Architect Workspace',
    });
    const originalArenaId = deskSurface.arenaId;

    assert.equal(deskSurface.mode, 'open-world-3d');
    assert.ok(deskSurface.worldPose);
    assert.equal(deskSurface.worldPose.deskId, `desk-${agent.actorId.toString()}`);

    // Focus auto-pulls from 3D desk into 2D tiling split via createOpenWorldSwarmWmScript
    const pulled = wm.focusSurface(deskSurface.surfaceId);
    assert.equal(pulled.mode, 'tiling-2d');
    assert.equal(pulled.arenaId, originalArenaId);
    assert.equal(pulled.tabGroup, 'pulled-focus');

    // Explicit rebind back to 3D world preserves arenaId unchanged
    const returned = wm.rebindSurface(deskSurface.surfaceId, 'open-world-3d', {
      worldPose: { x: 12, y: 1, z: -5, yaw: 1.57, deskId: 'desk-architect' },
    });
    assert.equal(returned.mode, 'open-world-3d');
    assert.equal(returned.arenaId, originalArenaId);
    assert.equal(returned.worldPose?.deskId, 'desk-architect');

    assert.equal(rebindLog.length, 2);
    assert.deepEqual(rebindLog[0], {
      from: 'open-world-3d',
      to: 'tiling-2d',
      arenaId: originalArenaId,
    });
    assert.deepEqual(rebindLog[1], {
      from: 'tiling-2d',
      to: 'open-world-3d',
      arenaId: originalArenaId,
    });
  });

  it('enforces 3-bit wake leases (DISPLAY_SLEEP vs FROZEN -> CHECKPOINTED), focus thaw, and custom onIdle override', () => {
    assert.equal(WakeLeaseFlag.LEASE_NONE, 0);
    assert.equal(WakeLeaseFlag.LEASE_AUDIO, 1);
    assert.equal(WakeLeaseFlag.LEASE_NET_PUSH, 2);
    assert.equal(WakeLeaseFlag.LEASE_COMPUTE, 4);

    const wm = new ScriptableWindowManager({
      hooks: createDefaultCodingWmScript(),
    });

    const musicTab = wm.openSurface({
      kind: 'web',
      title: 'Lo-Fi Radio',
      leases: LEASE_AUDIO | LEASE_NET_PUSH,
    });
    const idleDoc = wm.openSurface({
      kind: 'web',
      title: 'Static Reference',
      leases: LEASE_NONE,
    });

    // With wake leases set -> DISPLAY_SLEEP (pixels frozen, worker alive)
    assert.equal(wm.tickIdle(musicTab.surfaceId, 15_000), 'DISPLAY_SLEEP');
    assert.equal(wm.tickIdle(musicTab.surfaceId, 120_000), 'DISPLAY_SLEEP');
    assert.equal(wm.getSurface(musicTab.surfaceId)?.checkpointCount, 0);

    // With leases === 0 -> FROZEN then CHECKPOINTED
    assert.equal(wm.tickIdle(idleDoc.surfaceId, 10_000), 'FROZEN');
    assert.equal(wm.getSurface(idleDoc.surfaceId)?.checkpointCount, 0);
    assert.equal(wm.tickIdle(idleDoc.surfaceId, 10_000), 'CHECKPOINTED');
    assert.equal(wm.getSurface(idleDoc.surfaceId)?.checkpointCount, 1);

    // Focusing a CHECKPOINTED surface thaws it back to ACTIVE and resets idleMs
    const thawed = wm.focusSurface(idleDoc.surfaceId);
    assert.equal(thawed.state, 'ACTIVE');
    assert.equal(thawed.idleMs, 0);
    assert.equal(thawed.focused, true);

    // Updating leases dynamically via setLeases
    wm.setLeases(idleDoc.surfaceId, LEASE_COMPUTE);
    assert.equal(wm.tickIdle(idleDoc.surfaceId, 20_000), 'DISPLAY_SLEEP');

    // Custom onIdle override
    const customIdleCalls: Array<{ id: string; leases: number; idleMs: number }> = [];
    const customWm = new ScriptableWindowManager({
      hooks: {
        ...createDefaultCodingWmScript(),
        onIdle(surface: SurfaceRecord, leases: number, idleMs: number) {
          customIdleCalls.push({ id: surface.surfaceId, leases, idleMs });
          if (leases === LEASE_NONE && idleMs < 5_000) {
            return 'IDLE';
          }
          return undefined;
        },
      },
    });
    const customSurface = customWm.openSurface({
      kind: 'canvas',
      title: 'Custom Canvas',
    });
    assert.equal(customWm.tickIdle(customSurface.surfaceId, 2_000), 'IDLE');
    assert.equal(customWm.tickIdle(customSurface.surfaceId, 10_000), 'FROZEN');
    assert.equal(customIdleCalls.length, 2);
  });

  it('hot-reloads WM scripts with zero loss of actors, surfaces, bindings, leases, or focus', () => {
    const wm = new ScriptableWindowManager({
      hooks: createDefaultCodingWmScript(),
      viewport: { width: 1200, height: 800 },
    });

    const worker = wm.registerActor({
      kind: 'worker',
      name: 'compiler-worker',
      avatarRef: 'avatar://compiler',
    });
    const editor = wm.openSurface({
      kind: 'vscode',
      title: 'Editor',
      leases: LEASE_COMPUTE,
    });
    const buildTerm = wm.openSurface({
      actorId: worker.actorId,
      kind: 'terminal',
      title: 'Build Output',
      leases: LEASE_NET_PUSH,
    });
    wm.bindStream(worker.actorId, 'dialog', 'dialog://build-log');
    wm.focusSurface(editor.surfaceId);

    const prevActors = wm.listActors().map((a) => a.actorId);
    const prevArenas = wm.listSurfaces().map((s) => s.arenaId);
    const prevBindingsCount = wm.listBindings().length;

    const keyEvents: string[] = [];
    const reloadedHooks: WmScriptHooks = {
      name: 'hot-reloaded-custom-wm',
      onKey(ev) {
        keyEvents.push(ev.key);
        return ev.key === 'F12';
      },
      layout(surfaces, viewport) {
        return {
          mode: 'tiling-2d',
          tabGroups: { all: surfaces.map((s) => s.surfaceId) },
          placements: surfaces.map((s, idx) => ({
            surfaceId: s.surfaceId,
            arenaId: s.arenaId,
            actorId: s.actorId,
            mode: s.mode,
            x: 0,
            y: idx * Math.floor(viewport.height / Math.max(surfaces.length, 1)),
            width: viewport.width,
            height: Math.floor(viewport.height / Math.max(surfaces.length, 1)),
            splitRatio: 1 / Math.max(surfaces.length, 1),
            tabGroup: 'all',
          })),
        };
      },
    };

    const newLayout = wm.reloadScript(reloadedHooks);

    assert.equal(wm.getScriptGeneration(), 2);
    assert.equal(wm.getHooks().name, 'hot-reloaded-custom-wm');
    assert.deepEqual(
      wm.listActors().map((a) => a.actorId),
      prevActors,
    );
    assert.deepEqual(
      wm.listSurfaces().map((s) => s.arenaId),
      prevArenas,
    );
    assert.equal(wm.listBindings().length, prevBindingsCount);
    assert.equal(wm.getSurface(editor.surfaceId)?.leases, LEASE_COMPUTE);
    assert.equal(wm.getSurface(buildTerm.surfaceId)?.leases, LEASE_NET_PUSH);
    assert.equal(wm.getFocusedSurface()?.surfaceId, editor.surfaceId);

    // New layout and new key hook are active immediately
    assert.equal(newLayout.placements.length, 2);
    assert.equal(newLayout.placements[0].width, 1200);
    assert.equal(newLayout.placements[1].y, 400);
    assert.equal(wm.dispatchKey({ key: 'F12' }), true);
    assert.equal(wm.dispatchKey({ key: 'Escape' }), false);
    assert.deepEqual(keyEvents, ['F12', 'Escape']);
  });
});
