import type { Skill } from "./types.js";

// CLAUDE-NOTE: Distilled from building a stylized cartoon sky for a candy-world level
// (M_CandySky family). Captures the equirect dome-mapping recipe and the three
// non-obvious editor gotchas that cost the most time, two of which now have native
// endpoints (set_material_scalar_default, reset_transaction_buffer).

export const skySkill: Skill = {
  name: "sky-dome",
  description: "Stylized textured sky-dome (equirect cloud panorama) workflow + gotchas",
  content: `# Sky Dome Skill

How to give a level a stylized painted sky (e.g. cartoon clouds) by texturing the
engine sky sphere, plus the editor gotchas that will otherwise burn time.

## Recipe: textured sky dome on SM_SkySphere
1. Put a 2:1 equirectangular sky panorama on a texture (deep color at top of image,
   pale near the bottom). Keep clouds in the MIDDLE band with clean sky at the very
   top and very bottom edges — this hides the pole pinch and the horizon stretch.
2. Build an **Unlit, two-sided, emissive** material. Compute the view direction in a
   Custom HLSL node from \`normalize(WorldPosition - ObjectPositionWS)\` and map it to
   UVs (hemispherical: horizon -> image bottom, zenith -> image top):
   \`\`\`
   float3 d = normalize(Dir);
   float u = atan2(d.y, d.x) * 0.15915494 + 0.5 + Pan;   // 1/(2pi); Pan = Time*speed for drift
   float v = 1.0 - saturate(asin(saturate(d.z)) * 0.63661977); // 2/pi
   return float2(u, v);
   \`\`\`
   Sample the panorama with that UV into Emissive. Add scalar params (e.g. DriftSpeed,
   Brightness) so variants are tweakable.
3. Assign to the SM_SkySphere static mesh's material slot 0.

## Gotcha 1 — the sky renders washed-out / hazy
A giant far sky mesh gets veiled by SkyAtmosphere aerial perspective + ExponentialHeightFog.
Fix: set the material flag **is_sky = true** (recompile). It excludes the material from
fog + aerial perspective with NO scene-wide fog changes. (Fallback lever:
ExponentialHeightFogComponent.fog_cutoff_distance.)

## Gotcha 2 — AI-generated panoramas are NOT seamless
Text-to-image output has a hard vertical seam at u=0/1. To heal horizontally: tile the
image x3, GaussianBlur (~r48), crop the centre third, and composite that wrap-blended
copy over the original only within a smoothstep band (~90px) at both edges. This dissolves
cut clouds into a soft cloudless lane — no hard line, no ghosts, no horizontal streaks.
(A single-cloudless-column fill causes horizontal streaks; a light blur leaves ghosts.)

## Gotcha 3 — can't change a base material's scalar DEFAULT from Python
UMaterial's Expressions array is protected; MaterialEditingLibrary only has *instance*
setters + a base *getter*. Use the **set_material_scalar_default** tool (native C++ walks
GetExpressions()). Do NOT rebuild the whole material graph just to change one default.

## Gotcha 4 — a material you just touched won't delete
delete_asset / delete_loaded_asset return false because the editor's undo/transaction
buffer pins the object. Use **reset_transaction_buffer** (clears undo + GCs) then delete.
Previously this needed an editor restart.

## Bonus — material reference sheets
To capture the same framing across N material variants for review: in ONE run_python per
variant, set_material(slot0, mat) then AutomationLibrary.take_high_res_screenshot(w,h,path)
— the shot lands on the next tick so it reflects the just-set material (no race, editor
mode, no PIE). First display of a material may show a shader-compile frame (dark + yellow
text) — just recapture that one. Then compose the tiles into a grid image with PIL.
`,
};
