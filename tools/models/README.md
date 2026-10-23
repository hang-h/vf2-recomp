# VF1 geometry on VF2 part transforms (experimental)

When `vf.zip` is beside `vf2.zip`, this branch prepares VF1 models during ROM loading
and lets each player select them with Up on Akira, Pai, Lau, Wolf, Jeffry, Kage, Sarah or Jacky. It replaces that player's
matching objects in `geo_object_data`. A mesh takes
the original VF2 object's current matrix, including smooth-motion replay. It enters
the existing VF2 projection, depth-sort, clipping and CPU/Metal drawing paths. Game
code, attacks, collision and animation remain controlled by VF2.

**Current status:** All eight regular VF1 fighters have heads, torsos, pelvises,
arms, legs, feet and original select icons. The VF2 skeleton drives their movement;
meshes are fitted to each fighter's measured VF2 joint lengths. Both VF2 costumes
use the corresponding VF1 first-player appearance. VF2's animated hands and
separately animated accessories (hair strands, cloth and belt pieces) are retained.
Shun and Lion have no VF1 models and keep their original behavior and appearance.
Dural and the secret-character cheats are outside this regular-roster selection.

Akira retains the three fitted facial expressions described below. The other seven
follow VF2's common eye states (open, partly closed, closed), using VF1 eye frames
0, 8 and 16 with stable brows and mouths. Jacky's two closing-eye states also use
his open-mouth frame. Kage always uses mouth-list frame 0, the blue covering;
frames 1 onward expose his mouth and are not used with the masked model. These are visual fits to each game's original parts.
All known head aliases for both costumes are covered to prevent switching back to
VF2 geometry; unclassified aliases retain the neutral face.
The original head-only and body capture presets remain Akira-only regression tools.

The ten Akira head variants are bound so a face change cannot switch those
draws back to VF2's face. The upper arms are scaled by 25/26 along their local X
axis and forearms by 25/21: measured joint lengths are VF1 0.26/0.21 and VF2
0.25/0.25. Leg joint lengths agree (0.39/0.44). The head needs no axis conversion.
The presets contain addresses and alignment metadata only; no ROM-derived assets
are committed.

Three head variants were observed in the game trace. Their correspondence is a
visual fit using VF1's own facial parts, not identical facial anatomy or shared
animation indices. VF2's displayed object controls the change, including smooth
replay; no independent blink timer or game-memory patch is used. Seven further
head aliases were identified by geometry; their expression use is unverified and
they retain the neutral fallback. The legacy capture presets still have fixed faces.

| VF2 head object | VF1 eye / brow / mouth frames | Appearance |
| --- | --- | --- |
| `84dc10` | `0 / 1 / 1` | Original neutral fit |
| `86ff68` | `0 / 1 / 9` | Eyes open, teeth showing |
| `870c11` | `16 / 16 / 16` | Eyes shut, clenched teeth |

VF1's face table is at `fd7f4d` (record `fd4f99 + 72`). Parts 0–3 are fixed,
4/6 and 5/7 are paired eye/brow lists, and 8 is the mouth list. The rendering
lookup resolves the actual displayed head before the generic skeleton-slot binding;
otherwise every expression would be mapped back to the neutral head.

The shared VF2 face-state table is at `main_data + 0x768c + fighter_id * 48`:
three rows of four descriptor indices cover expressions and costume/mask variants.
Rendered head comparisons identify partial and closed eyelids for the seven newly
supported fighters. Synthetic keys `0xf1000000 + roster_slot * 32 + part_slot`
keep body parts separate even when VF2 shares model addresses; slots 16/17 are
those two additional face states. The player's roster index and choice travel
with stored display lists for smooth rendering and replay.

## Run from the two ZIPs

Put your MAME `vf.zip` and `vf2.zip` together in the ROM folder, then launch VF2
normally. On iOS use **Files → On My iPhone/iPad → VF2 Recomp → roms**, copy both
files before returning to the app, and restart if VF2 was already running.
The existing Documents-root location is also supported. See the
[Apple setup guide](../../apple/README.md).

On character select, highlight one of the eight supported fighters and press **Up for VF1** or **Down for VF2**,
then confirm with an attack/guard button. The default is original VF2. The keyboard,
controller D-pad and touch joystick all use the same cabinet input path. Each player
chooses independently; the choice is locked on confirmation and stays through the fight.
Browsing away clears it; a new selection or machine reset starts from original VF2.
Fight directions keep their normal behavior. Without `vf.zip`, controls remain unchanged.

The selected fighter's small icon at the bottom also changes to VF1's original select portrait
when Up selects VF1, and returns to VF2 with Down. The slot is shared: in two-player
selection each icon shows VF1 while either player has that VF1 fighter selected. The large 3D
portraits remain independent. The original cursor borders and player labels stay
above the replacement image, which is cropped to fit without stretching the face.

`romset::load` validates VF2 and reads the optional VF1 ZIP. It verifies 15 required
VF1 ROM members by size and CRC, decodes the character record, polygon strips,
colour-index stream and BGR555 palette, and prepares per-character bindings for the 13 replaced skeleton slots in
memory. The 104 body/head bindings, 14 additional face states and 22 original
Akira object-address bindings total 140 prepared entries. The ROM buffers also supply each original 4bpp select portrait, tile map and
BGR555 palette; no separate image file is needed. The temporary VF1 ROM buffers are
then released. Each launch repeats this preparation; nothing is extracted or cached on disk. Gameplay uses VF2's engine.

No Python, VF1 executable, capture, `.mesh` file or environment variable is needed
for normal play. Without `vf.zip`, the original VF2 models are used. A present but
invalid VF1 ZIP reports an error rather than silently ignoring the model request.
Remove `vf.zip` from the app's ROM locations and restart to return to original VF2.

For an automated match with punches, kicks, crouching and guarding, the optional
Python helper stages the two ZIPs, builds the headless runner and saves PNGs:

```sh
python3 tools/models/demo-vf1.py /path/to/vf2.zip --vf1-rom /path/to/vf.zip
```

Output defaults to `build/runtime-vf1-demo/`. The game itself performs all decoding;
the helper only copies the ROMs and launches the runner.

## Legacy capture tools (fitting and regression only)

The original prototype's capture/packing tools remain for fitting additional parts
and comparing the runtime decoder against the VF1 renderer. They are not part of
startup. A capture requires a built sibling VF1 desktop app with `VF_DUMP_FIGHTER`:

```sh
python3 tools/models/capture-vf1.py ../vf1-recomp/vf.zip
python3 tools/models/pack-vf1.py build/vf1-capture/akira.jsonl --list
```

## Inspect more parts and make bindings

With a local VF2 ROM, record a frame in which the selected fighter is visible:

```sh
make -j8 build/bin/vf2run
VF2_DUMP_OBJECTS=1650-2150:build/vf2-objects.jsonl \
  build/bin/vf2run /path/to/vf2.zip 2400 --script tools/models/akira-demo.txt
```

The supplied script selects default Akira. Each JSON line records frame, polygon
address, count, mode, matrix and focus. A single-frame `1700:file` specification
also works. Identify the actual part before making a game binding; the
implementation does not infer a bone name from an address.
An explicit diagnostic `VF2_MODEL_OVERRIDES` package affects **every occurrence** of
that ROM model, including portraits and mirrored matches. Normal runtime VF1 selection
instead tracks each player's model slots and replaces only the chosen player. Costume
and pose variants can use other addresses.

Use `presets/akira-head.json` or `presets/akira-body.json` for the verified mapping.
A minimal binding for the neutral head is shown below; the full head preset also
handles nine facial variants. `source_record` prevents packing the wrong fighter.

```json
{
  "source_record": "fd4f99",
  "bindings": [{
    "object": "0x84dc10",
    "tags": ["h*"],
    "reference": "h0"
  }]
}
```

`tags` selects captured VF1 parts (glob patterns). `reference` identifies the part
whose local frame the group uses. The converter applies
`alignment × inverse(reference capture matrix) × part capture matrix` to vertices,
so the captured world/camera pose is removed. `matrix` is column-major 3×4:
three axis columns followed by translation; omit it for identity. It can set the
scale, orientation and pivot required by a measured VF2 part. Head variants and
hair in this first prototype are frozen to the captured geometry.

```sh
python3 tools/models/pack-vf1.py build/vf1-capture/akira.jsonl \
  --bindings tools/models/presets/akira-body.json --out build/akira.mesh
VF2_MODEL_OVERRIDES="$PWD/build/akira.mesh" build/bin/vf2run /path/to/vf2.zip 2400 \
  --script tools/models/akira-demo.txt
```

The same environment variable works with the desktop app and Xcode Run scheme.
Paths must be accessible to the app; an iOS simulator/device has its own sandbox.
This diagnostic environment variable overrides the models prepared from `vf.zip`.
There is no in-app package importer/menu. A malformed package logs an error and
keeps the already prepared models (or original VF2 models if no VF1 was loaded). Package loading never writes polygon ROM, palette RAM or game state.

## ROM-free checks and local preview

```sh
python3 tools/models/test-pack.py
make -j8 build/bin/modelcheck
build/bin/modelcheck
# Optional: a package bound to the synthetic address, not a live fighter binding.
build/bin/modelcheck build/modelcheck build/akira.mesh
```

The optional preview writes three PPM images. On macOS, `modelcheck` also requires
Metal and compares its exact renderer against CPU pixels. Other platforms check
CPU rendering and GPU export without a GPU backend.

## Runtime loader checks

```sh
make -j8 build/bin/vf1check
build/bin/vf1check /path/to/vf.zip /path/to/vf2.zip
# Optional comparison against the original renderer capture:
build/bin/vf1check /path/to/vf.zip /path/to/vf2.zip build/vf1-on-vf2/akira-body.mesh
# Real-ROM selection and mirror-match regression (vf.zip beside vf2.zip):
make -j8 build/bin/selectcheck
build/bin/selectcheck /path/to/vf2.zip
# All eight fighters, icons, exclusions and two-player choices, with screenshots:
make -j8 build/bin/rostercheck
build/bin/rostercheck /path/to/vf2.zip
# Actual Akira face transitions versus the previous fixed face, with screenshots:
make -j8 build/bin/facecheck
build/bin/facecheck /path/to/vf2.zip
```

The local-ROM test checks automatic loading, import into the destination ROM folder,
legacy Documents paths, vanilla VF2 without a companion, corrupt VF1 rejection,
transactional failure and absence of extracted/cache files. Against the original
Akira capture, the body and neutral head bindings match, with maximum vertex
difference 2.99e-8. The two animated expressions are excluded from that fixed-face
reference comparison. Preparation took about 36 ms on the development Mac; this is not
a device performance measurement.

`selectcheck` exercises default VF2, Up/Down preview changes, confirmation, fight
directions, browsing away, missing VF1 and reset. It checks independent P1/P2 pixels
in the selection screen and a mirror match, including alternate Akira, and compares
game RAM against the same inputs without skin changes. It also checks the small
icon's Up/Down restoration, unchanged game/tile/character RAM, stored-picture
redraw after a live selection change, and the tile image used by GPU composition.
`modelcheck` also covers
identical model addresses belonging to different players and redraw/smooth replay of
an older choice after the live selection changes. It also checks that the displayed
face wins over the actor's current slot and that smoothing preserves face changes.
`rostercheck` checks model/icon changes and restoration for all eight fighters,
unchanged VF2 game RAM, Shun/Lion exclusions, clearing the choice when browsing to
another supported fighter, mixed-fighter choices and a mirror match. It saves
original/VF1 select and fight screenshots for visual inspection.
`facecheck` runs 2,400 frames against the previous fixed-face model: the neutral
picture matches, both other expressions change pixels, and game RAM matches every
frame. It saves the three expressions and their fixed-face references as PPM images.

The eight-fighter expansion passed these checks with the user's ROM pair. Additional
mirror cases cover Wolf, Kage and Jeffry; a Pai/Sarah match covers different fighters
chosen independently. Xcode Debug builds pass for arm64 devices and the simulator.
A normal iOS simulator launch read both ZIPs from Documents/roms, prepared all 140
bindings in 47 ms, and displayed VF1 Pai/Sarah selection and combat through Metal.
The screenshots and logs are under the ignored `build/vf1-roster/` directory;
physical-device gameplay has not been verified.

Before expression animation was added, a 2,400-frame run using just the two ZIPs reproduced all six sampled prototype
screenshots byte-for-byte and matched all 28 original VF2 memory dumps. CPU/Metal
exact pixels also agreed in seven sampled frames at each of 1x and 2x. Xcode Debug
builds succeeded for arm64 iOS devices and the simulator; a simulator launch through
the normal Documents/roms importer prepared the models in 37 ms and displayed the
Akira–Lau match. Physical-device gameplay remains untested.

## Validation and limits

The scripted match was run with the original and replacement models. At frames
1700, 1900, 2100 and 2300, all seven dumped memory regions matched byte-for-byte
(28 comparisons: work RAM, display-list RAM, palette, colour translation, tile RAM,
backup and EEPROM). This checks that the rendering change preserves gameplay state.
CPU and Metal exact rendering matched every covered pixel at 1x and 2x in 12 sampled
frames per scale. `modelcheck` separately covers smooth-motion replay and near
clipping. These comparisons cover the sampled frames, not every move and camera.

`VF2MESH 1` is a bounded text format: magic/version/object count; then hexadecimal
object address and decimal triangle count, followed by hexadecimal RGB and nine
local vertex coordinates per triangle. A whole file is validated before activation.
Limits are 128 ROM object bindings, 16,384 triangles per binding, 65,536 total,
finite coordinates within ±1,000,000, unique addresses and no trailing data.

The prototype uses two-sided triangles and approximate flat lighting from VF2's
light vector. Its shading is not a recreation of VF1's lighting hardware. Replaced
polygons use minimum vertex depth for sorting. The original object stream is still
consumed to preserve inherited rasterizer state. Additional stages/cameras, throws,
hand variants, cloth attachments, fine seam fitting and alternate costumes need
further testing. The diagnostic package mode is global; the normal Up/Down selector
is per player and its choice is saved with each rendered display list.
