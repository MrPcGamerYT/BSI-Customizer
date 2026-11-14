
### Interaction polish

The native control center keeps its bento/inspector layout and near-black palette.
Shared buttons, navigation groups, pack cards, color fields and switches use short
accent-tinted hover/focus transitions; menus, settings, the design drawer and lazy
pages reveal without blocking navigation. The thin header strip tracks real backend
work/page loading, not a simulated delay. Boot content/behavior is unchanged.

Appearance (also the app menu) exposes **Animated background** and **Reduce motion**.
Both are saved with the other appearance preferences. Reduced motion overrides
ambient animation and interface transition durations, while keeping busy status
visible as a static line. Background movement uses two cached radial textures and
12 small scene-graph points, without per-frame Canvas repaint, blur or resource
scans; it stops while the window is inactive or minimized. These customizer effects
do not add animation support to deployed emulator skin packs.
