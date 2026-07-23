import type { Skill } from "./types.js";

// CLAUDE-NOTE: Added 2026-07-14. Editing EXISTING AnimSequences via run_python (there is no MCP anim tool).
// Captured while de-animating a crab's eyes/claws, trimming to a seamless loop, and making rate_scale retimes.
// Distinct API surface from authoring new clips — the gotchas here are silent no-ops / wrong-frame traps.

export const animEditingSkill: Skill = {
  name: "anim-editing",
  description: "Edit existing AnimSequences via run_python: remove bone tracks, retime, seamless-loop trim, ID bones",
  content: `# Animation Editing Skill (run_python)

No MCP tool edits AnimSequences — use run_python. model = anim.data_model_interface (a PROPERTY, not a method;
get_data_model fails). ctrl = anim.controller. All edits go inside ctrl.open_bracket('x', True) ... ctrl.close_bracket(True).
If a bracket call throws mid-edit the bracket stays OPEN — call ctrl.close_bracket(False) before retrying.

## Gotchas
- Track names are LOWERCASED: model.get_bone_track_names() -> "bone_027_012", "_rootjoint", while SKELETON bone
  names (SkeletalMeshComponent.get_bone_name) are "Bone_027_012", "_rootJoint". Match case-insensitively or
  removals/edits are silent no-ops.
- You cannot read keys directly: get_bone_animation_tracks() is empty and internal_track_data.get_positional_keys()
  is empty (compressed rep). READ a pose with unreal.AnimationLibrary.get_bone_pose_for_frame(anim, bone, frame,
  True) -> LOCAL (parent-relative) FTransform, exactly what set_bone_track_keys wants.
- set_number_of_frames needs the STRUCT: ctrl.set_number_of_frames(unreal.FrameNumber(N)) (raw int fails). keys = frames + 1.
- After edits, verify by re-reading get_bone_pose_for_frame; save with EditorAssetLibrary.save_asset(path, False).

## Operations
- Remove a bone's animation (-> it holds the reference pose): ctrl.remove_bone_track(lowercase_name).
- Retime (speed), non-destructive: duplicate the asset, a.set_editor_property('rate_scale', mult). RateScale is a
  PLAYBACK multiplier (asset length / get_play_length unchanged; effective time = length/mult). Retimes are
  independent copies — regenerate them after editing the source.
- Rewrite keys: ctrl.set_bone_track_keys(bone, pos[], rot[], scale[]) (replaces ALL keys; arrays of
  unreal.Vector / unreal.Quat).
- Trim to N frames: capture keys 0..N first (evaluation reads current data), then set_number_of_frames(FrameNumber(N))
  and rewrite every track truncated.

## Make a clip loop seamlessly
1. Trim to the natural loop point (best when the tail is bad): scan frames near the suspected loop for the one
   minimizing sum over bones of angle(rot_f, rot_0) vs frame 0; delta ~ 0 is a perfect match F. Keep frames 0..F-1
   (so F-1 -> 0 == the natural F-1 -> F step). Do NOT also keep F or you get a 1-frame stutter.
2. Stitch the ends (whole clip wanted): for each bone whose endpoints differ, ease the last K frames' rotation
   toward the frame-0 rotation (nlerp + smoothstep) so the last key equals frame 0.

## Identify unnamed bones (eyes / claws / legs on a generic rig)
Component-space positions: skel.get_reference_pose() -> AnimPose; pose.get_bone_pose(name, unreal.AnimPoseSpaces.WORLD).
Parent hierarchy: spawn a temp SkeletalMeshActor, smc.set_skeletal_mesh_asset(mesh), smc.get_parent_bone(name) per
bone (destroy the temp actor after). Classify leaf positions (Z=up, +Y=front): eyes = top/front central symmetric
pair; claws = twin-tip pincer chains (two bones at the same tip); legs = side/rear chains reaching the ground.
Bone names are meaningless to the user too — CONFIRM the mapping before stripping animation.
`,
};
