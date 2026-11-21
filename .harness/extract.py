import re, os, sys, zlib

def entries(path):
    d = open(path,'rb').read()
    return d

def find_qml(path):
    d = open(path,'rb').read()
    starts=[m.start() for m in re.finditer(rb'/\*\r\n \* Copyright \(C\)', d)]
    out={}
    for i,s in enumerate(starts):
        end = starts[i+1] if i+1 < len(starts) else s+300000
        chunk=d[s:end]
        m=re.match(rb'/\*.*?\*/\r?\n\r?\n(.*)', chunk, re.S)
        body=m.group(1) if m else chunk
        k=body.find(b'import ')
        txt=body[k:].decode('utf-8','replace')
        depth=0; cut=len(txt)
        for idx,ch in enumerate(txt):
            if ch=='{': depth+=1
            elif ch=='}':
                depth-=1
                if depth==0: cut=idx+1; break
        txt=txt[:cut]
        if len(txt) < 200: continue
        out.setdefault(txt, s)
    return out

if __name__ == '__main__':
    d=open(sys.argv[1],'rb').read()
    print('size', len(d))
    for pat in (rb'UiSettingsWindow', rb'pSettingElements', rb'iContentLayout', rb'PlrAboutSetting', rb'SettingElementModelList', rb'Performance'):
        print(pat, len(re.findall(pat, d)))
