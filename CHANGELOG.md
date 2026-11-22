# Changelog

## Unreleased

- Draw distance extended to the whole wrapping 32×16 leg map (18 cells, now the default), beyond the game's 3/6/10 cells: the far ring uses 32-bit vertex offsets instead of the game's 16-bit x4 ones, and selects cells, objects, cars and sprites by their nearest image across the map's seams.
- Cars the game culls at its own draw distance are carried into the enhanced far ring with their live motion and heading, so they no longer pop into view as the landscape renders farther.
- `--far-lod 1` restores the original's reduced far models for vehicles and some buildings (off by default: the enhanced view keeps the full-detail models at every distance). The launcher has the matching "Use original far models at distance" checkbox.

## Unreleased

- Draw distance extended to the whole wrapping 32×16 leg map (18 cells, now the default), beyond the game's 3/6/10 cells: the far ring uses 32-bit vertex offsets instead of the game's 16-bit x4 ones, and selects cells, objects, cars and sprites by their nearest image across the map's seams.
- Cars the game culls at its own draw distance are carried into the enhanced far ring with their live motion and heading, so they no longer pop into view as the landscape renders farther.
- `--far-lod 1` restores the original's reduced far models for vehicles and some buildings (off by default: the enhanced view keeps the full-detail models at every distance). The launcher has the matching "Use original far models at distance" checkbox.

## Unreleased

- The two computer opponents now drive a random selection of the other cars on the play disk, including the
  expansion cars; the same two cars race all legs of a course. On by default, with a launcher checkbox and
  `--random-rivals 0` to keep the original's behaviour.
- Car weight multiplier (`--weight 1|2|3`, default 1): scales gravity so cars stay planted over hills instead
  of flying off crests at speed. 2x/3x also increase climbing resistance and grip. Available in the launcher.

## 0.2.0

- Distance fog per pixel: the scenery sinks gradually into the fog instead of face by face; stronger by default (70 %).
- Headlight beams have soft edges, with their own strength by day and at night (Enhanced lights, `--enh-lights`).
- Launcher: menus (File > Preferences with Always on top, Game settings, About), as in Aces of the Pacific Enhanced.
- Launcher: Game settings > Graphics, with sliders for motion, draw distance, fog, where the fog begins and the headlight beams (strength at night and by day, soft edge).
- Launcher: Game settings > Key Bindings; the game applies them while racing (`--keys`).

## 0.1.1

- The game executable may be named `TD3.EXE` as well as `TDIII.EXE`.

## 0.1.0

- Project started from the faithful Test Drive III port, with its launcher renamed "Test Drive III Enhanced".
- Enhanced renderer: the 3D view and mirror are redrawn smoothly at the display's rate (`--motion-delay`).
- High resolution (`--res-scale`) with anti-aliasing (`--aa`), sprites drawn from their native images.
- The cockpit overlays are replayed over the enhanced view; the dashboard, HUD and menus stay the original's.
- The main menu's turning car is enhanced too; `--classic` shows the original picture only.
- Fixed shimmer where parts of the view fell back to the blocky original for a frame.
- The car answers the keys a frame sooner.
- Wheels of the other cars keep their size as the cars drive away.
- Default game speed is 14 ticks a frame (`--frame-ticks`), and the race clock counts real seconds.
- Fixed double images of trees and roadside sprites when entering a new map cell.
- Fixed another car's parts being carried with the player's car after leaving the chase view.
- Fixed flickering road markings and trees, which are now depth-tested against the scenery.
- The sun and moon are a smooth disc and crescent.
- Finish marker on the compass (`--finish-marker`).
- Draw distance of up to 10 map cells (`--draw-distance`), with the scenery, objects and trees beyond the game's own.
- Distance haze (`--haze`), after Play Stunts' upgraded renderer.
- Vehicles and buildings always use their full model in the enhanced view.
- The eye no longer dips under the ground on steep slopes or when landing.
- Smooth keyboard steering, from a small correction to a hard turn; spins, slides and the handbrake are unchanged.
- The car moves along its exact heading instead of in 64 fixed directions.
- Landings on a downslope after a jump no longer count as a drop onto the flat.
- The aeroplane is drawn small and flies slowly, so it looks far away.
- Drifting sprites (the aeroplane, clouds, birds) keep moving at the longer draw distance.
- Police cars give chase only once the player has passed them, and ticket opponents only once overtaken.
- The ticket message reads "20 second penalty" instead of ":20 penalty".
- The menu preview shows its trees on the first visit, with no pixelated frame when a race starts.
- Launcher: a "Picture" box for graphics, resolution, anti-aliasing, motion, haze and draw distance.
- Developer aids: `TD3_ENH_COMPARE` (enhanced and original side by side) and `TD3_ENH_LOG` (per-frame log).
