/* Original BSI Outline icons for the PLAYER, not Customizer navigation.
   Explicit SVG colors, basic QtSvg-compatible primitives, no fonts or scripts. */
(() => {
  'use strict';
  const icons = [
    ['back','Back','topbar','back','<path d="m12 5-7 7 7 7M5 12h15"/>'],
    ['home','Home','topbar','home','<path d="m3 10 9-7 9 7M5 9v12h14V9M10 21v-7h4v7"/>'],
    ['recent','Recent apps','topbar','recent','<rect x="7" y="3" width="14" height="15" rx="3"/><path d="M3 7v11a3 3 0 0 0 3 3h11M11 7h6"/>'],
    ['help','Help','topbar','help','<path d="M8 8a4 4 0 0 1 8 0c0 3-4 3-4 6M12 18v1"/><rect x="3" y="2" width="18" height="20" rx="6"/>'],
    ['menu','Menu','topbar','menu','<path d="M4 5h16M4 12h11M4 19h16"/><circle cx="19" cy="12" r="1"/>'],
    ['minimize','Minimize','topbar','minimize','<path d="M5 17h14M9 13l3 4 3-4"/>'],
    ['maximize','Maximize','topbar','maximize','<rect x="4" y="4" width="16" height="16" rx="3"/><path d="M8 8h8M16 8v8"/>'],
    ['close','Close','topbar','close','<path d="m6 6 12 12M18 6 6 18"/>'],
    ['fullscreen','Fullscreen','sidebar','fullscreen','<path d="M9 3H3v6M15 3h6v6M3 15v6h6M21 15v6h-6M8 8l-5-5M16 16l5 5"/>'],
    ['volume','Volume','sidebar','volume','<path d="M3 9h4l5-5v16l-5-5H3zM16 8a6 6 0 0 1 0 8M19 5a10 10 0 0 1 0 14"/>'],
    ['mute','Mute','sidebar','mute','<path d="M3 9h4l5-5v16l-5-5H3zM17 9l5 6M22 9l-5 6"/>'],
    ['keymap','Key mapping','sidebar','key','<rect x="2" y="5" width="20" height="14" rx="3"/><path d="M6 9h1M11 9h1M16 9h2M6 13h1M11 13h1M16 13h2M8 16h8"/>'],
    ['gamepad','Game controls','sidebar','gamepad','<path d="M7 6h10c3 0 6 12 3 13-2 1-4-3-5-3H9c-1 0-3 4-5 3C1 18 4 6 7 6Z M6 10v5M3.5 12.5h5M16 10h.1M19 13h.1"/>'],
    ['macro','Macro recorder','sidebar','macro','<circle cx="8" cy="7" r="3"/><circle cx="8" cy="17" r="3"/><path d="m16 7 5 5-5 5zM8 10v4"/>'],
    ['screenshot','Screenshot','sidebar','screenshot','<path d="M3 8h4l2-3h6l2 3h4v12H3z"/><circle cx="12" cy="13" r="4"/>'],
    ['rotate','Rotate screen','sidebar','rotate','<rect x="8" y="4" width="9" height="16" rx="2" transform="rotate(20 12 12)"/><path d="M3 12a9 9 0 0 1 3-7M2 5h4v4M21 12a9 9 0 0 1-3 7M22 19h-4v-4"/>'],
    ['install','Install APK','sidebar','apk','<path d="M5 9v12h14V9M3 9h18M12 2v12M8 10l4 4 4-4"/>'],
    ['sync','Sync operations','sidebar','sync','<path d="M4 8a9 9 0 0 1 15-2l2 2M21 3v5h-5M20 16a9 9 0 0 1-15 2l-2-2M3 21v-5h5"/>'],
    ['eco','Eco mode','sidebar','eco','<path d="M5 18C0 6 11 3 21 3c0 11-4 19-13 16M4 21 16 9M9 16v-5M13 12h5"/>'],
    ['settings','Settings','sidebar','setting','<path d="M4 6h16M4 12h16M4 18h16"/><circle cx="9" cy="6" r="2" fill="{color}"/><circle cx="16" cy="12" r="2" fill="{color}"/><circle cx="8" cy="18" r="2" fill="{color}"/>'],
    ['collapse','Collapse sidebar','sidebar','collapse','<path d="m14 5-7 7 7 7M19 5l-7 7 7 7M3 4v16"/>'],
    ['pin','Always on top','topbar','pin','<path d="m8 3 8 3-2 5 3 4-4 1-4-2-4-1 4-2zM10 15l-4 7"/>']
  ].map(([id,label,area,term,shape])=>Object.freeze({id,label,area,term,shape}));
  function svg(id,options={}) {
    const icon=icons.find(i=>i.id===id); if(!icon)throw Error('Unknown BSI outline icon.');
    const color=options.color??'#F1F5F9', stroke=String(options.stroke??1.7);
    if(!/^#[\da-f]{6}$/i.test(color)||!['1.4','1.7','2'].includes(stroke))throw Error('Invalid icon style.');
    const dimension=n=>Number.isFinite(Number(n))&&Number(n)>0&&Number(n)<=4096?Number(n):24;
    return `<svg xmlns="http://www.w3.org/2000/svg" width="${dimension(options.width)}" height="${dimension(options.height)}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${stroke}" stroke-linecap="round" stroke-linejoin="round">${icon.shape.replaceAll('{color}',color)}</svg>`;
  }
  const uri=(id,color,stroke)=>'data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg(id,{color,stroke}));
  const eligible=row=>!!row?.text && (/\.svg$/i.test(row.path||'')||String(row.format).toLowerCase()==='svg');
  function render({esc,button,selected,preview,choice='home',color='#F1F5F9',stroke='1.7'}) {
    const icon=icons.find(i=>i.id===choice)||icons[0];
    const image=id=>`<img src="${uri(id,color,stroke)}" alt="${esc(icons.find(i=>i.id===id).label)}" width="24" height="24">`;
    const controls=ids=>ids.map(id=>`<span title="${esc(icons.find(i=>i.id===id).label)}">${image(id)}</span>`).join('');
    const original=/^data:image\/(png|jpeg|webp|gif|bmp);base64,/i.test(preview||'')?`<img src="${esc(preview)}" alt="Current native raster preview">`:'<span>No original raster preview</span>';
    return `<details class="emu-icon-studio" id="emu-icon-details"><summary>Emulator icon studio <span>22 original outline SVGs · TOPBAR + SIDEBAR</span></summary><div class="emu-icon-body"><p class="note">For the BlueStacks / MSI Player itself, not BSI navigation. Original BSI shapes; not traced from the screenshot. This changes an icon visual only—not button behavior, bar spacing or QML tint rules.</p><div class="emu-demo" aria-label="Emulator icon style preview"><div class="emu-demo-top"><b>PLAYER / OUTLINE</b>${controls(['back','home','recent'])}<i></i>${controls(['help','menu','minimize','maximize','close'])}</div><div class="emu-demo-screen"><p>Your game stays unchanged<small>Illustrative chrome preview · not a running emulator</small></p></div><div class="emu-demo-side">${controls(['fullscreen','volume','keymap','macro','screenshot','rotate','install','settings'])}</div></div><div class="emu-icon-options"><label>Icon color <input type="color" id="emu-icon-color" value="${color}"></label><label>Stroke <select class="inp" id="emu-icon-stroke">${[['1.4','Thin'],['1.7','Standard'],['2','Strong']].map(([n,label])=>`<option value="${n}" ${String(stroke)===n?'selected':''}>${label}</option>`).join('')}</select></label><span>24 × 24 viewBox · rounded caps · explicit QtSvg colors</span></div><div class="emu-icon-grid">${icons.map(i=>`<button type="button" data-emulator-icon="${i.id}" aria-pressed="${i.id===choice}" title="${esc(i.label)} — ${i.area}">${image(i.id)}<span>${esc(i.label)}</span></button>`).join('')}</div><div class="emu-icon-compare"><div><small>Current resource</small>${original}</div><div><small>Chosen outline / ${esc(icon.label)}</small>${image(icon.id)}</div></div><p class="note">Target: <strong>${esc(selected?.path||'Select a scanned SVG button icon below')}</strong>. Suggested resource search: <code>${esc(icon.term)}</code>. Names vary by emulator build; map each icon yourself, including hover/disabled variants. Do not replace logos or backgrounds.</p>${eligible(selected)?button('Stage '+icon.label+' in selected SVG','stageEmulatorIcon',true):'<p class="note">Scan Resources manually, then select a text-editable .svg icon to enable staging. PNGs, compiled files and unrelated text cannot receive an SVG preset.</p>'}<p class="note">Staging changes the editor draft only. Review the measured size, extract the original if wanted, then use Save resource text to apply through the original native engine. It may refuse a resource that cannot fit; no EXE growth or forced overwrite. Restore EXE backup restores the whole file, not just one icon.</p></div></details>`;
  }
  window.BsiEmulatorIcons=Object.freeze({icons:Object.freeze(icons),svg,eligible,render});
})();
