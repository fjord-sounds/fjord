// Which README screenshots are made, and from which preset/page.
//
// page:   play | synth | osc | mod | fx | comp | arp | rhythm | browser   (see FjordRenderTest --screenshot)
// preset: factory preset name
// fx:     selected effect on the FX page (0 Drive, 1 Crush, 2 EQ, 3 Comp, 4 Chorus, 5 Phaser,
//         6 Flanger, 7 Delay, 8 Reverb, 9 Width)
// notes:  MIDI notes held on the on-screen keyboard (also feeds the oscilloscope)

export const RAW_SHOTS = [
  { id: "synth",   page: "synth",   preset: "Aurora Engine",          notes: [48, 55, 60, 64] },
  { id: "play",    page: "play",    preset: "Velvet Galaxy",          notes: [45, 52, 57, 60, 64] },
  { id: "osc",     page: "osc",     preset: "Eight Drawbar Dream",    notes: [48, 55, 60, 64] },
  { id: "comp",    page: "comp",    preset: "Stadium Pluck",          notes: [48, 55, 60, 64] },
  { id: "mod",     page: "mod",     preset: "Polychrome",             notes: [50, 57, 62, 65] },
  { id: "fx",      page: "fx",      preset: "Midnight Choir Machine", notes: [43, 50, 55, 59], fx: 2 },
  { id: "arp",     page: "arp",     preset: "Heart On Sleeve",        notes: [45, 48, 52, 57] },
  { id: "rhythm",  page: "rhythm",  preset: "Neon Grid",              notes: [48, 52, 55, 59] },
  { id: "browser", page: "browser", preset: "Aurora Engine",          notes: [] },
];

// Composed images in docs/screenshots/ – every one in a light and a dark version
// (the README picks the one that matches the reader's GitHub theme).
export const COMPOSITIONS = [
  { id: "hero",    layout: "hero",   front: "synth", back: "fx" },
  { id: "themes",  layout: "split",  shot: "synth", themed: false },
  { id: "synth",   layout: "window", shot: "synth" },
  { id: "play",    layout: "window", shot: "play" },
  { id: "osc",     layout: "window", shot: "osc" },
  { id: "mod",     layout: "window", shot: "mod" },
  { id: "fx",      layout: "window", shot: "fx" },
  { id: "comp",    layout: "window", shot: "comp" },
  { id: "arp",     layout: "window", shot: "arp" },
  { id: "rhythm",  layout: "window", shot: "rhythm" },
  { id: "browser", layout: "window", shot: "browser" },
];
