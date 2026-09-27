// SPDX-License-Identifier: MIT
//
// The real-time page: input in, pictures out.
//
// No rule of the game lives here. The engine (engine/src/rt.cpp) decides who
// is where and who got hurt; this file turns the stick and the fire button
// into an Input sixty times a second, and turns what comes back into a moving
// picture — with every shake, flash and spark driven by an event the engine
// reported, not by a guess at what probably happened.
(function () {
  'use strict';

  var STEP = 1 / 60;                 // must match rt::kDt
  var REDUCED = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var TOUCH = window.matchMedia && window.matchMedia('(pointer: coarse)').matches;

  // Engine event kinds, in the order of rt::EventKind.
  var EV = {
    Shot: 0, EnemyShot: 1, BulletWall: 2, EnemyHit: 3, EnemyDied: 4, HeroHit: 5,
    ShieldHit: 6, ShieldBroken: 7, Pickup: 8, RoomLocked: 9, RoomCleared: 10,
    Descended: 11, HeroDied: 12, Victory: 13
  };

  var $ = function (id) { return document.getElementById(id); };
  var canvas = $('view');
  var ctx = canvas.getContext('2d', { alpha: false });

  // ------------------------------------------------------------- sprites --
  //
  // The same text-authored pixel art the turn-based page draws from. Each
  // sprite becomes a small canvas once, with a white silhouette beside it for
  // the frame an enemy flashes when it is hit.
  var Sprites = (function () {
    var data = window.NAV_SPRITES || null;
    var cache = {};

    function rgb(hex) {
      var n = parseInt(hex.slice(1), 16);
      return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
    }
    function normalise(hex) {
      var c = rgb(hex), top = Math.max(c[0], c[1], c[2]) || 255, k = 255 / top;
      return [c[0] * k / 255, c[1] * k / 255, c[2] * k / 255];
    }
    function build(name, tint, white) {
      var s = data && data.sprites[name];
      if (!s) return null;
      var n = s.s || data.size;
      var c = document.createElement('canvas');
      c.width = n; c.height = n;
      var g = c.getContext('2d');
      var img = g.createImageData(n, n), px = img.data;
      var pal = s.p.map(rgb);
      var mul = (tint && s.t) ? normalise(tint) : null;
      for (var i = 0; i < n * n; i++) {
        var ch = s.d.charCodeAt(i);
        if (ch === 46) continue;
        var col = pal[ch <= 57 ? ch - 48 : ch - 87];
        if (!col) continue;
        var o = i * 4;
        if (white) { px[o] = px[o + 1] = px[o + 2] = 255; }
        else {
          px[o] = mul ? Math.round(col[0] * mul[0]) : col[0];
          px[o + 1] = mul ? Math.round(col[1] * mul[1]) : col[1];
          px[o + 2] = mul ? Math.round(col[2] * mul[2]) : col[2];
        }
        px[o + 3] = 255;
      }
      g.putImageData(img, 0, 0);
      return c;
    }
    return {
      get: function (name, tint) {
        var key = name + '|' + (tint || '');
        if (!(key in cache)) cache[key] = build(name, tint, false);
        return cache[key];
      },
      white: function (name) {
        var key = name + '|#white';
        if (!(key in cache)) cache[key] = build(name, null, true);
        return cache[key];
      }
    };
  })();

  var TINT_OF = { wall: 'tintWall', floor: 'tintFloor', stairs_down: 'tintFloor', water: 'tintLiquid' };

  // -------------------------------------------------------------- engine --
  var Api = null;

  function read(m, ptr) {
    var s = m.UTF8ToString(ptr);
    m._nav_free_cstr(ptr);
    return s;
  }

  function boot() {
    if (typeof NavModule !== 'function') {
      $('goBtn').textContent = 'Движок не загрузился';
      $('goBtn').disabled = true;
      return;
    }
    NavModule().then(function (m) {
      Api = {
        start: function (seed, cls) { m._nav_rt_start(seed, cls); },
        step: function (mx, my, fire, n) { m._nav_rt_step(mx, my, fire ? 1 : 0, n); },
        map: function () { return JSON.parse(read(m, m._nav_rt_map_json())); },
        frame: function () { return JSON.parse(read(m, m._nav_rt_frame_json())); }
      };
    });
  }

  // --------------------------------------------------------------- state --
  var map = null;        // the floor, re-read only when its revision moves
  var layer = null;      // the floor drawn once at 16 px a cell
  var cur = null;        // the latest frame
  var prevPos = {};      // id -> [x, y] one step before `cur`, for interpolation
  var lastEnemies = {};  // id -> enemy, to colour a death after it is gone
  var running = false, paused = false, over = false;

  // Camera, in cells, and the pixel size of a cell on this screen.
  var cam = { x: 0, y: 0 };
  var cellPx = 64, scale = 4;
  var W = 0, H = 0, dpr = 1;

  // Juice.
  var shake = 0;           // current magnitude, in cells
  var flash = null;        // {color, alpha}
  var particles = [];
  var floaters = [];
  var bannerTimer = 0;

  // ---------------------------------------------------------------- sizing --
  //
  // A cell is a whole multiple of the 16-pixel art, never a fraction: scaled by
  // 5.3 a sprite has some pixels one column wide and some two, and the art
  // shimmers as it moves. The multiple is chosen so the view holds roughly a
  // room — the zoom Soul Knight plays at — rather than the whole floor.
  function resize() {
    dpr = window.devicePixelRatio || 1;
    W = Math.round(window.innerWidth * dpr);
    H = Math.round(window.innerHeight * dpr);
    canvas.width = W;
    canvas.height = H;
    var landscapeCols = TOUCH ? 15 : 21, rows = TOUCH ? 9 : 12;
    scale = Math.max(1, Math.floor(Math.min(W / (landscapeCols * 16), H / (rows * 16))));
    cellPx = 16 * scale;
    ctx.imageSmoothingEnabled = false;
    buildVignette();
  }
  window.addEventListener('resize', resize);

  var vignette = null;
  function buildVignette() {
    vignette = document.createElement('canvas');
    vignette.width = W; vignette.height = H;
    var g = vignette.getContext('2d');
    var r = Math.max(W, H) * 0.75;
    var grad = g.createRadialGradient(W / 2, H / 2, r * 0.35, W / 2, H / 2, r);
    grad.addColorStop(0, 'rgba(0,0,0,0)');
    grad.addColorStop(1, 'rgba(0,0,0,0.55)');
    g.fillStyle = grad;
    g.fillRect(0, 0, W, H);
  }

  // --------------------------------------------------------------- floor --
  //
  // The whole floor painted once at the art's own resolution and scaled up by
  // one drawImage per frame. Remembered-but-unseen ground is darkened here, so
  // the fog costs nothing at draw time.
  function rebuildLayer() {
    var w = map.w, h = map.h;
    if (!layer || layer.width !== w * 16 || layer.height !== h * 16) {
      layer = document.createElement('canvas');
      layer.width = w * 16;
      layer.height = h * 16;
    }
    var g = layer.getContext('2d');
    g.imageSmoothingEnabled = false;
    g.fillStyle = '#060504';
    g.fillRect(0, 0, layer.width, layer.height);
    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        var i = y * w + x;
        var vis = map.vis.charCodeAt(i) - 48;
        if (vis === 0) continue;
        var key = map.terrainKeys[map.terrain.charCodeAt(i) - 65];
        var bmp = key ? Sprites.get(key, TINT_OF[key] ? map[TINT_OF[key]] : null) : null;
        if (bmp) g.drawImage(bmp, x * 16, y * 16);
        if (vis === 1) {
          g.fillStyle = 'rgba(6,5,4,0.62)';
          g.fillRect(x * 16, y * 16, 16, 16);
        }
      }
    }
  }

  function doorCells() {
    var out = [];
    if (!map) return out;
    var doorIndex = map.terrainKeys.indexOf('door');
    if (doorIndex < 0) return out;
    var code = 65 + doorIndex;
    for (var i = 0; i < map.terrain.length; i++)
      if (map.terrain.charCodeAt(i) === code && map.vis.charCodeAt(i) === 50)
        out.push([i % map.w, (i / map.w) | 0]);
    return out;
  }
  var lockedDoors = [];

  // --------------------------------------------------------------- input --
  var keys = {};
  var stick = { id: null, ox: 0, oy: 0, x: 0, y: 0 };
  var fireTouch = null, mouseFire = false;

  function moveVector() {
    if (stick.id !== null) return [stick.x, stick.y];
    var x = 0, y = 0;
    if (keys.KeyA || keys.ArrowLeft) x -= 1;
    if (keys.KeyD || keys.ArrowRight) x += 1;
    if (keys.KeyW || keys.ArrowUp) y -= 1;
    if (keys.KeyS || keys.ArrowDown) y += 1;
    var l = Math.hypot(x, y);
    return l > 0 ? [x / l, y / l] : [0, 0];
  }
  function firing() {
    return !!(keys.Space || keys.KeyJ || keys.KeyK || mouseFire || fireTouch !== null);
  }

  document.addEventListener('keydown', function (e) {
    if (e.code === 'Escape' || e.code === 'KeyP') { togglePause(); e.preventDefault(); return; }
    if (e.code === 'Enter' && !running) { begin(); e.preventDefault(); return; }
    keys[e.code] = true;
    if (e.code === 'Space' || e.code.indexOf('Arrow') === 0) e.preventDefault();
  });
  document.addEventListener('keyup', function (e) { keys[e.code] = false; });
  window.addEventListener('blur', function () { keys = {}; mouseFire = false; });

  // Pointer events, filtered to a real mouse. A phone follows every tap with
  // compatibility mouse events, and listening for those made each touch of
  // the stick fire a stray bolt.
  canvas.addEventListener('pointerdown', function (e) {
    if (e.pointerType === 'mouse' && e.button === 0) mouseFire = true;
  });
  window.addEventListener('pointerup', function (e) {
    if (e.pointerType === 'mouse') mouseFire = false;
  });

  // A floating stick: it appears under the thumb wherever the thumb lands on
  // the left half, which is what lets a player find it without looking.
  var stickEl = $('stick'), knobEl = stickEl.querySelector('.knob');
  var STICK_R = 52;
  canvas.addEventListener('pointerdown', function (e) {
    if (e.pointerType !== 'touch') return;
    if (e.clientX < window.innerWidth * 0.5 && stick.id === null) {
      stick.id = e.pointerId;
      stick.ox = e.clientX; stick.oy = e.clientY;
      stick.x = stick.y = 0;
      stickEl.style.left = e.clientX + 'px';
      stickEl.style.top = e.clientY + 'px';
      knobEl.style.transform = '';
      stickEl.hidden = false;
    } else if (e.clientX >= window.innerWidth * 0.5 && fireTouch === null) {
      // The whole right half fires, not just the button: a thumb in a fight
      // does not land where it was aimed.
      fireTouch = e.pointerId;
      $('fireBtn').classList.add('down');
    }
    // Capture keeps the stick answering when the thumb slides off the canvas;
    // it can refuse a pointer that has already lifted, and that must not
    // abandon the rest of the handler.
    try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* already released */ }
  });
  canvas.addEventListener('pointermove', function (e) {
    if (e.pointerId !== stick.id) return;
    var dx = e.clientX - stick.ox, dy = e.clientY - stick.oy;
    var l = Math.hypot(dx, dy);
    // Past the rim the base follows the thumb, so dragging further never
    // means a thumb off the edge of a stick that no longer answers.
    if (l > STICK_R) {
      stick.ox = e.clientX - dx / l * STICK_R;
      stick.oy = e.clientY - dy / l * STICK_R;
      stickEl.style.left = stick.ox + 'px';
      stickEl.style.top = stick.oy + 'px';
      dx = dx / l * STICK_R; dy = dy / l * STICK_R; l = STICK_R;
    }
    var dead = 8;
    var k = l < dead ? 0 : Math.min(1, (l - dead) / (STICK_R - dead));
    stick.x = l > 0 ? dx / l * k : 0;
    stick.y = l > 0 ? dy / l * k : 0;
    knobEl.style.transform = 'translate(' + dx + 'px,' + dy + 'px)';
  });
  function releasePointer(e) {
    if (e.pointerId === stick.id) { stick.id = null; stick.x = stick.y = 0; stickEl.hidden = true; }
    if (e.pointerId === fireTouch) { fireTouch = null; $('fireBtn').classList.remove('down'); }
  }
  canvas.addEventListener('pointerup', releasePointer);
  canvas.addEventListener('pointercancel', releasePointer);

  var fireBtn = $('fireBtn');
  fireBtn.addEventListener('pointerdown', function (e) {
    fireTouch = e.pointerId;
    fireBtn.classList.add('down');
    try { fireBtn.setPointerCapture(e.pointerId); } catch (err) { /* already released */ }
    e.preventDefault();
  });
  fireBtn.addEventListener('pointerup', releasePointer);
  fireBtn.addEventListener('pointercancel', releasePointer);

  $('pauseBtn').addEventListener('click', togglePause);
  document.addEventListener('visibilitychange', function () {
    if (document.hidden && running && !paused && !over) togglePause();
  });

  // --------------------------------------------------------------- juice --
  function kick(amount) { if (!REDUCED) shake = Math.min(0.6, shake + amount); }
  function screenFlash(color, alpha) { if (!REDUCED) flash = { color: color, alpha: alpha }; }

  function burst(x, y, color, count, speed, life) {
    if (REDUCED) count = Math.min(count, 3);
    for (var i = 0; i < count; i++) {
      var a = Math.random() * Math.PI * 2, v = speed * (0.35 + Math.random() * 0.65);
      particles.push({ x: x, y: y, vx: Math.cos(a) * v, vy: Math.sin(a) * v,
                       life: life * (0.6 + Math.random() * 0.4), max: life, color: color,
                       size: 0.06 + Math.random() * 0.07 });
    }
  }
  function floater(x, y, text, color) {
    floaters.push({ x: x + (Math.random() - 0.5) * 0.3, y: y - 0.3, text: text, color: color, t: 0 });
  }
  function banner(html, alarm) {
    var el = $('banner');
    el.innerHTML = html;
    el.classList.toggle('alarm', !!alarm);
    el.classList.add('show');
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(function () { el.classList.remove('show'); }, 1700);
  }
  function hurtBar(which) {
    var el = document.querySelector('.bar.' + which);
    el.classList.remove('hurt');
    void el.offsetWidth;   // restart the animation
    el.classList.add('hurt');
  }

  function handleEvents(events) {
    for (var i = 0; i < events.length; i++) {
      var e = events[i], kind = e[0], x = e[1], y = e[2], amount = e[3], ref = e[4];
      switch (kind) {
        case EV.Shot:
          burst(x, y, '#ffe7a6', 2, 1.5, 0.08);
          break;
        case EV.EnemyShot:
          burst(x, y, '#ff8f6a', 2, 1.2, 0.1);
          break;
        case EV.BulletWall:
          burst(x, y, ref ? '#f3d58c' : '#ff8f6a', 4, 2.4, 0.18);
          break;
        case EV.EnemyHit:
          floater(x, y, String(amount), '#fff4d6');
          burst(x, y, '#fff4d6', 3, 2, 0.15);
          kick(0.04);
          break;
        case EV.EnemyDied: {
          var dead = lastEnemies[ref];
          burst(x, y, dead ? dead.color : '#c9b48a', 16, 3.2, 0.45);
          burst(x, y, '#ffffff', 4, 1.5, 0.2);
          kick(0.12);
          break;
        }
        case EV.HeroHit:
          floater(x, y, '−' + amount, '#ff6b5a');
          screenFlash('#c0281c', 0.28);
          kick(0.32);
          hurtBar('hp');
          break;
        case EV.ShieldHit:
          floater(x, y, '−' + amount, '#a9c8ea');
          screenFlash('#5f8fc4', 0.16);
          kick(0.16);
          hurtBar('sh');
          break;
        case EV.ShieldBroken:
          burst(x, y, '#a9c8ea', 10, 2.6, 0.35);
          break;
        case EV.Pickup:
          floater(x, y, '+' + amount, ref === 0 ? '#e8c46a' : '#7fd88a');
          burst(x, y, ref === 0 ? '#e8c46a' : '#7fd88a', 6, 1.6, 0.25);
          break;
        case EV.RoomLocked:
          banner('Двери захлопнулись', true);
          kick(0.22);
          break;
        case EV.RoomCleared:
          banner('Чисто');
          break;
        case EV.Descended:
          arriving = 0.6;
          break;
        case EV.HeroDied:
          kick(0.5);
          screenFlash('#7a0f08', 0.55);
          setTimeout(showDeath, 1100);
          break;
        case EV.Victory:
          setTimeout(showVictory, 600);
          break;
      }
    }
  }

  // ---------------------------------------------------------------- frame --
  function takeFrame() {
    var f = Api.frame();
    // Where everyone was one step ago, so drawing can glide between steps on a
    // screen faster than sixty hertz instead of jumping on every second frame.
    if (cur) {
      prevPos.hero = [cur.hero.x, cur.hero.y];
      for (var i = 0; i < cur.enemies.length; i++) {
        var e = cur.enemies[i];
        prevPos[e.id] = [e.x, e.y];
      }
    }
    for (var j = 0; j < f.enemies.length; j++) lastEnemies[f.enemies[j].id] = f.enemies[j];
    if (!map || f.rev !== map.rev) {
      var wasDepth = map ? map.depth : 0;
      map = Api.map();
      rebuildLayer();
      lockedDoors = doorCells();
      if (map.depth !== wasDepth) {
        prevPos = {};
        cam.x = f.hero.x; cam.y = f.hero.y;
        banner('Этаж ' + map.depth + '<small>' + map.zone + '</small>');
      }
    }
    handleEvents(f.events);
    cur = f;
  }

  function lerpPos(id, x, y, a) {
    var p = prevPos[id];
    if (!p) return [x, y];
    // A jump of more than a cell in one step is a teleport — a new floor, a
    // guard pushed out of a door — and gliding across it would lie.
    if (Math.abs(p[0] - x) > 1 || Math.abs(p[1] - y) > 1) return [x, y];
    return [p[0] + (x - p[0]) * a, p[1] + (y - p[1]) * a];
  }

  // ----------------------------------------------------------------- draw --
  var arriving = 0;   // fade-in after the stairs

  function draw(alpha, dt) {
    var f = cur;
    var hp = lerpPos('hero', f.hero.x, f.hero.y, alpha);

    // The camera leans a little towards where the hero is shooting, so the
    // fight in front gets more of the screen than the corridor behind.
    var lead = f.hero.fire ? 1.1 : 0.35;
    var tx = hp[0] + f.hero.ax * lead, ty = hp[1] + f.hero.ay * lead;
    var ease = 1 - Math.exp(-dt * 9);
    cam.x += (tx - cam.x) * ease;
    cam.y += (ty - cam.y) * ease;

    var sx = 0, sy = 0;
    if (shake > 0.001) {
      sx = (Math.random() * 2 - 1) * shake;
      sy = (Math.random() * 2 - 1) * shake;
      shake *= Math.exp(-dt * 14);
    }

    // World cell (x, y) lands on screen at ox + x * cellPx.
    var ox = Math.round(W / 2 - (cam.x + sx) * cellPx);
    var oy = Math.round(H / 2 - (cam.y + sy) * cellPx);
    function px(x) { return Math.round(ox + x * cellPx); }
    function py(y) { return Math.round(oy + y * cellPx); }

    ctx.fillStyle = '#060504';
    ctx.fillRect(0, 0, W, H);
    if (layer) ctx.drawImage(layer, 0, 0, layer.width, layer.height, ox, oy, layer.width * scale, layer.height * scale);

    // Doors a fight has slammed glow, so the reason the corridor is gone is
    // on the screen and not only in the banner.
    if (f.locked && lockedDoors.length) {
      var pulse = 0.35 + 0.25 * Math.sin(f.t * 8);
      ctx.fillStyle = 'rgba(220,70,50,' + pulse.toFixed(3) + ')';
      for (var d = 0; d < lockedDoors.length; d++)
        ctx.fillRect(px(lockedDoors[d][0]), py(lockedDoors[d][1]), cellPx, cellPx);
    }

    // The way down, marked so it reads from across a room.
    if (map && map.vis.charCodeAt(map.exit[1] * map.w + map.exit[0]) > 48) {
      var glow = 0.25 + 0.15 * Math.sin(f.t * 3);
      ctx.fillStyle = 'rgba(215,180,106,' + glow.toFixed(3) + ')';
      ctx.fillRect(px(map.exit[0]) - scale * 2, py(map.exit[1]) - scale * 2, cellPx + scale * 4, cellPx + scale * 4);
    }

    // Pickups, bobbing.
    for (var p = 0; p < f.pickups.length; p++) {
      var pk = f.pickups[p];
      var bob = Math.sin(f.t * 5 + pk[0]) * 0.06;
      var bmp = Sprites.get(pk[1] === 0 ? 'item_gold' : 'item_potion', null);
      var sz = cellPx * 0.7;
      if (bmp) ctx.drawImage(bmp, px(pk[2]) - sz / 2, py(pk[3] + bob) - sz / 2, sz, sz);
    }

    // Shadows first, so no creature's shadow lands on another creature.
    ctx.fillStyle = 'rgba(0,0,0,0.35)';
    for (var s = 0; s < f.enemies.length; s++) {
      var se = lerpPos(f.enemies[s].id, f.enemies[s].x, f.enemies[s].y, alpha);
      shadow(px(se[0]), py(se[1] + 0.36), cellPx * 0.32);
    }
    shadow(px(hp[0]), py(hp[1] + 0.36), cellPx * 0.3);

    // Enemies.
    for (var k = 0; k < f.enemies.length; k++) {
      var e = f.enemies[k];
      var ep = lerpPos(e.id, e.x, e.y, alpha);
      var walk = e.awake ? Math.abs(Math.sin(f.t * 10 + e.id)) * 0.06 : 0;
      creature(e.hurt ? Sprites.white(e.key) : Sprites.get(e.key, null),
               ep[0], ep[1] - walk, e.fx < 0, px, py, e.glyph, e.color);
      if (e.hp < e.max) healthPip(px(ep[0]), py(ep[1] - 0.62), e.hp / e.max);
    }

    // The hero: blinking while nothing can hurt them, bobbing while walking.
    var blink = f.hero.inv && !REDUCED && Math.floor(f.t * 20) % 2 === 0;
    if (!blink && !f.dead) {
      var hb = f.hero.mv ? Math.abs(Math.sin(f.t * 12)) * 0.07 : Math.sin(f.t * 2.5) * 0.015;
      var aimLeft = (f.hero.fire ? f.hero.ax : f.hero.fx) < 0;
      creature(Sprites.get(f.hero.key, null), hp[0], hp[1] - hb, aimLeft, px, py, '@', '#e7ddcc');
      weapon(px(hp[0]), py(hp[1] + 0.05 - hb), f.hero.ax, f.hero.ay, f.hero.fire);
    }

    // Bullets: a hot core and a soft glow, with a streak behind the fast ones.
    for (var b = 0; b < f.bullets.length; b++) {
      var bl = f.bullets[b], bx = px(bl[0]), by = py(bl[1]);
      if (bl[2]) {
        ctx.fillStyle = 'rgba(255,221,140,0.28)';
        circle(bx, by, cellPx * 0.2);
        ctx.fillStyle = '#fff3cf';
        circle(bx, by, cellPx * 0.08);
      } else {
        ctx.fillStyle = 'rgba(255,110,70,0.3)';
        circle(bx, by, cellPx * 0.24);
        ctx.fillStyle = '#ffd0b8';
        circle(bx, by, cellPx * 0.1);
      }
    }

    // Particles.
    for (var q = particles.length - 1; q >= 0; q--) {
      var pa = particles[q];
      pa.life -= dt;
      if (pa.life <= 0) { particles.splice(q, 1); continue; }
      pa.x += pa.vx * dt; pa.y += pa.vy * dt;
      pa.vx *= Math.exp(-dt * 6); pa.vy *= Math.exp(-dt * 6);
      ctx.globalAlpha = Math.max(0, pa.life / pa.max);
      ctx.fillStyle = pa.color;
      var ps = Math.max(scale, pa.size * cellPx);
      ctx.fillRect(px(pa.x) - ps / 2, py(pa.y) - ps / 2, ps, ps);
    }
    ctx.globalAlpha = 1;

    // Floating numbers.
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = '700 ' + Math.round(cellPx * 0.34) + 'px ui-monospace, Menlo, Consolas, monospace';
    for (var n = floaters.length - 1; n >= 0; n--) {
      var fl = floaters[n];
      fl.t += dt;
      if (fl.t > 0.75) { floaters.splice(n, 1); continue; }
      var rise = fl.t * 1.1, pop = fl.t < 0.08 ? 1 + (0.08 - fl.t) * 5 : 1;
      ctx.globalAlpha = fl.t < 0.5 ? 1 : 1 - (fl.t - 0.5) / 0.25;
      ctx.save();
      ctx.translate(px(fl.x), py(fl.y - rise));
      ctx.scale(pop, pop);
      ctx.lineWidth = Math.max(2, scale);
      ctx.strokeStyle = '#000';
      ctx.strokeText(fl.text, 0, 0);
      ctx.fillStyle = fl.color;
      ctx.fillText(fl.text, 0, 0);
      ctx.restore();
    }
    ctx.globalAlpha = 1;

    if (vignette) ctx.drawImage(vignette, 0, 0);

    if (flash && flash.alpha > 0.01) {
      ctx.globalAlpha = flash.alpha;
      ctx.fillStyle = flash.color;
      ctx.fillRect(0, 0, W, H);
      ctx.globalAlpha = 1;
      flash.alpha *= Math.exp(-dt * 9);
    }
    if (arriving > 0) {
      ctx.globalAlpha = Math.min(1, arriving / 0.6);
      ctx.fillStyle = '#000';
      ctx.fillRect(0, 0, W, H);
      ctx.globalAlpha = 1;
      arriving -= dt;
    }
  }

  function circle(x, y, r) {
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
  }
  function shadow(x, y, r) {
    ctx.beginPath();
    ctx.ellipse(x, y, r, r * 0.4, 0, 0, Math.PI * 2);
    ctx.fill();
  }
  function creature(bmp, x, y, flip, px, py, glyph, color) {
    var cx = px(x), cy = py(y);
    if (!bmp) {
      ctx.fillStyle = color;
      ctx.font = '700 ' + Math.round(cellPx * 0.8) + 'px ui-monospace, Menlo, monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(glyph, cx, cy);
      return;
    }
    ctx.save();
    ctx.translate(cx, cy);
    if (flip) ctx.scale(-1, 1);
    ctx.drawImage(bmp, -cellPx / 2, -cellPx / 2, cellPx, cellPx);
    ctx.restore();
  }
  function weapon(x, y, ax, ay, hot) {
    // A crossbow, as a short bar from the hero's hand along the aim. It is the
    // one thing on screen that says where the automatic aim has settled.
    var len = cellPx * 0.42;
    ctx.save();
    ctx.translate(x + ax * cellPx * 0.18, y + ay * cellPx * 0.18);
    ctx.rotate(Math.atan2(ay, ax));
    ctx.fillStyle = '#5b3c1c';
    ctx.fillRect(0, -scale, len, scale * 2);
    ctx.fillStyle = hot ? '#ffe7a6' : '#c8a45c';
    ctx.fillRect(len - scale * 2, -scale * 2, scale * 2, scale * 4);
    ctx.restore();
  }
  function healthPip(x, y, frac) {
    var w = cellPx * 0.7, h = Math.max(2, scale);
    ctx.fillStyle = 'rgba(0,0,0,0.7)';
    ctx.fillRect(x - w / 2 - 1, y - 1, w + 2, h + 2);
    ctx.fillStyle = frac > 0.5 ? '#9ad26a' : (frac > 0.25 ? '#e8c46a' : '#e8604f');
    ctx.fillRect(x - w / 2, y, Math.round(w * frac), h);
  }

  // ------------------------------------------------------------------ HUD --
  var hudCache = {};
  function setBar(which, value, max) {
    var key = which + value + '/' + max;
    if (hudCache[which] === key) return;
    hudCache[which] = key;
    var el = document.querySelector('.bar.' + which);
    var pct = max > 0 ? Math.max(0, Math.min(100, value * 100 / max)) : 0;
    el.querySelector('i').style.width = pct + '%';
    el.querySelector('b').style.width = pct + '%';
    el.querySelector('span').textContent = value + ' / ' + max;
  }
  function hud(f) {
    setBar('hp', f.hero.hp, f.hero.maxHp);
    setBar('sh', f.hero.sh, f.hero.maxSh);
    var floor = 'Этаж ' + f.depth + (map ? ' · ' + map.zone : '');
    if (hudCache.floor !== floor) { hudCache.floor = floor; $('floor').textContent = floor; }
    var coins = '◆ ' + f.hero.coins;
    if (hudCache.coins !== coins) { hudCache.coins = coins; $('coins').textContent = coins; }
  }

  // ----------------------------------------------------------------- loop --
  var acc = 0, last = 0;

  function loop(now) {
    if (!running) return;
    requestAnimationFrame(loop);
    var dt = last ? Math.min(0.1, (now - last) / 1000) : STEP;
    last = now;
    if (paused || over) { if (cur) draw(1, 0); return; }

    acc += dt;
    var steps = Math.floor(acc / STEP);
    if (steps > 0) {
      acc -= steps * STEP;
      var mv = moveVector();
      Api.step(mv[0], mv[1], firing(), Math.min(steps, 8));
      takeFrame();
    }
    draw(Math.min(1, acc / STEP), dt);
    hud(cur);
  }

  // --------------------------------------------------------------- screens --
  var overlay = $('overlay');

  function begin() {
    if (!Api) return;
    over = false;
    paused = false;
    map = null;
    cur = null;
    prevPos = {};
    lastEnemies = {};
    particles = [];
    floaters = [];
    hudCache = {};
    // A fresh dungeon each descent. The seed is not shown yet: there is no
    // way in this build to type one in, so naming it would promise nothing.
    Api.start(Math.floor(Math.random() * 4294967295), 0);
    takeFrame();
    cam.x = cur.hero.x; cam.y = cur.hero.y;
    overlay.hidden = true;
    $('hud').hidden = false;
    $('pauseBtn').hidden = false;
    if (TOUCH) fireBtn.hidden = false;
    if (!running) {
      running = true;
      last = 0;
      acc = 0;
      requestAnimationFrame(loop);
    }
  }

  function togglePause() {
    if (!running || over) return;
    paused = !paused;
    keys = {};
    if (paused) {
      overlay.innerHTML = '<div class="sheet"><h2>Пауза</h2><p class="sub">Этаж ' + cur.depth +
        ' · ' + (map ? map.zone : '') + '</p><button type="button" class="go" id="resumeBtn">Дальше</button>' +
        '<p class="hint"><a href="index.html">Пошаговая версия</a></p></div>';
      overlay.hidden = false;
      $('resumeBtn').addEventListener('click', togglePause);
      $('resumeBtn').focus();
    } else {
      overlay.hidden = true;
      last = 0;
    }
  }

  function endScreen(title, sub) {
    over = true;
    var f = cur;
    overlay.innerHTML = '<div class="sheet"><h2>' + title + '</h2><p class="sub">' + sub + '</p>' +
      '<div class="stats"><div><b>' + f.depth + '</b><span>этаж</span></div>' +
      '<div><b>' + f.hero.kills + '</b><span>убито</span></div>' +
      '<div><b>' + f.hero.coins + '</b><span>монет</span></div></div>' +
      '<button type="button" class="go" id="againBtn">Ещё раз</button>' +
      '<p class="hint"><a href="index.html">Пошаговая версия</a></p></div>';
    overlay.hidden = false;
    fireBtn.hidden = true;
    $('againBtn').addEventListener('click', begin);
    $('againBtn').focus();
  }
  function showDeath() { endScreen('Ты пал', 'Навь забрала ещё одного. Смерть здесь окончательна.'); }
  function showVictory() { endScreen('Ты прошёл Навь', 'Шестнадцать этажей позади.'); }

  $('goBtn').addEventListener('click', begin);

  resize();
  boot();
})();
