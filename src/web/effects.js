/* Presentation only. No auth decisions, file I/O or patch logic live here. */
(() => {
  'use strict';
  const $ = selector => document.querySelector(selector);
  const reduced = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
  const enter = element => {
    if (!reduced()) element.animate([{ opacity: 0, transform: 'translateY(10px)' }, { opacity: 1, transform: 'none' }], { duration: 280, easing: 'cubic-bezier(.2,.8,.2,1)' });
  };
  let decisionQueue = Promise.resolve(), decisions = 0;
  function confirm(message) {
    ++decisions; document.body.dataset.confirming = "true";
    const next = decisionQueue.then(() => new Promise(resolve => {
      const dialog = $('#confirm-dialog');
      $('#confirm-message').textContent = message;
      const done = () => { dialog.removeEventListener('close', done); resolve(dialog.returnValue === 'yes'); };
      dialog.addEventListener('close', done);
      dialog.returnValue = 'no'; dialog.showModal();
      $('#confirm-cancel').focus();
    }));
    const result = next.finally(() => { if (!--decisions) delete document.body.dataset.confirming; });
    decisionQueue = result.catch(() => false);
    return result;
  }
  let picker = null;
  const rgb = hex => [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16));
  const toHex = values => '#' + values.map(v => Math.round(Math.min(255, Math.max(0, v))).toString(16).padStart(2, '0')).join('').toUpperCase();
  function toHsv([r, g, b]) {
    r /= 255; g /= 255; b /= 255;
    const max = Math.max(r,g,b), min = Math.min(r,g,b), delta = max-min;
    let h = !delta ? 0 : max === r ? ((g-b)/delta)%6 : max === g ? (b-r)/delta+2 : (r-g)/delta+4;
    return [(h*60+360)%360, max ? delta/max : 0, max];
  }
  function paint() {
    if (!picker) return;
    const {h,s,v} = picker;
    const f = n => { const k = (n+h/60)%6; return (v-v*s*Math.max(0,Math.min(k,4-k,1)))*255; };
    picker.hex = toHex([f(5),f(3),f(1)]);
    $('#color-dialog').style.setProperty('--hue', `hsl(${h},100%,50%)`);
    $('#cpsvh').style.left = s*100+'%'; $('#cpsvh').style.top = (1-v)*100+'%';
    $('#cphh').style.left = h/360*100+'%';
    $('#cphue').setAttribute('aria-valuenow', String(Math.round(h)));
    $('#cpnew').style.background = picker.hex;
    if (document.activeElement !== $('#cphex')) $('#cphex').value = picker.hex;
    $('#cp-rgb').textContent = 'RGB ' + rgb(picker.hex).join(' · ');
  }
  function setHex(hex) { if (!picker || !/^#[0-9a-f]{6}$/i.test(hex)) return; [picker.h,picker.s,picker.v] = toHsv(rgb(hex)); paint(); }
  function pickColor(hex) {
    if (picker) return Promise.resolve(null);
    return new Promise(resolve => {
      const dialog = $('#color-dialog');
      picker = { h: 0, s: 0, v: 0, hex, original: hex };
      $('#cpold').style.background = hex;
      dialog.returnValue = 'no'; dialog.showModal(); setHex(hex);
      const done = () => { dialog.removeEventListener('close', done); const result = dialog.returnValue === 'yes' ? picker.hex : null; picker = null; resolve(result); };
      dialog.addEventListener('close', done);
    });
  }
  function drag(element, update) {
    const apply = event => {
      const rect = element.getBoundingClientRect();
      update(Math.max(0,Math.min(1,(event.clientX-rect.left)/rect.width)), Math.max(0,Math.min(1,(event.clientY-rect.top)/rect.height))); paint();
    };
    element.addEventListener('pointerdown', event => { if (!picker) return; event.preventDefault(); element.setPointerCapture(event.pointerId); apply(event); });
    element.addEventListener('pointermove', event => { if (picker && element.hasPointerCapture(event.pointerId)) apply(event); });
    element.addEventListener('pointerup', event => { if (element.hasPointerCapture(event.pointerId)) element.releasePointerCapture(event.pointerId); });
  }
  drag($('#cpsv'), (x,y) => { picker.s=x; picker.v=1-y; });
  drag($('#cphue'), x => { picker.h=x*359.99; });
  $('#cphue').addEventListener('keydown', event => {
    if (!picker || !['ArrowLeft','ArrowRight','Home','End'].includes(event.key)) return;
    event.preventDefault(); picker.h=event.key==='Home'?0:event.key==='End'?359.99:(picker.h+(event.key==='ArrowRight'?1:-1)+360)%360; paint();
  });
  $('#cpsv').addEventListener('keydown', event => {
    if (!picker || !event.key.startsWith('Arrow')) return;
    event.preventDefault();
    picker.s=Math.max(0,Math.min(1,picker.s+(event.key==='ArrowRight' ? .01 : event.key==='ArrowLeft' ? -.01 : 0)));
    picker.v=Math.max(0,Math.min(1,picker.v+(event.key==='ArrowUp' ? .01 : event.key==='ArrowDown' ? -.01 : 0))); paint();
  });
  if (window.EyeDropper) {
    $('#cpeye').hidden=false;
    $('#cpeye').addEventListener('click', async () => {
      try { const result=await new EyeDropper().open(); if(picker)setHex(result.sRGBHex); }
      catch (_) { /* Screen sampling may be cancelled or unsupported by the host. */ }
    });
  }
  $('#cphex').addEventListener('input', event => setHex(event.target.value));
  $('#cpold').addEventListener('click', () => picker && setHex(picker.original));
  $('#cppal').addEventListener('click', event => { if (event.target.dataset.colorValue) setHex(event.target.dataset.colorValue); });
  for (const color of ['#9CE9C1','#72D9A5','#4F8FF0','#8B7CFF','#FF7B88','#FFB86B','#F5F8F6','#121212']) {
    const swatch = document.createElement('button'); swatch.type='button'; swatch.style.background=color; swatch.title=color; swatch.setAttribute('aria-label', color); swatch.dataset.colorValue=color; $('#cppal').append(swatch);
  }
  document.addEventListener('click', async event => {
    const input = event.target.closest('input[type=color]');
    if (!input || input.disabled) return;
    event.preventDefault();
    const color = await pickColor(input.value);
    if (color && input.isConnected && !input.disabled) { input.value=color; input.dispatchEvent(new Event('change', {bubbles:true})); }
  });
  document.addEventListener('click', event => {
    const details=$('#profile-menu');
    if (details.open && !event.target.closest('#profile-menu')) details.open=false;
  });
  document.addEventListener('visibilitychange', () => { document.body.classList.remove('minimizing'); if (!document.hidden) enter($('.app')); });
  async function windowAction(action, callback) {
    if (action === 'min' || action === 'close') {
      document.body.classList.add(action === 'min' ? 'minimizing' : 'closing');
      await new Promise(resolve => setTimeout(resolve, reduced() ? 0 : action === 'min' ? 180 : 260));
    }
    callback(action);
    // A busy native operation may refuse a close; never strand the display.
    setTimeout(() => document.body.classList.remove('minimizing','closing'), 600);
  }
  window.BsiEffects = { confirm, enter, windowAction };
})();
