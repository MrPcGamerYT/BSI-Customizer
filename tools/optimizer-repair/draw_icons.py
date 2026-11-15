"""Author the isolated, monochrome PNG glyphs (Pillow is a dev dependency only)."""
from pathlib import Path
from PIL import Image, ImageDraw
out = Path(__file__).resolve().parents[2] / 'assets/optimizer-repair/Assets/VoidOptimizerUI/icons'
out.mkdir(parents=True, exist_ok=True)
for name in ('power', 'balanced', 'performance', 'background', 'session', 'timing'):
    image = Image.new('RGBA', (96, 96))
    d = ImageDraw.Draw(image)
    ink = '#e5e5ee'
    def line(points): d.line([(x*4, y*4) for x, y in points], fill=ink, width=6, joint='curve')
    if name == 'power':
        d.arc((16,16,80,84), -56, 236, fill=ink, width=6); line([(12,2),(12,12)])
    elif name == 'balanced':
        line([(4,7),(20,7)]);line([(4,17),(20,17)])
        for x,y in ((9,7),(15,17)): d.ellipse(((x-2)*4,(y-2)*4,(x+2)*4,(y+2)*4),fill='#15151b',outline=ink,width=5)
    elif name == 'performance':
        line([(14,2),(5,14),(11,14),(10,22),(20,9),(13,9),(14,2)])
    elif name == 'background':
        line([(5,5),(5,19),(20,19)]);line([(9,9),(14,14),(19,8)])
    elif name == 'session':
        d.rounded_rectangle((12,16,84,68),radius=8,outline=ink,width=6)
        line([(12,17),(12,21)]);line([(8,21),(16,21)])
    else:
        d.ellipse((12,12,84,84),outline=ink,width=6);line([(12,6),(12,12),(16,14)])
    image.resize((48,48),Image.Resampling.LANCZOS).save(out/(name+'.png'))
