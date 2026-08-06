# Fidelity and degradation

glyphcade is an executable demonstration of one TermForge idea: **one ruleset
can survive several presentations**. A capable terminal should make a game more
beautiful and, where richer input exists, better to operate. A modest terminal
must still receive every fact needed to play.

This is not generic compatibility policy. It is part of each game's design. A
game is incomplete until its lowest and highest fidelity paths tell the same
truth.

## Profiles are names, not terminal grades

The following names describe rendering outcomes:

| Profile | What glyphcade may use |
|---|---|
| **Baseline** | 7-bit ASCII, no colour, one glyph per cell |
| **Styled** | Unicode glyph families and the colour the terminal reports |
| **Raster** | truecolour images rendered through cells/half-blocks |
| **Native** | Kitty graphics placements at the terminal's pixel density |

They are useful vocabulary, not an `A`-through-`E` ranking and not a capability
enum to add to `GameMeta`. Real terminals are not totally ordered. Graphics,
keyboard protocol, dimensions, mouse reporting and audio availability vary
independently: a small terminal may have Kitty graphics; a large truecolour
terminal may have only legacy keyboard input.

TermForge currently exposes that reality as independent facts
(`kitty_graphics`, `truecolor`, `color_levels`, `kitty_keyboard`) and chooses a
graphics driver from them. glyphcade must not flatten those facts into one
grade and then infer capabilities the terminal did not report. Sixel is a future
graphics path, not a reason to change this rule.

## The contract for an arcade game

Every roster entry records three things in its design, whether or not all three
are eventually represented in metadata:

1. **Floor** -- the smallest geometry at which the game will enter, plus the
   Baseline rendering and legacy-keyboard controls that make it playable there.
2. **Preferred experience** -- independent capabilities that materially improve
   presentation or input feel. Preferred never means required.
3. **Degradation story** -- what replaces every preferred feature when it is
   absent, and how the player is told.

The following rules are normative:

- **Terminal capability never grants permission to play an arcade game.** Every
  game works without colour, Unicode, pixels, mouse, audio or enhanced keyboard
  input. Geometry is the only terminal property that may refuse entry, because
  a fixed rules extent sometimes cannot be represented in fewer cells.
- **Rules and essential information do not change with fidelity.** Colour,
  images, animation, sound and mouse affordances are redundant carriers. A
  fallback may change presentation or input feel, but not board extent, legal
  moves, timing rules, difficulty, score keys or saved-state meaning.
- **A downgrade is visible and has one owner.** The Shell reports global driver
  choices; a game reports a game-specific loss such as Tetris falling back from
  held-key DAS to discrete movement. Do not emit the same fact once per layer,
  and do not silently substitute behavior.
- **The fallback is designed with the feature.** A sprite is not complete until
  the glyph path exists. A mouse gesture is not complete until the keyboard can
  perform the same action. Colour is not allowed to be the only state marker.
- **Minimum and preferred remain different statements.** `43x24 needed` is a
  hard geometry boundary. `best with native graphics` is a description of the
  experience above that boundary, never a warning that discourages entry.

Audio follows the same informational rule but is not a terminal profile. A
silent build and a session with no device remain fully playable. Mouse is also
an optional input path rather than a probed rendering capability; every action
has a keyboard equivalent.

## Roster matrix

This matrix is the design target. A check mark means the game exists today;
planned rows describe acceptance for work that has not landed.

| Game | Geometry floor | Baseline | Preferred experience | Explicit degradation |
|---|---:|---|---|---|
| **Minesweeper** ✓ | 21x13 `Drawable` | ASCII cells, cursor and complete keyboard controls | Styled number colours; mouse, eventually buttonless hover | Glyphs carry every cell state; keyboard replaces mouse; a selected preset that needs more room names its own floor |
| **2048** ✓ | 29x19 `Drawable` | ASCII lattice and printed tile values | Styled palette and emphasis animation | Values and status text carry state after colour and visual emphasis disappear |
| **Snake** ✓ | 58x20 `Drawable` | Distinct ASCII head, body, food and wall glyphs | Styled glyphs/colour and sound | Shape carries identity; silence changes no timing or score |
| **Tetris** ✓ | 35x24 `Drawable` | ASCII well and legacy-keyboard discrete movement | Styled piece colours and enhanced-keyboard DAS/release | Piece roles remain distinct as glyphs; the HUD and an event name the discrete-input fallback |
| **Sokoban** ✓ | 34x12 `Playable` | ASCII layered tiles, keyboard and camera | Styled tile glyphs/colour and mouse | Every layer remains distinguishable; the camera preserves the fixed map and rules |
| **Solitaire** *(planned)* | 43x24 `Drawable` | 5x3 ASCII cards, keyboard selection and counted hidden prefixes | Native card art, raster card art where supported, mouse drag-and-drop | Text cards preserve rank/suit and every selectable face-up card; keyboard replaces drag |

The matrix is deliberately asymmetric. Not every game needs every profile. A
pixel path added merely so every row has one would make these small games larger
without demonstrating a real requirement.

## Evidence for a degradation story

Each claimed arm needs evidence at the layer that can judge it:

- model tests prove rules and scores do not depend on the selected presentation;
- offscreen rendering tests prove essential state survives the Baseline profile;
- a positive high-fidelity case and an unchanged lower-tier control prove that a
  tier-specific improvement moved only the intended arm;
- pty captures prove real escape sequences, glyph families and terminal restore;
- synthesized events prove enhanced-input behavior when CI cannot negotiate the
  protocol itself;
- human verification remains required for feel, image quality and sound.

An absent capability is not proof that its fallback works. The fallback must be
driven and its surviving information asserted. Making these profiles visible in
the selector and mechanically complete is tracked by [#19](https://github.com/gobha-me/glyphcade/issues/19).

## The boundary of this repository

glyphcade stays small on purpose: one Shell, compact games, one static library
per game, and a Baseline path for every roster entry. It should not become the
container for every application that can be built with TermForge.

An application whose identity genuinely requires Kitty graphics, or whose
honest floor is truecolour half-block raster, belongs in its **own repository**.
There it may state that requirement plainly instead of carrying an artificial
ASCII implementation. Existing examples already take that route:
[gloam](https://github.com/gobha-me/gloam) is a focused 1-bit game built around
the Kitty path, while [OBSCURA](https://github.com/gobha-me/obscura) makes Kitty
graphics part of the game state and says so in its premise.

The same boundary applies to larger DOS-era-inspired projects: strategy,
tactics or simulation games in the spirit of *M.A.X.*, *Fragile Allegiance* or
*Syndicate Wars* deserve independent repositories, asset pipelines, save
formats, release rhythms and capability contracts. They can demonstrate
TermForge at greater fidelity without making an arcade of deliberately compact
games carry their complexity. Inspiration is a direction, not permission to
copy another game's assets, names or rules wholesale.
