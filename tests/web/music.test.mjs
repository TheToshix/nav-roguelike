// SPDX-License-Identifier: MIT
//
// The eight guardian themes, checked by measurement rather than by ear.
//
// `frontend/web/music.js` builds its music out of oscillators and filtered
// noise; there is no audio file anywhere in the repository to compare against,
// and live playback cannot be captured in a headless browser. What can be done
// is render a theme offline, through the same code down the same paths, and
// then look at the samples. `NavMusic._render` exists for exactly that and was
// written for a test that, until this file, did not exist — both READMEs
// described its results as fact.
//
// Three questions are worth asking of a synth, and they are the three the
// READMEs claim:
//
//   1. Is there a sound at all? A synth that silently fails still "plays".
//   2. Does the third phase press harder than the first? That is the whole
//      point of phases — the fight is supposed to sound worse as it goes.
//   3. Does a phase change break the waveform outright? This one is a
//      guardrail rather than a measurement — see the note on the test itself
//      for exactly what it can and cannot see.
//
// Nothing here asserts an exact sample: the noise layers draw on Math.random,
// so no two renders are identical. Every threshold below is a ratio against the
// render's own amplitude, and every one of them is recorded with the margin it
// had when it was chosen, so a later reader can tell a tightened threshold from
// a real regression.

import test from 'node:test';
import assert from 'node:assert/strict';
import { chromium } from 'playwright';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const musicSource = readFileSync(join(here, '..', '..', 'frontend', 'web', 'music.js'), 'utf8');

/// The eight guardians, in the order they are met. Kept as a literal rather
/// than read out of THEMES: a theme silently disappearing from the table is
/// precisely the kind of regression this list is here to catch.
const THEMES = [
  'mara', 'viy',          // Погост
  'vodyanoy', 'babayaga', // Чернотопь
  'morozko', 'koschei',   // Кощеево царство
  'polozh', 'gorynych',   // Пекло
];

// Nine seconds at three phases is three seconds each — comfortably longer than
// the 1.5 s crossfade a phase change runs, so a boundary is fully contained in
// the render. 22.05 kHz is half the usual rate and halves the work; every
// measurement below is well under the Nyquist limit either way.
const SECONDS = 9;
const RATE = 22050;

// --- Thresholds, with the headroom each had when it was set -----------------
//
// Measured across the eight themes and repeated renders (September 2026):
//
//   phase RMS          0.039 … 0.111        quietest theme is Полоз
//   phase 3 / phase 1  1.82× … 2.11×        smallest is Вий
//   click / peak       1.17% … 6.94%        largest is Кощей, stable to 0.01%
//
const MIN_RMS = 0.01;              // ~4x below the quietest phase measured
const MIN_PHASE_GROWTH = 1.3;      // ~1.4x below the smallest growth measured
const MAX_STEP_FRACTION = 0.20;    // ~2.9x above the largest step measured

/// Renders every theme once and returns the measurements, so the browser is
/// launched a single time for the whole file.
async function measureAllThemes() {
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage();
    await page.setContent('<!doctype html><meta charset="utf-8"><title>music</title>');
    await page.addScriptTag({ content: musicSource });

    const real = await page.evaluate(
      () => typeof window.NavMusic === 'object' &&
            typeof window.NavMusic._render === 'function' &&
            window.NavMusic.available());
    assert.ok(real, 'music.js fell back to its silent stub: this browser has no Web Audio, ' +
                    'so nothing below would be measuring the synth');

    return await page.evaluate(async ({ themes, seconds, rate }) => {
      const out = {};
      for (const id of themes) {
        const buffer = await window.NavMusic._render(id, [1, 2, 3], seconds, rate);
        const d = buffer.getChannelData(0);
        const seg = Math.floor(d.length / 3);

        // Loudness per phase, as root mean square. Peak would be dominated by
        // whichever noise burst happened to land loudest; RMS is what "how
        // loud is this passage" actually means, and it is stable across
        // renders to five decimal places.
        const rms = [];
        for (let phase = 0; phase < 3; phase++) {
          let sum = 0;
          for (let i = phase * seg; i < (phase + 1) * seg; i++) sum += d[i] * d[i];
          rms.push(Math.sqrt(sum / seg));
        }

        let peak = 0;
        for (let i = 0; i < d.length; i++) {
          const a = Math.abs(d[i]);
          if (a > peak) peak = a;
        }

        // A click is a step between neighbouring samples. Looked for in a 5 ms
        // window either side of each phase boundary — wide enough to catch a
        // step that is a few samples long, narrow enough that the answer is
        // about the transition and not about the music around it.
        const window_ = Math.round(rate * 0.005);
        let click = 0;
        for (const boundary of [seg, 2 * seg]) {
          const from = Math.max(1, boundary - window_);
          const to = Math.min(d.length, boundary + window_);
          for (let i = from; i < to; i++) click = Math.max(click, Math.abs(d[i] - d[i - 1]));
        }

        out[id] = { rms, peak, click, samples: d.length };
      }
      return out;
    }, { themes: THEMES, seconds: SECONDS, rate: RATE });
  } finally {
    await browser.close();
  }
}

const measured = await measureAllThemes();

test('every guardian has a theme that makes a sound', () => {
  for (const id of THEMES) {
    const m = measured[id];
    assert.ok(m, `${id} rendered nothing at all`);
    assert.equal(m.samples, Math.ceil(SECONDS * RATE), `${id}: wrong render length`);
    m.rms.forEach((value, i) => {
      assert.ok(value > MIN_RMS,
        `${id} phase ${i + 1}: RMS ${value.toFixed(5)} is below ${MIN_RMS} — silence, ` +
        `or near enough that a player would not hear it`);
    });
  }
});

test('the third phase presses harder than the first', () => {
  for (const id of THEMES) {
    const [first, , third] = measured[id].rms;
    const growth = third / first;
    assert.ok(growth >= MIN_PHASE_GROWTH,
      `${id}: phase 3 is only ${growth.toFixed(2)}x phase 1 (want >= ${MIN_PHASE_GROWTH}). ` +
      `The fight is supposed to sound worse as it goes.`);
  }
  // Deliberately not asserted: that loudness rises at *every* step. Вий's
  // second phase is louder than his third, and that is the composition, not a
  // defect — his last phase trades weight for a faster, thinner pulse.
});

/// A guardrail, and worth being exact about its reach.
///
/// A phase change here is a gain ramp applied to oscillators that keep running
/// underneath it. The step it can introduce is bounded by the gain difference
/// times wherever the waveform happens to be, and the gains are small — so this
/// synth cannot produce a large discontinuity at a phase change no matter how
/// the crossfade is set. Measured: with the crossfade at its normal 1.5 s the
/// largest step across all eight themes is 6.9% of peak; with the crossfade cut
/// to half a millisecond it rises only to 10.0%. This test does not catch that,
/// and no threshold that did would be far enough from 6.9% to be stable.
///
/// So it is not a check that the crossfade is working. It is a check that the
/// waveform is still continuous at all — it would catch a transition rewritten
/// to swap buffers or restart an oscillator mid-note, which is a real way to
/// break this and would be plainly audible. Read it as a floor, not a proof.
///
/// (The obvious alternative — watching the level travel across the boundary to
/// see the crossfade happen — was tried and does not work: the pulses and motif
/// swing the level far more than the crossfade does, and the counts come out
/// somewhere between 0 and 33 windows on unmodified code.)
test('the waveform stays continuous across a phase change', () => {
  for (const id of THEMES) {
    const { click, peak } = measured[id];
    const fraction = click / peak;
    assert.ok(fraction < MAX_STEP_FRACTION,
      `${id}: the waveform steps by ${(100 * fraction).toFixed(2)}% of its own peak at a phase ` +
      `change (want < ${100 * MAX_STEP_FRACTION}%). A break that size is not a crossfade at all.`);
  }
});

test('nothing clips', () => {
  for (const id of THEMES) {
    // Above 1.0 the samples are past what the format can carry and the sound
    // distorts. The synth caps itself at MASTER = 0.22 per voice, so this is
    // headroom checking, not a close call — it catches a future layer added
    // without regard for the ones already there.
    assert.ok(measured[id].peak < 1.0,
      `${id}: peaks at ${measured[id].peak.toFixed(3)} — the mix is clipping`);
  }
});
