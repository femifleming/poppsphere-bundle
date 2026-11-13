#!/usr/bin/env python3
# Builds manual.html (same look as the plug-in: red field, grey bars, colour grids, Verdana bold lowercase)
# from figs.json (QuellTest --figs) and the screenshots in img/, then prints manual.pdf with Chrome.
import json, math, os, random, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
D = json.load(open(os.path.join(HERE, 'figs.json')))
RED, BLUE, ORANGE, YELLOW, GREY, INK, PINK, LILAC, GREEN, NAVY = '#c8251f', '#1fb1e6', '#f7931e', '#f2c400', '#b7b8ba', '#1a1a1a', '#e58fbf', '#7b6fb8', '#1f8a3c', '#1d2f6f'
PAL = ['#d22a26', '#f2c400', '#1f8a3c', '#e58fbf', '#2e7dc4', '#f7931e', '#6f5fb0', '#222', '#2a7fd0', '#a88a1e']
out = []
add = out.append

# ---------------------------------------------------------------------------------------------- svg
def grid_svg(kind, seed, cols=8, rows=3, cell=13):
    rnd = random.Random(seed)
    s = [f'<svg class="grid" width="{cols*cell}" height="{rows*cell}" viewBox="0 0 {cols*cell} {rows*cell}">']
    for i in range(cols * rows):
        x, y = (i % cols) * cell, (i // cols) * cell
        a, b = rnd.choice(PAL), rnd.choice(PAL)
        s.append(f'<rect x="{x+1}" y="{y+1}" width="{cell-2}" height="{cell-2}" fill="{a}"/><rect x="{x+3}" y="{y+3}" width="{cell-6}" height="{cell-6}" fill="#fff"/>')
        k = 3.6
        if kind == 0: s.append(f'<rect x="{x+k+.5}" y="{y+k+.5}" width="{cell-2*k-1}" height="{cell-2*k-1}" fill="{b}"/>')
        elif kind == 1: s.append(f'<line x1="{x+k}" y1="{y+cell-k}" x2="{x+cell-k}" y2="{y+k}" stroke="{b}" stroke-width="2"/>')
        elif kind == 2: s.append(f'<polygon points="{x+cell/2},{y+k} {x+cell-k},{y+cell/2} {x+cell/2},{y+cell-k} {x+k},{y+cell/2}" fill="{b}"/>')
        else: s.append(f'<path d="M{x+k} {y+k}L{x+cell-k} {y+cell-k}M{x+k} {y+cell-k}L{x+cell-k} {y+k}" stroke="{b}" stroke-width="1.8"/>')
    s.append('</svg>')
    return ''.join(s)

W, H, L, B, T, R = 520, 170, 36, 22, 10, 10   # chart size and margins

def fx(i, n): return L + (W - L - R) * i / (n - 1)
def freq_axis(s):
    for f, t in [(20, '20'), (50, '50'), (100, '100'), (200, '200'), (500, '500'), (1000, '1k'), (2000, '2k'), (5000, '5k'), (10000, '10k'), (20000, '20k')]:
        x = L + (W - L - R) * math.log(f / 20) / math.log(1000)
        s.append(f'<line x1="{x:.1f}" y1="{T}" x2="{x:.1f}" y2="{H-B}" stroke="{GREY}" stroke-width="1.6" stroke-dasharray="1.6 5"/>')
        s.append(f'<text x="{x:.1f}" y="{H-B+13}" text-anchor="middle" class="ax">{t}</text>')

def red_chart(series, top=24, blocks=None, note=''):
    """series: list of (values (512 pts, dB reduction), colour, label). blocks = index of the series drawn as orange blocks."""
    global s
    s = [f'<svg class="chart" viewBox="0 0 {W} {H}">', f'<rect x="{L}" y="{T}" width="{W-L-R}" height="{H-T-B}" fill="#fff"/>']
    freq_axis(s)
    for db in range(6, top, 6):
        y = T + (H - T - B) * db / top
        s.append(f'<text x="{L-5}" y="{y+3:.1f}" text-anchor="end" class="ax">-{db}</text>')
    def ry(v): return T + (H - T - B) * min(1, v / top)
    if blocks is not None:
        vals = series[blocks][0]; n = len(vals); step = 6
        for i in range(0, n, step):
            v = max(vals[i:i+step])
            if v < 0.25: continue
            v = math.ceil(v / 1.5) * 1.5
            x0, x1 = fx(i, n), fx(min(n - 1, i + step), n)
            s.append(f'<rect x="{x0:.1f}" y="{T}" width="{x1-x0+.4:.1f}" height="{ry(v)-T:.1f}" fill="url(#og)"/>')
    for k, (vals, col, lab) in enumerate(series):
        if k == blocks: continue
        n = len(vals)
        pts = ' '.join(f'{fx(i, n):.1f},{ry(v):.1f}' for i, v in enumerate(vals))
        s.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="1.6"/>')
    s.append(f'<rect x="{L}" y="{T}" width="{W-L-R}" height="2" fill="{INK}"/>')
    s.append('<defs><linearGradient id="og" x1="0" x2="1"><stop offset="0" stop-color="#f7931e"/><stop offset="1" stop-color="#fde3c2"/></linearGradient></defs>')
    leg = ''.join(f'<span class="key"><i style="background:{c}"></i>{l}</span>' for _, c, l in series)
    s.append('</svg>')
    return ''.join(s) + f'<div class="legend">{leg}{note}</div>'

def time_chart(series, step, top=16):
    s = [f'<svg class="chart" viewBox="0 0 {W} {H}">', f'<rect x="{L}" y="{T}" width="{W-L-R}" height="{H-T-B}" fill="#fff"/>']
    n = len(series[0][0]); dur = n * step
    def tx(t): return L + (W - L - R) * t / dur
    for t in [0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.2, 1.4]:
        s.append(f'<line x1="{tx(t):.1f}" y1="{T}" x2="{tx(t):.1f}" y2="{H-B}" stroke="{GREY}" stroke-width="1.6" stroke-dasharray="1.6 5"/>')
        lab = f'{t:.1f}'.replace('.', ',')
        s.append(f'<text x="{tx(t):.1f}" y="{H-B+13}" text-anchor="middle" class="ax">{lab} s</text>')
    s.append(f'<rect x="{tx(0.3):.1f}" y="{H-B-8}" width="{tx(1.0)-tx(0.3):.1f}" height="8" fill="{YELLOW}"/>')
    s.append(f'<text x="{(tx(0.3)+tx(1.0))/2:.1f}" y="{H-B-11}" text-anchor="middle" class="ax">3 khz tone on</text>')
    for db in range(4, top, 4):
        y = T + (H - T - B) * db / top
        s.append(f'<text x="{L-5}" y="{y+3:.1f}" text-anchor="end" class="ax">-{db}</text>')
    for vals, col, lab in series:
        pts = ' '.join(f'{tx(i*step):.1f},{T + (H-T-B) * min(1, v / top):.1f}' for i, v in enumerate(vals))
        s.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="1.8"/>')
    s.append(f'<rect x="{L}" y="{T}" width="{W-L-R}" height="2" fill="{INK}"/></svg>')
    leg = ''.join(f'<span class="key"><i style="background:{c}"></i>{l}</span>' for _, c, l in series)
    return ''.join(s) + f'<div class="legend">{leg}</div>'

def shape_chart(vals, name):
    w, h = 150, 74
    s = [f'<svg class="mini" viewBox="0 0 {w} {h}"><rect width="{w}" height="{h}" fill="#fff"/>']
    mid = h * 0.45
    s.append(f'<line x1="0" y1="{mid}" x2="{w}" y2="{mid}" stroke="{GREY}" stroke-dasharray="4 6"/>')
    x1k = w * math.log(1000 / 20) / math.log(1000)
    s.append(f'<line x1="{x1k}" y1="0" x2="{x1k}" y2="{h}" stroke="{GREY}" stroke-width="1.4" stroke-dasharray="1.4 4"/>')
    n = len(vals)
    for i in range(0, n, 3):
        y = max(2, min(h - 2, mid - vals[i] * (h * 0.4 / 12)))
        s.append(f'<rect x="{w*i/(n-1)-1.6:.1f}" y="{y-1.6:.1f}" width="3.2" height="3.2" fill="{RED}"/>')
    s.append('</svg>')
    return f'<figure class="shape">{"".join(s)}<figcaption>{name}</figcaption></figure>'

def tilt_chart():
    s = [f'<svg class="chart" viewBox="0 0 {W} {H}">', f'<rect x="{L}" y="{T}" width="{W-L-R}" height="{H-T-B}" fill="#fff"/>']
    freq_axis(s)
    for vals, col in [(D['tiltLow'], RED), (D['tiltHigh'], NAVY)]:
        n = len(vals)
        pts = ' '.join(f'{fx(i, n):.1f},{H-B - (H-T-B-6)*v:.1f}' for i, v in enumerate(vals))
        s.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="2"/>')
    s.append(f'<text x="{L-5}" y="{T+8}" text-anchor="end" class="ax">100%</text><text x="{L-5}" y="{H-B}" text-anchor="end" class="ax">0</text></svg>')
    return ''.join(s) + f'<div class="legend"><span class="key"><i style="background:{RED}"></i>low tilt reach</span><span class="key"><i style="background:{NAVY}"></i>high tilt reach</span></div>'

def flow_svg():
    boxes = [('input', GREY), ('fft analysis', YELLOW), ('envelope + excess', ORANGE), ('soft / hard', ORANGE), ('detail smoothing', ORANGE),
             ('attack / release', ORANGE), ('x depth curve', PINK), ('max cut, mix, trim', LILAC), ('min / linear filter', NAVY), ('output', GREY)]
    w, h = 520, 132
    s = [f'<svg class="chart" viewBox="0 0 {w} {h}">']
    bw, bh, gap = 92, 26, 10
    for i, (t, c) in enumerate(boxes):
        row, col = divmod(i, 5)
        if row: col = 4 - col
        x, y = 8 + col * (bw + gap), 14 + row * 66
        fg = '#fff' if c in (NAVY, LILAC) else INK
        s.append(f'<rect x="{x}" y="{y}" width="{bw}" height="{bh}" fill="{c}"/><text x="{x+bw/2}" y="{y+17}" text-anchor="middle" class="fl" fill="{fg}">{t}</text>')
        if i < len(boxes) - 1:
            if i == 4: s.append(f'<path d="M{x+bw/2} {y+bh+3} L{x+bw/2} {y+60}" stroke="{RED}" stroke-width="2"/><polygon points="{x+bw/2-5},{y+56} {x+bw/2+5},{y+56} {x+bw/2},{y+63}" fill="{RED}"/>')
            elif row == 0: s.append(f'<polygon points="{x+bw+1},{y+8} {x+bw+1},{y+18} {x+bw+gap-1},{y+13}" fill="{RED}"/>')
            else: s.append(f'<polygon points="{x-1},{y+8} {x-1},{y+18} {x-gap+1},{y+13}" fill="{RED}"/>')
    s.append(f'<text x="8" y="{h-4}" class="ax">sidechain (sc) replaces the input in the analysis only; the filter is always applied to the input.</text></svg>')
    return ''.join(s)

# ---------------------------------------------------------------------------------------------- text helpers
def h1(t, sub=''):
    add(f'<h1><span>{t}</span>{f"<em>{sub}</em>" if sub else ""}</h1>')
def h2(t): add(f'<h2><b></b>{t}</h2>')
def p(t): add(f'<p>{t}</p>')
def table(rows, head=None):
    hd = f'<tr>{"".join(f"<th>{c}</th>" for c in head)}</tr>' if head else ''
    add('<table>' + hd + ''.join('<tr>' + ''.join(f'<td>{c}</td>' for c in r) + '</tr>' for r in rows) + '</table>')
def shot(name, cap): add(f'<figure class="shot"><img src="img/{name}.png"><figcaption>{cap}</figcaption></figure>')
def fig(svg, cap): add(f'<figure class="fig">{svg}<figcaption>{cap}</figcaption></figure>')
def note(t): add(f'<div class="note"><span class="tri"></span>{t}</div>')
def page(): add('<div class="pb"></div>')

M, T_ = D['modes'], D['time']
step = T_['step']

# ---------------------------------------------------------------------------------------------- cover
add('<section class="cover">')
add('<div class="grids">' + ''.join(grid_svg(k, 3 + k * 7, rows=6) for k in range(4)) + '</div>')
add('<div class="pill">manual is loading...</div>')
add('<div class="word"><span class="a">que</span><span class="b">ll</span></div>')
add('<div class="sub">a dynamic resonance suppressor<br>vst3 &middot; vst &middot; au &middot; lv2 &middot; standalone &middot; macos &middot; windows &middot; linux</div>')
add('<img class="coverimg" src="img/main.png">')
add('<div class="ver">version 1.0.1 &middot; 2026</div>')
add('</section>')

# ---------------------------------------------------------------------------------------------- body
h1('what it does', 'start here')
p('quell listens for <b>resonances</b>: narrow peaks in the spectrum that ring out, poke through or sound harsh. it pushes them down <b>only while they are there</b>, and only by as much as they stick out. when the peak goes away, so does the cut. you get a smoother, less tiring sound without having to set up dozens of eq bands that would be wrong half the time.')
p('think of it as hundreds of tiny compressors, one for each sliver of the spectrum, each watching its own neighbourhood. the orange blocks in the window show what is being taken away, right now.')
note('the quickest start: put quell on a vocal or a bright instrument, leave everything at default, and raise <b>depth</b> until the harsh moments calm down. then switch on <b>delta</b> to hear only what is removed: it should sound like the harshness and nothing else.')

h2('installing')
table([
 ('macos', 'Quell.vst3 &rarr; ~/Library/Audio/Plug-Ins/VST3 &middot; Quell.component &rarr; ~/Library/Audio/Plug-Ins/Components &middot; Quell.vst &rarr; ~/Library/Audio/Plug-Ins/VST &middot; Quell.app &rarr; /Applications (or brew install --cask femifleming/tap/quell)'),
 ('windows', 'Quell.vst3 &rarr; C:\\Program Files\\Common Files\\VST3 &middot; Quell.dll (vst 2) &rarr; your vst folder &middot; Quell.exe runs on its own'),
 ('linux', 'Quell.vst3 &rarr; ~/.vst3 &middot; Quell.lv2 &rarr; ~/.lv2 &middot; Quell.so (vst 2) &rarr; ~/.vst &middot; Quell runs on its own (alsa or jack)'),
], head=['system', 'where it goes'])
p('everything comes in the <b>poppsphere bundle</b> (github.com/femifleming/poppsphere-bundle, releases), one zip per system. the standalone app plays your audio input through quell. quell runs on mono and stereo tracks and has an optional stereo <b>sidechain</b> input. on macos it is universal (apple silicon and intel).')

page()
h1('the window', 'a guided tour')
shot('bands', 'the window with three bands. the grids on top hold presets, your stored configurations, band shapes and remove buttons. the white play area is the graph. underneath: the main controls, the band rings and the selected band.')
p('every control explains itself: <b>hover</b> over anything and its description appears in the blue status pill under the grids. click <b>help</b> (bottom left, under the name) for the guided tour layer below. the background turns blue while it is on, just like the help screen of the old software this look is borrowed from. click <b>end help</b> to go back.')
shot('help', 'help mode: grey labels point at every area.')

h2('handling the controls')
table([
 ('grey bars with orange checks', 'drag left / right (or up / down). <b>shift</b> = fine. <b>alt-click</b> = back to default. <b>double-click</b> = type a value. the mouse wheel works too.'),
 ('black-to-white strips with a red triangle', 'the same, in the side panel. the triangle shows the value.'),
 ('timeline bars (mix, out)', 'drag the gradient tab.'),
 ('orange checker blocks', 'switches and choices. lit (checkered) = on / chosen; grey = off.'),
 ('squares with a dot', 'bypass (black dot), delta (red dot), band on.'),
 ('gradient squares with triangles', 'quality, linear phase, low latency, window size. a solid black square means chosen.'),
 ('grids of coloured squares', 'each bright cell does something (load a preset, store, pick a shape, remove a band). pale cells are empty.'),
], head=['what it looks like', 'how to use it'])

h1('soft / hard', 'the gold bar')
p('click the gold bar to switch between the two ways quell finds resonances.')
table([
 ('<b>soft</b> (adaptive)', 'compares each sliver of the spectrum to its surroundings. only what sticks out <i>relative to its neighbours</i> is reduced, so it does the same thing whether the input is loud or quiet. low distortion, keeps transients. the right choice for vocals, acoustic instruments, anything with a wide dynamic range.'),
 ('<b>hard</b> (level)', 'also looks at how <i>loud</i> a peak is in absolute terms: louder input means more reduction, and depth sets the threshold. firmer, more like a compressor; good after gain staging. push depth high for obvious, creative effects.'),
])
fig(red_chart([(M['soft_loud']['red'], ORANGE, 'soft, loud input'), (M['soft_quiet']['red'], RED, 'soft, input 12 db quieter'),
               (M['hard_loud']['red'], NAVY, 'hard, loud input'), (M['hard_quiet']['red'], BLUE, 'hard, input 12 db quieter')], blocks=0),
    'reduction across the spectrum for noise with tones at 440 hz, 1,25 khz, 3 khz and 5,1 khz (depth 8). soft: the curves for loud and quiet input are the same. hard: the quieter input gets less reduction.')

h1('depth, detail, attack, release', 'the bottom rail')
h2('depth')
p('how much is taken away. at low values quell only smooths; higher values cut deeper. the number is not in db: the actual cut depends on how far each resonance sticks out and on the depth curve. read the real amount off the graph. the range goes past what you usually need (0 to 20) so that the depth curve has room to boost.')
fig(red_chart([(D['depth']['d14']['red'], LILAC, 'depth 14'), (D['depth']['d8']['red'], ORANGE, 'depth 8'), (D['depth']['d3']['red'], RED, 'depth 3')], blocks=1),
    'the same signal at depth 3, 8 and 14.')
h2('detail')
p('the width of the cuts. <b>low detail</b> makes broad, gentle dips that catch wide build-ups (a boxy room, a muddy low-mid). <b>high detail</b> makes narrow, deep notches for sharp ringing (cymbals, feedback-like tones, drum overheads). in soft mode very high detail also slows the detection slightly so that steep notches stay clean.')
fig(red_chart([(D['detail']['d1']['red'], NAVY, 'detail 1'), (D['detail']['d5']['red'], ORANGE, 'detail 5'), (D['detail']['d9']['red'], RED, 'detail 9')], blocks=1),
    'detail 1, 5 and 9 at depth 8: low detail spreads each cut out, high detail keeps it on the peak.')
h2('attack and release')
p('<b>attack</b>: how quickly a cut appears when a resonance starts. higher = slower, which lets more of the initial transient through and keeps the sound forward. lower = faster, which tucks the source back. <b>release</b>: how quickly the cut goes away again. fast release follows the material closely but can sound breathy; slow release is steadier. both are frequency-dependent: high frequencies react faster than low ones, and the tilt controls in the side panel change that balance.')
fig(time_chart([(T_['a1'], RED, 'attack 1'), (T_['a5'], ORANGE, 'attack 5'), (T_['a9'], NAVY, 'attack 9')], step),
    'reduction at 3 khz when a tone starts at 0,3 s and stops at 1,0 s (release 5). the higher the attack, the slower the cut arrives.')
fig(time_chart([(T_['r1'], RED, 'release 1'), (T_['r5'], ORANGE, 'release 5'), (T_['r9'], NAVY, 'release 9')], step),
    'the same, attack 2, with release 1, 5 and 9: how long the cut hangs on after the tone stops.')

h1('the graph', 'the white play area')
p('the graph runs from 20 hz on the left to 20 khz on the right.')
table([
 ('orange blocks', 'what is being removed now, hanging down from the black line at the top. the scale on the left is in db (scroll on empty space to change the range from 6 to 48 db). in stereo, yellow blocks just behind are the second channel (r or s).'),
 ('grey steps', 'the input spectrum, faintly, to help you find your way.'),
 ('red dots', 'the <b>depth curve</b>: where quell works harder (above the dashed middle line, toward "more") or softer (below it, toward "less"). with no bands it is flat: everything is treated the same.'),
 ('grey dots', 'the depth curve of the second channel, shown when band focus makes the two channels differ.'),
 ('grey area', 'max cut: the reduction will not go below this line.'),
 ('pink area', 'the region of the band you are listening to.'),
], head=['you see', 'it means'])

h2('bands')
p('up to <b>eight bands</b> shape the depth curve. boosting a band means "suppress more here"; cutting means "leave this alone more". cuts (low cut, high cut, band reject) stop processing outside or inside their range; band passes process <i>only</i> their regions, and several band passes work side by side.')
add('<div class="shapes">' + ''.join(shape_chart(D['shapes'][str(i)], n) for i, n in enumerate(['low cut', 'low shelf', 'high shelf', 'high cut', 'bell', 'band pass', 'band reject', 'tilt'])) + '</div>')
add('<p class="cap">the eight shapes at 1 khz (shelves, bell, band pass and tilt with +9 db). red dots = depth curve, dashed line = neutral.</p>')
table([
 ('double-click empty space', 'adds a bell there (near 20 hz a low cut, near 20 khz a high cut, with <b>cmd</b> a band pass). if you picked a shape in the shape grid first, you get that shape.'),
 ('right-click empty space', 'choose the shape from a menu, then it is added where you clicked.'),
 ('drag a node', 'frequency left / right, depth up / down. <b>shift</b> = fine, <b>cmd</b> = q or slope, <b>alt</b> = listen while dragging.'),
 ('scroll on a node', 'q or slope. <b>cmd</b>-scroll: the band\'s stereo focus.'),
 ('double-click a node', 'switch the band on / off. a switched-off band (hollow node) no longer changes the curve, but its area is still processed.'),
 ('click / shift-click / drag a box', 'select one, add to the selection, select everything in the box. dragged together, selected nodes move together.'),
 ('right-click a node', 'shape, on, listen, reset focus, remove.'),
 ('delete or backspace', 'removes the selected bands.'),
], head=['do this', 'to'])
h2('the band rings and the band rail')
p('the eight rings under the graph are the band slots: a coloured tab means the slot is in use, the orange ring is the selected band, a white centre means it is on. click a ring to select its band, double-click to switch it on / off. the orange checker block on the left shows the selected band\'s shape (click for a menu); next to the rings are <b>on</b> and <b>listen</b>. listen plays only what this band\'s region removes (its delta). the rail below holds the band\'s <b>freq</b> (type <i>6k3</i>, <i>6.3k</i> or <i>6300</i>), <b>depth</b>, <b>q</b> or <b>slope</b> and <b>focus</b>.')

h1('side panel', 'stereo, tilt, scale')
table([
 ('<b>l/r</b> or <b>m/s</b>', 'process left and right, or mid and side. every stereo control below follows this choice.'),
 ('<b>link</b>', '0%: each channel is analysed and processed alone (dual mono). 100%: both are analysed together and get the same cuts. in between: partly linked. for hard-panned material try l/r with low link; for full mixes, m/s with link at 100%. lowering link in m/s mode can narrow the stereo image.'),
 ('<b>focus</b>', 'shifts the work toward l / m (left) or r / s (right).'),
 ('<b>detail low / high</b>', 'scales detail below ~500 hz and above ~2 khz.'),
 ('<b>attack low / high</b>', 'scales attack in those two regions. e.g. raise attack low to let kick and bass transients through.'),
 ('<b>release low / high</b>', 'scales release in those two regions.'),
 ('<b>max cut</b>', 'the most quell will ever take away (grey area on the graph). lets you push depth hard without notches getting too deep. "off" at 40 db.'),
 ('<b>wet trim</b>', 'level of the processed signal before mix, so that mix percentages stay honest.'),
])
fig(tilt_chart(), 'how far the tilt controls reach: low tilt is strongest below ~300 hz and fades out by ~1 khz; high tilt starts around 1 khz and is full above ~4 khz.')

h1('footer and corners')
table([
 ('<b>bypass</b> (square with black dot, right rail)', 'compare with the original. quell keeps running in the background, so switching is instant (and costs the same cpu).'),
 ('<b>delta</b> (square with red dot)', 'hear only what is being removed. useful for setting up; can also be left on as an effect.'),
 ('<b>mix</b>', 'wet / dry. 100% = only processed. the graph shows the reduction with mix applied.'),
 ('<b>out</b>', 'output level, after mix.'),
 ('<b>normal / high / ultra</b>', 'time resolution: how often quell re-analyses (every 256 / 128 / 64 samples at 48 khz). higher reacts more precisely to fast material and uses more cpu.'),
 ('<b>linear</b>', 'linear-phase filtering instead of minimum phase. no phase shift, so parallel mixing and unlinked m/s stay clean; costs 50% more latency.'),
 ('<b>0 lat.</b>', 'low-latency mode: zero latency, minimum phase, no lookahead, so cuts arrive a little later. use it while tracking or playing live.'),
 ('<b>size</b>', 'cycles the window through s, m, l and xl. you can also drag the bottom-right corner.'),
 ('crescent + number', 'the current latency in samples, reported to your host.'),
 ('lilac bars on the left', 'how much is being removed right now (largest cut, per channel, 0 to 24 db).'),
], head=['control', 'what it does'])
h2('latency')
table([('minimum phase (default)', '2048', '4096', '8192'), ('linear phase', '3072', '6144', '12288'), ('low latency (0 lat.)', '0', '0', '0')],
      head=['mode', '44,1 / 48 khz', '88,2 / 96 khz', '176,4 / 192 khz'])
p('the host compensates for this automatically. offline bounces use ultra quality automatically (switch this off in <b>settings</b>).')

h1('top row')
add('<div class="minigrids">' + ''.join(f'<div>{grid_svg(k, 3 + k * 7, rows=2)}<span>{t}</span></div>' for k, t in enumerate(['presets', 'my presets', 'shapes', 'remove'])) + '</div>')
table([
 ('<b>presets</b> (squares)', 'the factory presets. bright cells are presets; hover to read the name, click to load. the loaded one has a black frame.'),
 ('<b>my presets</b> (slashes)', 'your stored configurations. the last bright cell is "... and store configurations here": click it, give a name. files live in the quell/presets folder of your user application data: ~/Library/Application Support on macos, %appdata% on windows, ~/.config on linux (settings &rarr; show preset folder).'),
 ('<b>shapes</b> (diamonds)', 'the eight band shapes. with bands selected, click a shape to change them. with nothing selected, it picks the shape for the next band you add.'),
 ('<b>remove</b> (crosses)', 'one cell per band slot; bright = in use. click to remove that band.'),
 ('<b>status pill</b>', 'shows what the control under the mouse does, or which preset is loaded.'),
 ('<b>undo / redo</b>', 'also cmd-z and shift-cmd-z (ctrl on windows and linux). a and b keep separate histories.'),
 ('<b>a / b / a &gt; b</b>', 'two complete settings to compare. a &gt; b copies the one you are on to the other.'),
 ('<b>sc</b> and <b>listen</b>', 'sc: the cuts follow the sidechain input instead of the track (route a signal to quell\'s sidechain in your host). listen: hear the sidechain. "(no input)" means nothing is routed to it.'),
 ('<b>settings</b>', 'render on ultra when bouncing, frequency readout near the cursor, window size, reset to default, show preset folder.'),
])

page()
h1('starting points', 'the factory presets')
table([
 ('default', 'neutral: depth 5, detail 5, attack 4, release 5.'),
 ('gentle smoothing', 'low depth and slower times. a light polish for anything.'),
 ('vocal harshness', 'low cut at 900 hz, a bell boost around 3,5 khz, a little less above 9 khz: aims at the harsh upper mids of a voice.'),
 ('de-ess', 'a band pass around 7 khz, fast attack and release. only the sibilance region is processed.'),
 ('boxy low mids', 'processing only between 120 hz and 1,2 khz, broad (low detail) and slow.'),
 ('drum overheads', 'high detail for ringing cymbals and shells, m/s, low cut at 400 hz.'),
 ('acoustic guitar', 'moderate, with extra attention around 2,5 khz.'),
 ('bus glue', 'low depth, 70% mix, m/s. evens out a group or a mix.'),
 ('sound design (hard)', 'hard mode with very high depth and detail: strips the tonal parts and leaves noise and grit.'),
 ('sidechain ducking', 'hard mode keyed by the sidechain: the track makes room for the sidechain signal where they overlap in frequency.'),
], head=['preset', 'what it is for'])

h2('tips')
note('<b>listen to delta first.</b> if delta sounds like the instrument itself rather than its harsh parts, depth is too high or detail too low.')
note('<b>shape before depth.</b> use a low cut to keep quell away from the body of a sound, then boost the depth curve where the trouble is.')
note('<b>m/s on mixes.</b> cuts on the mid and the side are independent with link below 100%, which can tame harsh reverb tails without touching the centre.')
note('<b>sidechain for space.</b> put quell on a pad with the vocal on the sidechain in hard mode: the pad gives way only at the vocal\'s own frequencies.')

h1('how it works', 'for the curious')
fig(flow_svg(), 'the signal path, every analysis step (64 to 256 samples).')
p('quell takes the last 2048 samples (at 44,1 / 48 khz), weights them with a hann window and measures them with a zero-padded 4096-point fft. for each frequency it computes the level in db, blends it with the other channel as far as <b>link</b> asks, and smooths it across neighbouring frequencies on a log scale to get the local envelope (detail sets the width). the <b>excess</b> above that envelope is a resonance. soft turns the excess into reduction with a soft knee and depth as the slope; hard compares the level with a threshold that falls as depth rises, weighted by how peaked the spot is.')
p('the reduction is smoothed across frequency (detail again), then in time with separate attack and release per frequency, multiplied by the depth curve, limited by max cut and turned into a gain curve with mix and wet trim folded in. from that curve quell builds a filter: <b>minimum phase</b> (from the cepstrum) or <b>linear phase</b>, with its impulse response trimmed so that overlapping frames add up to an exact convolution. at depth 0 the output is a bit-exact delayed copy of the input. in <b>low latency</b> mode the same minimum-phase filter is shortened to 256 taps and run directly on the signal, so nothing waits.')

h2('specifications')
table([
 ('channels', 'mono or stereo, plus mono or stereo sidechain'),
 ('sample rates', 'any; analysis size 2048 / 4096 / 8192 up to 50 / 100 / 192 khz'),
 ('bands', '8 (low cut, low shelf, high shelf, high cut, bell, band pass, band reject, tilt)'),
 ('cpu (m2 max, 48 khz stereo)', 'about 4% of a core in normal, 8 to 10% in high, 16 to 24% in ultra'),
 ('formats', 'vst3, vst 2, standalone everywhere; audio unit on macos (universal), lv2 on linux'),
])

body = '\n'.join(out)
css = f'''
@page {{ size: A4; margin: 16mm 15mm 16mm 15mm; }}
@page :first {{ margin: 0; }}
@font-face {{ font-family: V; src: local("Verdana Bold"); font-weight: bold; }}
body {{ font-family: Verdana, sans-serif; font-size: 9.2pt; line-height: 1.5; color: {INK}; margin: 0; }}
b {{ font-weight: bold; }}
p {{ margin: 0 0 8pt; }}
.cover {{ background: {RED}; margin: 0; height: 297mm; overflow: hidden; box-sizing: border-box; padding: 14mm 15mm; position: relative; page-break-after: always; color: #fff; }}
.cover .grids {{ display: flex; gap: 8px; }}
.cover .grids svg {{ background: #fff; padding: 4px; }}
.cover .pill {{ margin-top: 12px; width: 60%; background: repeating-linear-gradient(90deg, {NAVY} 0 2px, #2a7fd0 2px 4px); border-radius: 12px; padding: 3px 14px; font-weight: bold; font-size: 9pt; }}
.cover .word {{ font-family: "Times New Roman", serif; font-size: 96pt; margin-top: 40px; line-height: 1; }}
.cover .word .a {{ color: {INK}; }} .cover .word .b {{ color: #fff; }}
.cover .sub {{ font-weight: bold; font-size: 12pt; margin-top: 8px; }}
.cover .coverimg {{ width: 100%; margin-top: 30px; border: 6px solid {GREY}; box-sizing: border-box; }}
.cover .ver {{ position: absolute; bottom: 14mm; left: 15mm; font-weight: bold; font-size: 9pt; background: {GREY}; color: {INK}; padding: 3px 10px; }}
h1 {{ font-size: 13pt; margin: 18pt 0 10pt; display: flex; align-items: stretch; gap: 0; break-after: avoid; }}
h1 span {{ background: {RED}; color: #fff; padding: 4px 12px; font-weight: bold; }}
h1 em {{ font-style: normal; background: {GREY}; color: {INK}; font-size: 9pt; padding: 7px 12px; flex: 1; text-align: right; }}
h2 {{ font-size: 10.5pt; margin: 14pt 0 6pt; display: flex; align-items: center; gap: 8px; break-after: avoid; }}
h2 b {{ display: inline-block; width: 12px; height: 12px; background: {ORANGE}; box-shadow: inset 0 0 0 3px {ORANGE}, inset 0 0 0 6px #fff; background: {YELLOW}; }}
table {{ width: 100%; border-collapse: collapse; margin: 4pt 0 10pt; break-inside: auto; }}
tr {{ break-inside: avoid; }}
th {{ background: {GREY}; text-align: left; padding: 4px 8px; font-size: 8.5pt; }}
td {{ padding: 5px 8px; vertical-align: top; border-bottom: 2px dotted {ORANGE}; }}
td:first-child {{ width: 32%; }}
figure {{ margin: 6pt 0 12pt; break-inside: avoid; }}
figcaption, .cap {{ font-size: 8pt; color: #555; margin-top: 4px; }}
.shot img {{ width: 100%; border: 5px solid {GREY}; box-sizing: border-box; }}
.chart {{ width: 100%; background: {GREY}; padding: 6px; box-sizing: border-box; }}
.chart .ax {{ font-family: Verdana; font-weight: bold; font-size: 8px; fill: #6d6e70; }}
.chart .fl {{ font-family: Verdana; font-weight: bold; font-size: 8.5px; }}
.legend {{ font-size: 8pt; font-weight: bold; margin-top: 4px; }}
.key {{ margin-right: 14px; white-space: nowrap; }}
.key i {{ display: inline-block; width: 10px; height: 10px; margin-right: 5px; vertical-align: -1px; }}
.shapes {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; background: {GREY}; padding: 8px; break-inside: avoid; }}
.shape {{ margin: 0; }} .shape svg {{ width: 100%; display: block; }} .shape figcaption {{ font-weight: bold; color: {INK}; }}
.minigrids {{ display: flex; gap: 14px; margin: 4pt 0 8pt; }}
.minigrids div {{ display: flex; flex-direction: column; font-size: 8pt; font-weight: bold; }}
.note {{ background: #fff3e2; border-left: 8px solid {ORANGE}; padding: 6px 10px 6px 26px; margin: 6pt 0 8pt; position: relative; break-inside: avoid; }}
.note .tri {{ position: absolute; left: 8px; top: 9px; width: 0; height: 0; border-left: 10px solid {RED}; border-top: 6px solid transparent; border-bottom: 6px solid transparent; }}
.pb {{ break-after: page; }}
'''
html = f'<!doctype html><html><head><meta charset="utf-8"><title>quell manual</title><style>{css}</style></head><body>{body}</body></html>'
open(os.path.join(HERE, 'manual.html'), 'w').write(html)
subprocess.run(['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                '--virtual-time-budget=5000', f'--print-to-pdf={HERE}/manual.pdf', 'file://' + os.path.join(HERE, 'manual.html')], stderr=subprocess.DEVNULL)
print('wrote', os.path.join(HERE, 'manual.pdf'))
