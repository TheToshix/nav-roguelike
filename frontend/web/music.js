// Навь — процедурная музыка боссов: ни одного аудиофайла, всё собирается из
// осцилляторов и шума прямо в браузере.
//
// Ноты планируются на 200 мс вперёд по часам AudioContext, а не в момент, когда
// их слышно. Игровой цикл перерисовывает канвас и иногда подвисает на кадр —
// ноты, рождённые по setInterval, заикались бы в самые напряжённые моменты боя.
(function () {
  'use strict';

  var AC = window.AudioContext || window.webkitAudioContext;

  // Заглушка обязана быть вызываемой: интерфейс дёргает play/stop из игрового
  // цикла и не должен знать, есть ли в этом браузере Web Audio вообще.
  if (!AC) {
    window.NavMusic = {
      available: function () { return false; },
      setEnabled: function () {}, enabled: function () { return false; },
      play: function () {}, stop: function () {}, resume: function () {}
    };
    return;
  }

  var MASTER = 0.22;   // потолок громкости: музыка — фон, а не событие
  var FADE = 1.5;      // сколько длится смена фазы и уход темы
  var LOOK = 0.2;      // горизонт планирования
  var TICK = 100;      // как часто планировщик просыпается

  // -------------------------------------------------------------- лады --
  // Только минорные и модальные: фригийский и локрийский дают нагнетание
  // одной второй ступенью, без диссонансной каши, которая получается, если
  // просто набросать полутонов.
  var AEOL = [0, 2, 3, 5, 7, 8, 10];
  var PHRY = [0, 1, 3, 5, 7, 8, 10];
  var LOCR = [0, 1, 3, 5, 6, 8, 10];
  var HARM = [0, 2, 3, 5, 7, 8, 11];
  var HUNG = [0, 2, 3, 6, 7, 8, 11];   // цыганский минор — ковыляющий, «деревянный»
  var OCTA = [0, 1, 3, 4, 6, 7, 9, 10]; // полутон-тон: беспокойство без тональности

  var N = null; // пауза в мотиве — так строчка сетки читается как рисунок

  // ------------------------------------------------------------- темы --
  // Слой активен в фазах из `phases`. Любой числовой параметр можно задать
  // массивом из трёх значений — тогда он меняется по фазам сам.
  var THEMES = {
    // Зыбкое, расстроенное. Два дрона разведены на десятки центов: биения
    // между ними и дают ощущение, что мир слегка не в фокусе.
    mara: {
      root: 98, bpm: [52, 58, 66], scales: [AEOL, PHRY, LOCR],
      layers: [
        { kind: 'drone', wave: 'triangle', semi: 0, detune: [10, 17, 26], cut: [420, 520, 660], gain: [0.34, 0.42, 0.52] },
        { kind: 'drone', wave: 'sine', semi: 12, detune: [24, 34, 46], cut: [900, 1150, 1500], gain: [0.12, 0.18, 0.26] },
        { kind: 'pulse', form: 'noise', phases: [2, 3], freq: 380, q: 0.8, dec: 0.5, gain: [0.10, 0.14, 0.19],
          pattern: ['x.......o.......', 'x...o...x...o...', 'x..o..x..o..x..o'] },
        // Колыбельная наоборот: качание есть, но фраза лезет вверх, а её
        // трёхдольный рисунок не сходится с четырёхдольной сеткой.
        { kind: 'motif', wave: 'sine', oct: 2, dec: 0.9, gain: [0.12, 0.16, 0.21], steps: [
          [0, N, N, 4, N, N, 2, N, N, 3, N, N, 1, N, N, N],
          [0, N, 4, N, N, 2, N, 6, N, 3, N, 1, N, N, 5, N],
          [0, N, 4, 2, N, 6, N, 3, 1, N, 5, N, 4, N, 2, N]] }
      ]
    },
    // Низкое и вязкое: фильтр почти закрыт, поэтому пила слышна как ил, а не
    // как пила. Пузыри — синус с быстрым подъёмом частоты.
    vodyanoy: {
      root: 49, bpm: [48, 56, 64], scales: [PHRY, PHRY, LOCR],
      layers: [
        { kind: 'drone', wave: 'sawtooth', semi: 0, detune: 6, cut: [110, 150, 220], gain: [0.42, 0.50, 0.60] },
        { kind: 'drone', wave: 'sawtooth', semi: 7, phases: [2, 3], detune: 9, cut: [140, 210, 300], gain: [0.22, 0.30, 0.40] },
        { kind: 'pulse', form: 'blip', wave: 'sine', freq: [170, 190, 210], sweep: 3.4, dec: 0.13, gain: [0.18, 0.24, 0.31],
          pattern: ['..x.......o.....', '..x...o...x...o.', '..x.o.x..o.x.o.x'] },
        { kind: 'motif', wave: 'triangle', oct: 1, dec: 1.1, gain: 0.14, cut: 700, steps: [
          [0, N, N, N, N, N, N, N, 3, N, N, N, N, N, N, N],
          [0, N, N, N, 2, N, N, N, 3, N, N, N, 1, N, N, N],
          [0, N, N, 2, N, N, 3, N, 1, N, N, 4, N, N, 2, N]] }
      ]
    },
    // Стеклянное: обертон на дуодециме и очень узкий полосовой шум высоко —
    // это и читается как треск льда, хотя источник тот же белый шум.
    morozko: {
      root: 110, bpm: [60, 72, 84], scales: [AEOL, HARM, LOCR],
      layers: [
        { kind: 'drone', wave: 'sine', semi: 0, detune: 4, cut: [500, 700, 950], gain: [0.24, 0.32, 0.42] },
        { kind: 'drone', wave: 'sine', semi: 19, phases: [2, 3], detune: 8, cut: [2600, 3400, 4400], gain: [0.09, 0.14, 0.20] },
        { kind: 'pulse', form: 'noise', freq: [3000, 4000, 5200], q: 14, dec: 0.05, gain: [0.10, 0.15, 0.21],
          pattern: ['....x.......x...', '..x.x.....x.x...', '.x.xx..x.x.xx.x.'] },
        { kind: 'motif', wave: 'sine', oct: 3, dec: 1.6, gain: [0.09, 0.13, 0.18], steps: [
          [0, N, N, N, N, N, 4, N, N, N, N, N, 2, N, N, N],
          [0, N, N, 4, N, N, 2, N, 6, N, N, N, 3, N, N, N],
          [7, N, 4, N, 6, N, 3, N, 5, N, 2, N, 4, N, 1, N]] }
      ]
    },
    // Шипящее: плотная шумовая сетка вместо ударов и полутон-тоновый лад,
    // в котором мотив никуда не разрешается.
    polozh: {
      root: 82.4, bpm: [84, 100, 116], scales: [PHRY, OCTA, OCTA],
      layers: [
        { kind: 'drone', wave: 'sawtooth', semi: 0, detune: [12, 18, 26], cut: [300, 430, 620], gain: [0.30, 0.40, 0.52] },
        { kind: 'drone', wave: 'sawtooth', semi: 6, phases: [3], detune: 20, cut: 700, gain: 0.22 },
        { kind: 'pulse', form: 'noise', freq: [1800, 2400, 3200], q: 1.4, dec: [0.2, 0.16, 0.12], gain: [0.09, 0.13, 0.18],
          pattern: ['x.x.x.x.x.x.x.x.', 'xxx.xx.xxx.xx.x.', 'xxxxxx.xxxxxx.xx'] },
        { kind: 'motif', wave: 'sawtooth', oct: 1, dec: 0.22, gain: 0.12, cut: 2200, steps: [
          [0, N, 3, N, N, 2, N, N, 1, N, N, 4, N, N, 2, N],
          [0, 3, N, 2, N, 4, 1, N, 5, N, 2, 6, N, 3, N, 1],
          [0, 3, 5, 2, 7, 4, 1, 6, 3, 0, 5, 2, 7, 4, 6, 1]] }
      ]
    },
    // Тяжёлое ожидание: почти всё ниже 200 Гц, удары редкие. В третьей фазе
    // добавляется слой, у которого пила медленно ползёт вверх на октаву и
    // срывается обратно — это и есть поднимающийся взгляд.
    viy: {
      root: 41.2, bpm: [44, 50, 58], scales: [AEOL, PHRY, PHRY],
      layers: [
        { kind: 'drone', wave: 'sawtooth', semi: 0, detune: 4, cut: [90, 130, 200], gain: 0.55 },
        { kind: 'drone', wave: 'square', semi: 7, phases: [2, 3], detune: 7, cut: [130, 190, 280], gain: 0.24 },
        { kind: 'drone', wave: 'sawtooth', semi: 12, phases: [3], detune: 10, cut: 1300, gain: 0.20, rise: 26 },
        { kind: 'pulse', form: 'blip', wave: 'sine', freq: 96, sweep: 0.45, dec: 0.5, gain: [0.30, 0.34, 0.38],
          pattern: ['x...............', 'x.......x.......', 'x...x...x...x...'] },
        { kind: 'motif', wave: 'triangle', oct: 2, dec: 1.3, gain: 0.13, steps: [
          [N], [N],
          [0, N, N, N, 7, N, N, N, 3, N, N, N, 10, N, N, N]] }
      ]
    },
    // Ковыляющий ритм сделан не рисунком, а сеткой: три шага подряд длятся
    // 1.35 / 0.75 / 0.9 доли. Сумма ровно 3, поэтому темп в среднем держится,
    // а хромота слышна на каждом такте.
    babayaga: {
      root: 73.4, bpm: [72, 84, 96], scales: [AEOL, HUNG, HUNG], limp: [1.35, 0.75, 0.9],
      layers: [
        { kind: 'drone', wave: 'triangle', semi: 0, detune: 8, cut: [280, 380, 520], gain: [0.30, 0.40, 0.52] },
        { kind: 'pulse', form: 'noise', freq: [900, 980, 1080], q: 9, dec: 0.08, gain: [0.16, 0.22, 0.29],
          pattern: ['x..x..x...x..x..', 'x.xx..x..x.x..x.', 'x.xxx.x.xx.x.xx.'] },
        { kind: 'pulse', form: 'noise', phases: [2, 3], freq: 420, q: 7, dec: 0.14, gain: [0.14, 0.20, 0.27],
          pattern: ['................', '....x.......x...', '..x...x...x...x.'] },
        { kind: 'motif', wave: 'square', oct: 1, dec: 0.3, gain: [0.10, 0.14, 0.19], cut: 1400, steps: [
          [0, N, 2, N, N, 3, N, N, 1, N, N, N, 4, N, N, N],
          [0, 2, N, 3, N, 1, N, 4, 2, N, 5, N, 3, N, N, N],
          [0, 2, 3, N, 1, 4, N, 2, 5, 3, N, 6, 4, N, 2, N]] }
      ]
    },
    // Костяное и торжественное: дрон сложен из тоники, квинты и октавы —
    // органный, не шумовой; поверх — сухие щелчки почти без тела.
    koschei: {
      root: 65.4, bpm: [60, 66, 74], scales: [HARM, HARM, HARM],
      layers: [
        { kind: 'drone', wave: 'sawtooth', semi: 0, detune: 5, cut: [200, 300, 430], gain: [0.34, 0.44, 0.56] },
        { kind: 'drone', wave: 'sawtooth', semi: 7, detune: 8, cut: [260, 380, 540], gain: [0.16, 0.23, 0.32] },
        { kind: 'drone', wave: 'sawtooth', semi: 12, phases: [2, 3], detune: 11, cut: [500, 760, 1050], gain: [0.12, 0.17, 0.24] },
        { kind: 'pulse', form: 'noise', freq: 4200, q: 16, dec: 0.03, gain: [0.08, 0.12, 0.16],
          pattern: ['x.x.x.x.x.x.x.x.', 'x.xxx.x.x.xxx.x.', 'xxx.xxx.xxx.xxx.'] },
        { kind: 'pulse', form: 'blip', wave: 'triangle', freq: 131, sweep: 0.5, dec: 0.9, gain: [0.20, 0.26, 0.33],
          pattern: ['x...............', 'x.......x.......', 'x...x...x...x...'] },
        { kind: 'motif', wave: 'square', oct: 1, dec: 0.45, gain: [0.12, 0.16, 0.21], cut: 1600, steps: [
          [0, N, N, N, 3, N, N, N, 4, N, N, N, 2, N, N, N],
          [0, N, N, 2, 3, N, N, 4, 5, N, N, 4, 2, N, N, N],
          [0, N, 2, 3, 4, N, 5, 4, 6, N, 5, 4, 2, N, 1, N]] }
      ]
    },
    // Три головы — три дрона с разными интервалами. С каждой фазой один
    // замолкает, но оставшиеся получают больше расстройки, открытый фильтр и
    // громкость: голов меньше, а страшнее.
    gorynych: {
      root: 87.3, bpm: [80, 96, 116], scales: [PHRY, PHRY, LOCR],
      layers: [
        { kind: 'drone', wave: 'sawtooth', semi: 0, detune: [8, 17, 30], cut: [300, 520, 900],
          gain: [0.28, 0.46, 0.74] },
        { kind: 'drone', wave: 'sawtooth', semi: 7, phases: [1, 2], detune: [7, 14, 14], cut: [340, 560, 560], gain: [0.26, 0.36, 0]  },
        { kind: 'drone', wave: 'square', semi: 15, phases: [1], detune: 9, cut: 620, gain: 0.20 },
        { kind: 'pulse', form: 'noise', freq: [900, 1400, 2200], q: 2, dec: [0.12, 0.1, 0.08],
          gain: [0.13, 0.20, 0.30],
          pattern: ['x..x..x...x.....', 'x..x.xx..x.x.x..', 'x.xx.xx.xx.xxxx.'] },
        { kind: 'motif', wave: 'sawtooth', oct: 1, dec: 0.28, gain: [0.10, 0.16, 0.24], cut: 2400, steps: [
          [0, N, N, 3, N, N, 2, N, 4, N, N, 1, N, N, N, N],
          [0, N, 3, 2, N, 4, N, 1, 5, N, 2, 4, N, 3, N, N],
          [0, 4, 3, 6, 2, 5, 1, 4, 7, 3, 6, 2, 5, 1, 4, 0]] }
      ]
    }
  };

  // ------------------------------------------------------ мелкие утилиты --
  function pv(v, ph) { return (v instanceof Array) ? v[ph - 1] : v; }
  function on(L, ph) { return !L.phases || L.phases.indexOf(ph) >= 0; }

  /// Ступень лада в герцах: номера за пределами лада сами уходят в соседние
  /// октавы, поэтому мотив пишется одной строкой чисел.
  function degHz(root, scale, deg, oct) {
    var n = scale.length, i = ((deg % n) + n) % n;
    return root * Math.pow(2, scale[i] / 12 + Math.floor(deg / n) + (oct || 0));
  }

  /// Снять запланированное, не дёрнув значение. Без этого любая смена фазы
  /// поверх незакончившегося ramp даёт щелчок.
  function hold(p, t) {
    if (p.cancelAndHoldAtTime) return p.cancelAndHoldAtTime(t);
    var v = p.value; p.cancelScheduledValues(t); p.setValueAtTime(v, t);
  }

  function ramp(p, to, t, dur) { hold(p, t); p.linearRampToValueAtTime(to, t + dur); }

  /// Огибающая всего, что звучит однократно. Источник глушится через 2*dec —
  /// к этому моменту от пика остаётся меньше тысячной, и stop() не слышен.
  function env(ac, t, atk, dec, peak) {
    var g = ac.createGain();
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(peak, t + atk);
    g.gain.setTargetAtTime(0, t + atk, dec / 3.5);
    return g;
  }

  function noise(ac) {
    if (!ac._navNoise) {
      var n = Math.floor(ac.sampleRate * 2), b = ac.createBuffer(1, n, ac.sampleRate);
      var d = b.getChannelData(0);
      for (var i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
      ac._navNoise = b;
    }
    return ac._navNoise;
  }

  // ------------------------------------------------------------- слои --
  function makeDrone(ac, v, L, ph, t) {
    var f = ac.createBiquadFilter();
    f.type = 'lowpass'; f.frequency.value = pv(L.cut, ph); f.Q.value = 0.7;
    var g = ac.createGain();
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(pv(L.gain, ph), t + FADE);
    f.connect(g); g.connect(v.bus);
    var base = v.root * Math.pow(2, (L.semi || 0) / 12);
    var det = pv(L.detune, ph) || 0, oscs = [], i, o;
    for (i = 0; i < 2; i++) {
      o = ac.createOscillator();
      o.type = L.wave; o.frequency.value = base;
      o.detune.value = i ? det : -det;
      o.connect(f); o.start(t);
      oscs.push(o);
    }
    var extra = [];
    if (L.rise) {
      // Пила как LFO: ползёт от -1 к +1 и мгновенно сбрасывается. Скачок
      // частоты фазу осциллятора не рвёт — «взгляд» поднимается без щелчка.
      var lfo = ac.createOscillator(), amt = ac.createGain();
      lfo.type = 'sawtooth'; lfo.frequency.value = 1 / L.rise; amt.gain.value = 600;
      lfo.connect(amt); amt.connect(oscs[0].detune); amt.connect(oscs[1].detune);
      lfo.start(t); extra.push(lfo);
    }
    return { oscs: oscs.concat(extra), g: g, f: f, det: det };
  }

  function tuneDrone(d, L, ph, t) {
    ramp(d.g.gain, pv(L.gain, ph), t, FADE);
    ramp(d.f.frequency, pv(L.cut, ph), t, FADE);
    var det = pv(L.detune, ph) || 0;
    if (det !== d.det) {
      ramp(d.oscs[0].detune, -det, t, FADE);
      ramp(d.oscs[1].detune, det, t, FADE);
      d.det = det;
    }
  }

  function killDrone(d, t) {
    ramp(d.g.gain, 0, t, FADE);
    for (var i = 0; i < d.oscs.length; i++) d.oscs[i].stop(t + FADE + 0.05);
  }

  function firePulse(v, L, ph, t, k) {
    var ac = v.ac, dec = pv(L.dec, ph), g = env(ac, t, 0.004, dec, pv(L.gain, ph) * k), src;
    if (L.form === 'noise') {
      src = ac.createBufferSource();
      src.buffer = noise(ac); src.loop = true;
      var bp = ac.createBiquadFilter();
      bp.type = 'bandpass'; bp.frequency.value = pv(L.freq, ph); bp.Q.value = L.q || 4;
      src.connect(bp); bp.connect(g);
      src.start(t, Math.random() * 1.5);
    } else {
      src = ac.createOscillator();
      src.type = L.wave || 'sine';
      var f0 = pv(L.freq, ph);
      src.frequency.setValueAtTime(f0, t);
      src.frequency.exponentialRampToValueAtTime(f0 * L.sweep, t + dec * 0.8);
      src.connect(g); src.start(t);
    }
    g.connect(v.bus);
    src.stop(t + 0.004 + dec * 2);
  }

  function fireNote(v, L, ph, t, deg) {
    var ac = v.ac, dec = pv(L.dec, ph);
    var g = env(ac, t, 0.02, dec, pv(L.gain, ph)), o = ac.createOscillator();
    o.type = L.wave;
    o.frequency.value = degHz(v.root, v.scale, deg, L.oct);
    if (L.cut) {
      var f = ac.createBiquadFilter();
      f.type = 'lowpass'; f.frequency.value = pv(L.cut, ph); f.Q.value = 0.9;
      o.connect(f); f.connect(g);
    } else o.connect(g);
    g.connect(v.bus);
    o.start(t); o.stop(t + 0.02 + dec * 2);
  }

  // ------------------------------------------------------------ голос --
  function newVoice(ac, dest, id, ph, t) {
    var th = THEMES[id];
    var bus = ac.createGain(); bus.gain.value = 1; bus.connect(dest);
    var v = { ac: ac, id: id, theme: th, bus: bus, root: th.root, phase: ph,
              scale: th.scales[ph - 1], drones: [], step: 0, next: t };
    for (var i = 0; i < th.layers.length; i++) {
      var L = th.layers[i];
      v.drones[i] = (L.kind === 'drone' && on(L, ph)) ? makeDrone(ac, v, L, ph, t) : null;
    }
    return v;
  }

  function applyPhase(v, ph, t) {
    v.phase = ph;
    v.scale = v.theme.scales[ph - 1];
    for (var i = 0; i < v.theme.layers.length; i++) {
      var L = v.theme.layers[i];
      if (L.kind !== 'drone') continue;
      var want = on(L, ph), have = v.drones[i];
      if (want && !have) v.drones[i] = makeDrone(v.ac, v, L, ph, t);
      else if (!want && have) { killDrone(have, t); v.drones[i] = null; }
      else if (want) tuneDrone(have, L, ph, t);
    }
  }

  function killVoice(v, t) {
    ramp(v.bus.gain, 0, t, FADE);
    for (var i = 0; i < v.drones.length; i++) if (v.drones[i]) killDrone(v.drones[i], t);
  }

  /// Один шаг сетки: каждый неспящий слой смотрит на свою клетку узора.
  function stepAt(v, i, t) {
    var ph = v.phase, ls = v.theme.layers;
    for (var k = 0; k < ls.length; k++) {
      var L = ls[k];
      if (L.kind === 'drone' || !on(L, ph)) continue;
      if (L.kind === 'pulse') {
        var pat = pv(L.pattern, ph), c = pat.charAt(i % pat.length);
        if (c !== '.') firePulse(v, L, ph, t, c === 'o' ? 0.5 : 1);
      } else {
        var seq = L.steps[ph - 1], d = seq[i % seq.length];
        if (d !== null && d !== undefined) fireNote(v, L, ph, t, d);
      }
    }
  }

  /// Разложить всё, что попадает в окно до `until`. Сетка шестнадцатых, но
  /// длина шага берётся из `limp`: хромота Бабы-Яги живёт во времени, а не в
  /// рисунке узора.
  function pump(v, until) {
    var lim = v.theme.limp, guard = 0;
    while (v.next < until && guard++ < 512) {
      stepAt(v, v.step, v.next);
      var dur = 60 / pv(v.theme.bpm, v.phase) / 4;
      if (lim) dur *= lim[v.step % lim.length];
      v.next += dur;
      v.step++;
    }
  }

  // ------------------------------------------------------------- модуль --
  var ctx = null, master = null, voice = null, timer = null;
  var isOn = true, wanted = null;

  function ensure() {
    if (ctx) return true;
    try { ctx = new AC(); } catch (e) { return false; }
    master = ctx.createGain();
    master.gain.value = isOn ? MASTER : 0;
    master.connect(ctx.destination);
    return true;
  }

  function wake() {
    // Браузер не даёт звук до жеста пользователя. Пробуем разбудить контекст
    // молча: отказ здесь — нормальное состояние, а не ошибка.
    if (ctx && ctx.state === 'suspended' && ctx.resume) {
      var p = ctx.resume();
      if (p && p.catch) p.catch(function () {});
    }
  }

  function startTimer() {
    if (timer) return;
    timer = setInterval(function () {
      if (!voice) return;
      var now = ctx.currentTime;
      // Вкладку увели в фон — setInterval душат до одного раза в секунду.
      // Досыпать пропущенное нельзя: пачка нот с временем в прошлом прозвучит
      // одной очередью. Сетка просто сдвигается на «сейчас».
      if (voice.next < now) voice.next = now;
      pump(voice, now + LOOK);
    }, TICK);
  }

  function stopTimer() { if (timer) { clearInterval(timer); timer = null; } }

  function drop() {
    if (voice) { killVoice(voice, ctx.currentTime); voice = null; }
    stopTimer();
  }

  window.NavMusic = {
    available: function () { return true; },
    enabled: function () { return isOn; },
    resume: function () { if (ensure()) wake(); },

    setEnabled: function (v) {
      v = !!v;
      if (v === isOn) return;
      isOn = v;
      if (isOn) {
        if (ctx) ramp(master.gain, MASTER, ctx.currentTime, 0.4);
        if (wanted) this.play(wanted.id, wanted.phase);
      } else if (ctx) {
        ramp(master.gain, 0, ctx.currentTime, 0.4);
        drop();
      }
    },

    /// Начать тему или перевести её в другую фазу. Повторный вызов с теми же
    /// аргументами — намеренно ничего не делает: play зовут из отрисовки
    /// каждого кадра, и перезапуск темы там был бы слышен как заикание.
    play: function (id, phase) {
      phase = phase || 1;
      if (phase < 1) phase = 1;
      if (phase > 3) phase = 3;
      if (!THEMES[id]) return;
      wanted = { id: id, phase: phase };
      if (!isOn || !ensure()) return;
      wake();
      if (voice && voice.id === id) {
        if (voice.phase !== phase) applyPhase(voice, phase, ctx.currentTime);
        return;
      }
      var t = ctx.currentTime;
      if (voice) killVoice(voice, t);
      voice = newVoice(ctx, master, id, phase, t + 0.05);
      pump(voice, t + LOOK);   // первый удар — сразу, а не с приходом таймера
      startTimer();
    },

    stop: function () { wanted = null; if (ctx) drop(); }
  };

  // Тестовый вход: рендерит тему офлайн, по сегменту на фазу. Живой звук в
  // headless-браузере не проверить, а это — тот же код теми же путями.
  window.NavMusic._render = function (id, phases, secs, rate) {
    var OC = window.OfflineAudioContext || window.webkitOfflineAudioContext;
    rate = rate || 44100;
    var ac = new OC(1, Math.ceil(secs * rate), rate);
    var m = ac.createGain(); m.gain.value = MASTER; m.connect(ac.destination);
    var seg = secs / phases.length, t = 0;
    var v = newVoice(ac, m, id, phases[0], 0.05);
    for (var i = 0; i < phases.length; i++) {
      if (i) applyPhase(v, phases[i], t);
      pump(v, t + seg);
      t += seg;
    }
    return ac.startRendering();
  };
})();
