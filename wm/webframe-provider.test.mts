import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { ScriptableWindowManager } from './scriptable-wm.mts';
import { InMemoryWebFrameTransport, WebFrameSurfaceProvider } from './webframe-provider.mts';

describe('WebFrameSurfaceProvider (Modular Web Renderer inside ScriptableWindowManager)', () => {
  it('mounts web frames as WM surfaces, respects audio leases on idle, suspends silent tabs, and resumes on focus', () => {
    const wm = new ScriptableWindowManager();
    const transport = new InMemoryWebFrameTransport();
    const provider = new WebFrameSurfaceProvider(wm, transport);

    const twitch = provider.mountWebFrame({
      url: 'https://twitch.tv/panza069',
      title: 'Twitch Live',
      playingAudio: true,
    });
    const docs = provider.mountWebFrame({
      url: 'https://b8g.app',
      title: 'b8g.app',
    });

    assert.equal(provider.deliverFrame(twitch.surfaceId), 1);
    assert.equal(provider.deliverFrame(docs.surfaceId), 1);

    // Twitch holds LEASE_AUDIO -> DISPLAY_SLEEP (not suspended!)
    const twitchAfterIdle = provider.evaluateIdle(twitch.surfaceId, 60_000);
    assert.equal(twitchAfterIdle.state, 'DISPLAY_SLEEP');
    assert.equal(twitch.suspended, false);

    // Silent docs tab -> FROZEN/CHECKPOINTED and renderer suspended
    const docsAfterIdle = provider.evaluateIdle(docs.surfaceId, 60_000);
    assert.ok(docsAfterIdle.state === 'FROZEN' || docsAfterIdle.state === 'CHECKPOINTED');
    assert.equal(docs.suspended, true);
    assert.ok(transport.log.includes(`suspend:${docs.surfaceId}`));

    // Focus resumes docs tab cleanly
    const resumed = provider.focusAndResume(docs.surfaceId);
    assert.equal(resumed.state, 'ACTIVE');
    assert.equal(docs.suspended, false);
    assert.ok(transport.log.includes(`resume:${docs.surfaceId}`));
  });

  it('rebinds a live web frame zero-copy between 2D tiling and 3D open world preserving arenaId', () => {
    const wm = new ScriptableWindowManager();
    const provider = new WebFrameSurfaceProvider(wm);
    const frame = provider.mountWebFrame({ url: 'https://github.com/awesome-os' });

    const in3d = provider.rebindToMode(frame.surfaceId, 'open-world-3d');
    assert.equal(in3d.mode, 'open-world-3d');
    assert.equal(in3d.arenaId, frame.arenaId);

    const back2d = provider.rebindToMode(frame.surfaceId, 'tiling-2d');
    assert.equal(back2d.mode, 'tiling-2d');
    assert.equal(back2d.arenaId, frame.arenaId);
  });
});
