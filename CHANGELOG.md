# Changelog

## Unreleased

- A copy of Test Drive III that is not the supported release is now refused with a plain message ("… is a different release of Test Drive III …") instead of "unexpected load image size"; README: "Supported game version" says which `TDIII.EXE` the port needs and how to check yours.

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
