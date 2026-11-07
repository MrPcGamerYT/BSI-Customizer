#!/usr/bin/env python3
"""Recolor ONLY the compact sidebar visual component; retain all native code."""
from pathlib import Path
import re
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/sidebar-panel'))
from build_external import structure,qcompress

COLORS={
 '#665774':'#63636d','#f0e5ff':'#ffffff','#c3aedc':'#d5d5de',
 '#291737':'#080809','#20122d':'#070708','#170f21':'#080809',
 '#624078':'#34343b','#110a19':'#030304',
 '#75459a':'#34343c','#573374':'#26262d','#402653':'#25252c',
 '#b78bd9':'#858590','#795494':'#565660','#c18aee':'#e2e2e9'
}
def monochrome(source):
    code,pairs,_=structure(source)
    match=re.search(r'component\s+BsiSidebarInlineVisual\s*:\s*Item\s*{',code)
    if not match or 'BSI SIDEBAR INLINE V1 COMPACT' not in source:
        raise ValueError('Expected the compact inline sidebar, not an unknown/native resource.')
    start=match.start();end=pairs[code.index('{',start)]+1
    visual=source[start:end]
    for old,new in COLORS.items():
        if old not in visual:raise ValueError('Unexpected visual palette: '+old)
        visual=visual.replace(old,new)
    return (source[:start]+visual+source[end:]).replace('BSI SIDEBAR INLINE V1 COMPACT','BSI SIDEBAR MONO V1 COMPACT',1)

if __name__=='__main__':
    source=(ROOT/'downloads/Sidebar-Compact/PlrSidebar.qml').read_text()
    output=monochrome(source)
    size=len(qcompress(output.encode('utf-8')))
    if size>15868:raise SystemExit(f'Refused {size}/15868 bytes')
    folder=ROOT/'downloads/Player-Chrome-Monochrome'
    folder.mkdir(parents=True,exist_ok=True)
    (folder/'PlrSidebar.qml').write_text(output,encoding='utf-8')
    print(f'Created complete monochrome sidebar: {size}/15868 compressed bytes')
