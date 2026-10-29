import math
cols=["#FF5A5F","#FFB347","#F9F871","#4CD7A6","#4DA3FF"]
cx,cy,R=128,134,104
V=[(cx+R*math.cos(math.radians(-90+72*k)),cy+R*math.sin(math.radians(-90+72*k))) for k in range(5)]
def wedge(k,off=7):
    a=V[k];b=V[(k+1)%5]
    mid=math.radians(-90+72*k+36)
    dx,dy=off*math.cos(mid),off*math.sin(mid)
    pts=[(cx,cy),a,b]
    return " ".join(f"{x+dx:.1f},{y+dy:.1f}" for x,y in pts)
def mark(fills):
    return "".join(f'<polygon points="{wedge(k)}" fill="{fills[k]}" stroke="{fills[k]}" stroke-width="3" stroke-linejoin="round"/>' for k in range(5))
def svg(vb,body,title):
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" role="img"><title>{title}</title>{body}</svg>\n'
INK="#E8ECF2"
# custom monoline letterforms (cap height 64, stem 11), no live text
STK=lambda d:f'<path d="{d}"/>'
GL={ # letter: (width, stroked-path-d or None, filled-path-d or None)
 'P':(46,"M5.5 0V64M5.5 5.5H27A14 14 0 0 1 27 33.5H5.5",None),
 'E':(40,"M5.5 0V64M0 5.5H40M0 32H36M0 58.5H40",None),
 'N':(44,None,"M0 0H12L32 41V0H44V64H32L12 23V64H0Z"),
 'T':(44,None,"M0 0H44V11H27.5V64H16.5V11H0Z"),
 'A':(44,None,"M0 64L16.5 0H27.5L44 64H32.5L22 18L11.5 64ZM14 42H30V51H14Z"),
 'G':(49,"M40.1 16.8A19 26.5 0 1 0 43.5 32H26",None),
 'R':(46,"M5.5 0V64M5.5 5.5H27A14 14 0 0 1 27 33.5H5.5","M17 34H28L45 64H32Z"),
 'I':(11,None,"M0 0H11V64H0Z"),
}
def wordpaths(color,x0=292,y0=84,sc=1.25,gap=10):
    x=0;st=[];fl=[]
    for ch in "PENTAGRIT":
        w,s,f=GL[ch]
        if s:st.append(f'<path transform="translate({x} 0)" d="{s}"/>')
        if f:fl.append(f'<path transform="translate({x} 0)" d="{f}"/>')
        x+=w+gap
    return (f'<g transform="translate({x0} {y0}) scale({sc})">'
      f'<g fill="none" stroke="{color}" stroke-width="11" stroke-linejoin="miter" stroke-miterlimit="4">{"".join(st)}</g>'
      f'<g fill="{color}">{"".join(fl)}</g></g>')
word=wordpaths
logo=svg("0 0 880 256",mark(cols)+word(INK),"Pentagrit")
logo_mono=svg("0 0 880 256",mark(["#0E1013"]*5)+word("#0E1013"),"Pentagrit mono")
t='<g transform="translate(128 128) scale(.66) translate(-128 -124)">'+mark(cols)+'</g>'
icon=svg("0 0 256 256",'<rect width="256" height="256" rx="56" fill="#0E1013"/>'+t,"Pentagrit icon")
for n,s in(("logo",logo),("logo-mono",logo_mono),("icon",icon)):
    open(f"brand/{n}.svg","w",encoding="utf-8").write(s)
tpl=open("mockup.template.html",encoding="utf-8").read()
mk=lambda f:svg("0 0 256 256",mark(f),"mark").replace('role="img"','role="img" class="m"')
tpl=tpl.replace("%%JS%%",open("mockup.js",encoding="utf-8").read())
out=tpl.replace("%%LOGO%%",logo).replace("%%LOGOMONO%%",logo_mono).replace("%%ICON%%",icon).replace("%%MARK%%",mk(cols))
open("index.html","w",encoding="utf-8").write(out)
