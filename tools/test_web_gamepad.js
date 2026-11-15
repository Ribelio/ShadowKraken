#!/usr/bin/env node
// Exercise the actual Gamepad polling function with a synthetic standard pad.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('web/index.html', 'utf8');
const start = html.indexOf('      function pollGamepad(now) {');
const end = html.indexOf('      function startGamepad()', start);
assert(start >= 0 && end > start);
const source = html.slice(start, end);

function fixture(ps = false) {
  const calls = [];
  const pad = { connected: true, mapping: 'standard', axes: [0, 0, 0, 0], buttons: Array.from({length: 16}, () => ({pressed: false})) };
  const target = {click() { calls.push('accept'); }};
  const nav = {padFrame: 0, padCommand: '', padRepeatAt: 0, padScrollAt: 0, keyboardAt: -1000, activatedAt: -1000};
  const ctx = { navigator: {getGamepads: () => [pad]}, document: {hidden: false, activeElement: target}, navigation: nav,
    playStationBrowser: ps, activeDialog: () => null, $: () => null, requestAnimationFrame: () => 1,
    switchMainTab: n => calls.push(n < 0 ? 'previousTab' : 'nextTab'),
    goBack: () => calls.push('back'), navigate: n => calls.push(n),
    focusableControls: () => [target], isNativeEditor: () => false, Math, Array };
  vm.runInNewContext(source + '\nthis.poll = pollGamepad;', ctx);
  function command(index, time) { pad.buttons.forEach(b => b.pressed = false); if (index !== null) pad.buttons[index].pressed = true; ctx.poll(time); }
  return {calls, pad, nav, command, ctx};
}
{
  const f = fixture();
  f.command(15, 1000); f.command(15, 1100); f.command(15, 1360); f.command(null, 1400);
  f.command(0, 1600); f.command(0, 1800); f.command(null, 1900);
  f.command(1, 2100); f.command(null, 2200); f.command(5, 2400); f.command(null, 2500); f.command(4, 2700);
  assert.deepEqual(f.calls, ['right', 'right', 'accept', 'back', 'nextTab', 'previousTab']);
}
{
  const f = fixture(true);
  f.command(15, 1000); f.command(null, 1100); f.command(0, 1200); f.command(null, 1300); f.command(5, 1400);
  assert.deepEqual(f.calls, ['nextTab'], 'PS browser leaves directional and Cross events to the browser');
}
{
  const f = fixture();
  f.nav.keyboardAt = 995; f.command(15, 1000);
  assert.deepEqual(f.calls, [], 'matching keyboard events do not double-activate');
  f.ctx.document.hidden = true; f.command(5, 1200);
  assert.deepEqual(f.calls, [], 'hidden pages do not process gamepad commands');
}
console.log('gamepad polling: direction/repeat, accept, back, tab switching and PS browser safeguards passed');
