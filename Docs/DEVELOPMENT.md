# Chutllu Sim — Development Notes

> Working log so any session can pick up where we left off. Update this as decisions are made.
> Last updated: 2026-07-28

## CURRENT STATE / HANDOFF (2026-07-28) — Voyager mobs converted to native C++

Supersedes the 2026-07-27 handoff below (which said "Voyager BP reverted, soldiers stay placeholder
Manny"). The Voyager roster is now **reimplemented natively in C++**: we keep Voyager's *art* — skeletal
meshes, anim blueprints, weapon meshes, VFX, audio — and drive all of it with our own
`AChutlluEnemyBase` tick-AI. **Nothing depends on Voyager's Blueprint layer running.**

### New files
| File | What |
|---|---|
| `ChutlluVoyagerEnemy.h/.cpp` | Data-driven base. Asset **paths** as `FString` UPROPERTYs, loaded in `ApplyVoyagerVisuals()`. |
| `ChutlluVoyagerEnemyVariants.h/.cpp` | 13 mob classes: Soldier, Trooper, Sniper, HeavyGunner, ShotgunTrooper, FlameGunner, ElectricGunner, Boomer, Gruntling, GruntlingTough, Spiderling, SpiderBoss, Minibot. |

`ApplyVoyagerVisuals()` is called **before** `Super::BeginPlay()` so the base class's Manny / engine-shape
fallbacks see a mesh already assigned and stand down.

### Why the Blueprint route failed (documented so nobody retries it)
`BP_Soldier` T-posed **and** never moved, and both symptoms share one cause: Voyager's character BP assigns
the AnimBP at runtime from its own `BeginPlay` (via `DA_AnimBPsUE4M`), and its behavior tree needs Voyager's
waypoints/blackboard/PlayerController. Outside `LevelCity` that `BeginPlay` bails out → no `AnimInstance`
(T-pose) and no BT (no movement).

### The key technique — driving `AnimBP_Soldier` from C++ by reflection
The AnimBP exposes `Speed`, `Direction`, `IsAiming`, `AimPitch`, `IsWeaponEquipped`, `IsFalling`,
`FirearmHoldingType`, but populates them by casting the pawn to `BP_SoldierCharParent` and reading its
`WeaponComp`/`AnimComp`. Those casts fail on our pawn → everything stays default → unarmed idle, no aiming.
We write the variables directly with `FindFProperty` + `SetPropertyValue_InContainer` each tick. Gotchas:
- Blueprint "float" vars are **`FDoubleProperty`** in UE5 — handle `FDoubleProperty` *and* `FFloatProperty`.
- Blueprint enum entries are internally `NewEnumerator0/2/3...`. Resolve by **display name** ("Rifle") via
  `UEnum::GetDisplayNameTextByIndex` — never hard-code an index.
- These actors tick in **`TG_PostPhysics`** so our writes land *after* the AnimBP's own update (the mesh
  updates in `TG_DuringPhysics`) and can't be stomped. Costs one imperceptible frame of aim latency.
- Safety net: `TickNativeAnim` detects a null `AnimInstance` after ~0.75s and drives idle/move clips via
  `SetAnimationMode(SingleNode)` + `PlayAnimation`. It can never T-pose.

### THE bug that caused "white ball" projectiles (cost two rounds of iteration)
`UWorld::SpawnActor` runs the spawned actor's **`BeginPlay` before it returns the pointer**. So
`Proj->TrailFX = ...` always landed *after* `BeginPlay` had checked for a trail, found null, and fallen back
to the emissive sphere. The VFX were loading fine the whole time. **All projectile spawners must use
`SpawnActorDeferred` → set properties → `UGameplayStatics::FinishSpawningActor`.** Applies to
`AChutlluEnemyBase::TickChaseAndAttack` and both `AChutlluAttackHelicopter` fire functions.

### Asset facts (verified — save the lookups)
- Skeleton **`SK_MannequinMainUE4M`** is shared by `SK_ShadowOps`, `SK_Trooper`, `SK_Murdock`, `SK_Hero`,
  `SK_WraithLunarOps`, `SK_MurdockWasteland` → one AnimBP + one socket set covers the whole human roster.
  Weapon socket `hand_rSocket`, melee `Hand_RSocketMeleeWeapon`; all weapon meshes expose a `Muzzle` socket.
- **`SK_MannequinMain` is the grey untextured mannequin** — never use it as a soldier. Use `SK_ShadowOps`.
- Gruntling rig = `SK_Gruntling_Guardian_Skeleton`, weapon socket **`Weapon_H`**, ~115cm tall.
  Spider rig = `SK_Greater_Spider_Skeleton`, no weapon socket, ~200cm tall and very wide.
- Voyager **muzzle flashes are Cascade** (`P_AssaultRifleMuzzle`); **trails/impacts are Niagara**
  (`NS_MachinegunTrail`, `NS_SniperTrail`, `NS_PlasmaTrail`, `NS_RocketTrailV1`, `NS_ImpactConcrete`).
  `AChutlluEnemyBase` has slots for both kinds.
- **`NS_AA_Gun_1` lives under `Niagara/Environment/`, not `Niagara/MuzzleFlash/`** — unlike the rest of the
  ArmyVFX pack. An easy silent-null mistake.
- Creature mesh yaw was the one value not readable from the assets: `-90°` (the mannequin convention) was
  assumed for Gruntling/Spider. If they face sideways, `MeshRotation` is the single number to change.

### Pre-existing bugs found and fixed along the way
- **Two health bars per enemy** — `AChutlluHUD::DrawEnemyBars()` painted a canvas bar *and* every
  `AChutlluEnemyBase` carries a `HealthBar` UWidgetComponent. Canvas pass disabled.
- **A grey Manny riding inside every vehicle** — `LoadPlaceholderSkeletalMesh()` loaded Manny into *all*
  enemies for ragdoll, including the ArmyVFX tank/jet/heli. Now skipped when `PlaceholderMesh` already has a
  static mesh; `BeginPlay` reordered so plain placeholder enemies still get theirs.
- **Music never played at all** — `UChutlluAudioSubsystem::MusicComponent` was never created, so
  `UpdateMusicIntensity` was a permanent no-op regardless of volume settings. Now spawned on first use with
  Stealth01 (explore) / Alarm02 (combat) switching, plus an ambient loop.
- **Tank never fired** — its `AttackRange` was **500** units; at 220cm/s it essentially never closed that
  gap. Now 3000.
- **Red "debug projectile"** — a red `DrawDebugLine` + target sphere in `AChutlluAttackHelicopter::
  FireHomingMissile` standing in for a missile-lock telegraph. Now behind `bDrawDebugTracers` (default off).

### Content/tuning changes
- Waves now field **all 20 enemy types from wave 1**; what scales with level is the *count* per type
  (common troops ~+50%/wave, siege ~+25%), ~30 enemies at wave 1 → ~90 at wave 8.
- `SpawnBatchSize` (default 4) on the wave manager — squads arrive together instead of one at a time.
- Soldiers roll a random `FChutlluVoyagerWeapon` loadout per spawn (assault rifle / heavy MG / shotgun /
  plasma), which also re-scales damage, cadence and range so the mix plays differently, not just looks it.
- Five new ArmyVFX vehicles: `AChutlluAPC`, `AChutlluArtillery`, `AChutlluAAGun`, `AChutlluMLRS`,
  `AChutlluSPG`, each with its own muzzle flash, shell mesh, smoke trail, ground splash and explosion.
- Supply crate now uses Voyager's `SM_ChestMesh` + `SM_ChestLid`.

### ⚠️ BUILD WORKFLOW — the mistake that cost a whole session
**Live Coding patches the running process IN MEMORY ONLY. It never rewrites
`Binaries/Win64/UnrealEditor-ChutlluSim.dll`.** Restarting the editor therefore **discards every Live Coding
change** — the editor reloads the last real on-disk build. This was misdiagnosed as "the enemies reverted";
proof was the DLL timestamp sitting *earlier* than the editor's own launch time while the sources were hours
newer. **Always finish with a closed-editor build before calling anything done, and only restart after:**
```
Build.bat ChutlluSimEditor Win64 Development -Project="D:/UE_Games/13_Chutllu_Sim/OctopusBackpack.uproject" -WaitMutex
```
Then verify `ls -la Binaries/Win64/UnrealEditor-ChutlluSim.dll` shows a fresh timestamp. Symptom of a
stale DLL referencing hot-added classes: `Assertion failed: Ret->IsA(T::StaticClass())` (Class.h).

**Second automation trap:** `unreal.load_class()` **silently kills** an `execute_python` run in this build —
no exception, code after it never executes, and the MCP call still reports success. Use the exposed type
directly (`unreal.ChutlluVoyagerSoldier`), `EditorAssetLibrary.load_asset`, or the MCP asset-registry tools
(`get_asset_references`, `get_blueprint_info`, `get_skeleton_info`) which are fast and reliable even during
PIE. Diagnose by writing marker files at the start *and* end of a script.

### PENDING / next steps
1. **Nothing in this section has been verified in play by a human or by an automated PIE pass.** Everything
   is verified by successful compile + all 84 `/Game/` asset paths checked against disk. A live pass is the
   single highest-value next step.
2. Gruntling/Spider mesh yaw (see above) — confirm they face forward.
3. Player-ability debug draws in `ChutlluCharacter.cpp` (Eye Beam, Void Nova, Ground Slam) are still
   `DrawDebug*` placeholders; the Ground Slam one is red-orange. Replace with real VFX if wanted.
4. Snow-emitter spawn-rate tuning — still offered, still not done, still **ask before touching**.

Older 2026-07-27 handoff below for reference.

## OLDER HANDOFF (2026-07-27) — PIVOT: no city-gen, military-only waves, Voyager + ArmyVFX

**Big pivot (supersedes the whole NYC living-city milestone below — that work is paused/retired, not
active).** User: no more procedural city generation; the map is now a hand-built **static level**,
`Chutllu_GameMap` (large pre-made city environment, ~10k+ decor actors, world origin roughly around
(1300, 35000)). Gameplay is now **military-only waves** (tanks/planes/helicopters/soldiers) — **no
civilians/humans**. Controls: **right-click = grab** (tentacle), **left-click = damage** (tentacle attack).
Two new asset packs were installed and are now the source of enemy content:
- **`/Game/Voyager`** — a large (~4800-asset) professional third-person-shooter Blueprint framework (AI
  controllers/behavior trees, cover system, abilities, health/death, weapons, animation). Demo level
  `/Game/Voyager/Demo/Levels/LevelCity`.
- **`/Game/ArmyVFX`** — pure art/VFX pack for military vehicles: meshes `SM_Tank/SM_APC/SM_Arty/SM_AA/
  SM_MLRS/SM_SPG/SM_Jet/SM_Heli/SM_Missile` + matching Niagara (muzzle flash, shells, explosions, smoke,
  splash-ground) + per-vehicle Sequencer showcases. **No gameplay logic at all** — meshes/VFX only. Demo
  map `/Game/ArmyVFX/Map/Overview_Map_Day`.

**Decision (reuse vs rewrite), made after inspection:** ArmyVFX → straightforward LoadObject-swap into our
EXISTING C++ enemy classes (same pattern as CitySample prop meshes). Voyager → **tried reusing
`BP_SoldierCharParent`/`BP_Soldier` directly, REVERTED.** Its `HealthComp` binds Unreal's native
`OnTakePointDamage`/`OnTakeRadialDamage` (so our tentacle attack would have damaged it for free) and the
tentacle grab code already handles any `ACharacter` generically (tag `"octo"` → grab/hold/throw, no
consume since it doesn't implement `IChutlluConsumable`) — the *wiring* was sound. But its behavior-tree AI
depends on Voyager's own ecosystem (waypoints, blackboard keys, a specific PlayerController/GameMode class)
that doesn't exist in `Chutllu_GameMap`, so it never actually chased/attacked when dropped in standalone.
**Reverted wave spawning to the native `AChutlluSoldier`** (proven tick-chase-attack AI); `BP_Soldier`
integration code (generic-actor wave spawning, death-delegate bridge) stays in place and works, just isn't
the default anymore. See memory `voyager-armyvfx-integration.md` for full detail + the debugging trail.

**IMPORTANT environmental gotcha (cost ~1 hour twice):** the level has two `Emitter` actors
(`snow_particles`, `snow_particles2`, Cascade `p_snow_particles_green`) that spawn tens of thousands of GPU
particles **every frame**, continuously, even outside Play (`LogParticles: Failed to allocate tiles`
spam). This throttles the editor AND Play frame rate so badly that things that should happen instantly
(a 1.4s spawn timer) can take **45-90+ seconds of real time** to fire, and MCP automation commands can sit
unprocessed for 10-30+ minutes. **User explicitly wants to keep this effect** (likes the visual) — do NOT
delete/disable it without asking again. If something "isn't working," suspect this FIRST (add a temporary
`UE_LOG` + wait 60-90s+ before concluding it's a logic bug — this is exactly what happened with the "AI not
working" report, which turned out to be real but just extremely slow). A tuned-down spawn rate (same look,
sane particle count) was offered as a fix but not yet done — ask before touching.

**Also fixed this session:** enemies never had a health bar — `HealthBarWidget`'s `WidgetClass` was never
assigned (needed manual `WBP_EnemyHealthBar` BP authoring that was never done) and the base C++ widget class
had no visuals at all. Fixed with a **fully code-built dummy health bar** (`UChutlluEnemyHealthBar::
Initialize()` constructs a `SizeBox > Overlay > Border + ProgressBar` via `WidgetTree->ConstructWidget`, no
UMG asset needed) — every `AChutlluEnemyBase` now shows a green→red bar by default.

**Working workflow (IMPORTANT):** editor-open live control via the bundled **UnrealMCPServer**
(`http://127.0.0.1:13579/mcp`, curl JSON-RPC). `execute_python` runs editor Python (print → `LogPython` in
`Saved/Logs/OctopusBackpack.log`), `take_screenshot` returns base64 JPEG, `run_console_command
"LiveCoding.Compile"` recompiles C++ live — works even during a running PIE session (hot-swaps in place).
Start/stop PIE from Python: `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()`
/ `.editor_request_end_play()`; get the live GameMode via `unreal.GameplayStatics.get_game_mode(unreal.
EditorLevelLibrary.get_game_world())`. Full C++ build (editor CLOSED) via `Build.bat ChutlluSimEditor ...`
is needed for NEW UPROPERTY/UFUNCTION/components (Live Coding is in-memory only + won't register new
reflection) — **though it DID successfully hot-compile a brand-new UCLASS file once this session**
(`ChutlluGenericEnemyListener`), so the old "never works for new files" assumption isn't absolute; if in
doubt, a closed-editor build is still the reliable fallback. Details: memory `headless-automation.md`.

**PENDING / next steps:**
1. **Live-test in Play** (not yet done end-to-end by a human): right-click grab on a soldier, left-click
   damage, tank/plane/helicopter waves with real ArmyVFX meshes, health bars showing/updating.
2. **Soldier visual** is still the placeholder Manny mesh (deliberately did NOT swap in a Voyager skeletal
   mesh — risk of a T-posed/non-animating enemy if its AnimBP expects Voyager-specific state; revisit if
   the user wants a better-looking soldier).
3. **PlayerStart** was missing from `Chutllu_GameMap` — added one at a validated street location
   (~1308, 34937, 120), confirmed the player pawn now spawns there without error.
4. **Boss** (`AChutlluBoss`) still uses an oversized placeholder cube — not covered by "tanks/planes/
   helicopters", left as-is; could get an ArmyVFX mesh (e.g. `SM_SPG`/`SM_Arty`) later if wanted.
5. Snow-emitter spawn-rate tuning (offered, not done — see gotcha above).

Older NYC living-city work (paused, not deleted) below for reference.

## OLDER MILESTONE (paused 2026-07-27): NYC living-city — procedural city generation

**Goal:** a New-York-scale living city (real buildings + driving cars + walking crowds) for the Cthulhu
rampage game. Project stays **UE 5.7**. Chose "custom generator + import CitySample assets/systems" over
adopting the 71GB CitySample project. Full research + decisions: memory `city-generation-options.md`.
**This entire milestone is superseded by the 2026-07-27 pivot above** — the map is now a static level, not
a procedural generator — but the code/knowledge is kept in case procedural generation returns later.

**Working workflow (IMPORTANT):** editor-open live control via the bundled **UnrealMCPServer**
(`http://127.0.0.1:13579/mcp`, curl JSON-RPC). `execute_python` runs editor Python (print → `LogPython` in
`Saved/Logs/OctopusBackpack.log`), `take_screenshot` returns base64 JPEG, `run_console_command
"LiveCoding.Compile"` recompiles C++ live. Helper scripts in `Scripts/` and the session scratchpad
(`mcp_post.py`, `mcp_call.py`). Full C++ build (editor CLOSED) via `Build.bat ChutlluSimEditor ...` is needed
for NEW UPROPERTY/UFUNCTION/components (Live Coding is in-memory only + won't register new reflection).
Details: memory `headless-automation.md`.

**DONE & working:**
- **City shell** — `AChutlluCityGenerator` (Source/ChutlluSim): procedural Manhattan grid (streets/sidewalks/
  blockout via ISM/HISM), World-Partition-free, seeded. Bake once via **Regenerate** (CallInEditor), persists
  in the .umap. Real facade/road/sidewalk materials (`ApplySurface`, `bUsePlainTint`). Auto-lighting.
- **REAL BUILDINGS (whole city) — now PRE-MERGED single-mesh, not per-piece HISM.** The old
  `PlaceModularBuildings` (exploded each building into ~hundreds of facade-piece HISM instances, 2.5M
  instances total) caused severe lag and was **reverted** (`ClearModularBuildings`, boxes restored). New
  pipeline: `Scripts/bake_merged_buildings.py` spawns `AChutlluModularBuilding` per (kit, floor-bucket),
  reads its baked HISM instances back out as individual piece transforms, spawns one `StaticMeshActor` per
  piece, and merges them (`StaticMeshEditorSubsystem.merge_static_mesh_actors`) into ONE StaticMesh asset at
  `/Game/Building/Merged/SM_MergedBldg_K{kit}_F{floors}` — 4 kits × 7 floor buckets (6/10/16/24/36/55/80) =
  **28 baked assets**, all at the city's fixed lot footprint (~4983×3650). New generator function
  **`PlaceMergedBuildings()`** (CallInEditor) picks the nearest floor bucket per lot and adds **ONE HISM
  instance per building** (reuses `FacadeHISMs`/`GetOrCreateFacadeHISM`, no per-piece explosion). Verified
  live: all **5384 lots** placed using only **28 unique meshes**, screenshot-confirmed real facade detail
  (window rows visible on a merged skyscraper). This is the "swap without perf cost" building system going
  forward — `AChutlluModularBuilding`/`PlaceModularBuildings` (per-piece) kept as reference/bake-source only,
  not used for live placement anymore.
- **Mass/Traffic/ZoneGraph plugins** imported + compiling (Traffic/MassTraffic, CitySampleMassCrowd,
  RuleProcessor + engine Mass/ZoneGraph). Config in `Content/AI` + `Config/DefaultMass.ini`+`DefaultPlugins.ini`.
  `AChutlluZoneGraphGen`: **BuildLivingCity/BuildZoneGraph** generate ZoneShape lanes (roads+intersections+
  sidewalks) and bake them (`OnZoneGraphRequestRebuild`) — verified 160/7440 lanes. `PlaceTrafficSpawners`
  drops CitySample's BP_MassTrafficVehicleSpawner + BP_MassCrowdSpawner.
- **Consume Mass crowd** — `AChutlluCharacter::ConsumeNearbyCrowd` (Mass entity query + BatchDestroyEntities).

**PENDING / KNOWN ISSUES (pick up here):**
1. **Traffic broken:** cars spawn but CLUMP (don't drive lanes); crowds wander. Lanes build fine → it's a
   MassTraffic **spawner-config** mismatch. ALSO the ZoneShapes+ZoneGraphData were lost in an unsaved close →
   **rebuild ZoneGraph first** (BuildZoneGraph), then debug the spawner via MCP. This is the last broken core.
2. **Roofs are flat GREY on the core box inside merged meshes** (BasicShapeMaterial has no Color param) —
   swap a real dark/roof material before the next bake (re-run `bake_merged_buildings.py`, existing assets
   are skip-if-exists so delete `/Game/Building/Merged` first to force a re-bake).
3. **Perf: instance count fixed, FPS still UNTESTED in Play** — 5384 single-mesh instances (was 2.5M
   piece instances) + tentacle physics; check FPS in Play, PIX/stat unit if heavy.
4. **Building variety:** 4 kits × 7 floor buckets = 28 merged variants (was continuous per-lot piece
   assembly). More kits possible later (extend `GKits` in ChutlluModularBuilding.cpp + re-bake + add the
   new kit index to `PlaceMergedBuildings`'s `NumKits`). One kit renders dark.
5. **~60GB CitySample content** moved into `Content/` triggers a one-time DDC build on first editor open (slow);
   `Content/Character` skeleton folder missing (crowd anim errors, non-fatal).

Plan + progress detail: memory `citysample-mass-integration.md`. Older gameplay work: session log below.

## What this project is

An Unreal Engine **5.7** game (`OctopusBackpack.sln` / `OctopusBackpack.uproject`, game module `ChutlluSim`).
The star mechanic is the **OctopusBackpack plugin** (`Plugins/OctopusBackpack`) — a set of
procedurally-generated, physics-simulated tentacles that sprout from the player character.
They idle, reach out to walls/objects, grab, throw, attack, and retract.

The long-term creative goal is to evolve this from a cute "octopus backpack" into a
**Cthulhu-like creature**: more tentacles, emerging from *underneath* the character rather
than from a backpack behind it.

## High-level goals (current milestone)

1. **More tentacles.** Currently 4. Target 6 (leading candidate) or 8. Must be data-driven, not hardcoded.
2. **Tentacles from underneath**, not from behind (a backpack). Change where tentacle roots attach/spawn.
3. **Move logic from Blueprint into C++.** The interaction layer (input → attack/grab/throw)
   currently lives in Blueprints (`BP_OctoPlayer` and event graphs on `BP_OctopusBackpackActor`).
   User wants it in C++ for tighter control. User can supply screenshots of the BP graphs.

## Decisions made (2026-07-24)

- [x] **Tentacle count = 6**, implemented as a configurable `TentacleCount` property (so 8 is a
      one-value change later). Rationale: reads as "many/Cthulhu" at ~1.5× current physics cost vs 2× for 8.
- [x] **BP → C++ port: user will send screenshots** of the interaction graphs (BP_OctoPlayer /
      BP_OctopusBackpackActor event graphs) so the port matches actual behavior. Port happens after
      screenshots arrive.

## Open questions / decisions pending

- [ ] **Exact "underneath" geometry.** BLOCKER for reproducing correct placement in code: the 5 anchor
      points per tentacle (origin / grab / walk / walkGrab / falling) are currently authored as Arrow
      transforms *inside the BP_OctopusBackpackActor Blueprint* — I cannot read those values from the
      .uasset. Two ways forward:
      (a) User sends the current Arrow relative transforms (or we read them together in-editor), so I can
          match existing behavior, then relocate the ring underneath; OR
      (b) I generate a code-driven ring of roots under the pelvis (angle = 360/N·i, tunable radius + Z +
          5 shared local anchor offsets) that the user fine-tunes live in the editor.
      Recommendation: (b) — it's the "code-driven control" the user wants and scales to any N.

## Live editor access — UnrealMCPServer

The project bundles **`Plugins/UnrealMCPServer`** (StraySpark, v2.0.2): an in-editor MCP server
(305 tools) over JSON-RPC 2.0 HTTP. It auto-starts with the editor at `http://127.0.0.1:13579/mcp`
(localhost only). It is **not** registered as a Claude connector, but is reachable directly via `curl`
while the editor is open. This lets me **read the Blueprints directly** (interaction graphs — no
screenshots strictly required) and create/modify blueprints, nodes, actors, and input mappings.

- Read a BP: tool `get_blueprint_info`, arg `asset_path` (e.g. `/OctopusBackpack/Blueprint/Player/BP_OctoPlayer.BP_OctoPlayer`).
  Returns components, variables, eventGraphs (node list w/ class+id+pos), functions. Pin wiring via `get_node_pins`.
- `BP_OctoPlayer` EventGraph = 196 nodes (parent Character; holds BP_OctopusBackpackComponent + camera boom/camera).
- Details in memory: `chutllu-unreal-mcp-access.md`.

## Architecture (as of 2026-07-24)

Two C++ classes do the work. Blueprints subclass them for asset assignment + interaction glue.

### `UOctopusBackpackComponent` (ActorComponent on the character)
- The **brain / state machine**. Lives on the player (and `BP_OctoNPC`).
- Holds `TArray<FOctoHandStatus> handsArr` — one entry per tentacle, each with an
  `EOctoHandAction` state (IDLE, MOVE_TO_ATTACH_POINT, ATTACHED, GRABBED, ATTACKS, THROW, FALLING, ...).
- `TickComponent` drives per-hand ticks (AttackHandTick, ReturnHandTick, IdleHandStateTick, etc.).
- Public BlueprintCallable API: `Attack`, `AttackHand(idx)`, `Grab`(via trace), `Drop`, `Throw`,
  `DropAll`, `ThrowAll`, `OctopusBackpackFlyingMode`, `OctopusBackpackBattleMode`, `SetMovementDirection`.
- **Already count-agnostic**: every loop uses `handsArr.Num()`.
  EXCEPT `InitializeOcto()` at `OctopusBackpackComponent.cpp:60` which hardcodes `for i < 4`.

### `AOctopusBackpackActor` (the visual tentacle rig)
- The **body**. Parented to the character; renders + physically simulates the tentacles.
- Holds `TArray<FOctoTentaclesStruct> tentaclesArr` — per-tentacle physics/visual data
  (sphere chain, physics constraints, spline + spline-mesh chain, junction ISM, skeletal "hand" tip,
  and a set of **Arrow components** marking origin/walk/grab/falling anchor points).
- `GenerateHands()` at `OctopusBackpackActor.cpp:599` **hardcodes 4**: `for i<4` then four explicit
  `GenerateOneHand(...)` calls, one per named mesh/enum value.
- `Tick` rebuilds the spline from the simulated sphere chain each frame and skins spline meshes onto it.
- Show/Hide animations grow/shrink tentacles along the spline.

### The two hardcoded-to-4 spots (the crux of "more tentacles")
1. `OctopusBackpackActor.cpp:599` `GenerateHands()` — loop of 4 + 4 explicit `GenerateOneHand`.
2. `OctopusBackpackComponent.cpp:60` `InitializeOcto()` — `for (i<4)` building `handsArr`.

### What makes them "4" structurally (needs refactor for N)
- `EOcto_Hand` enum (`OctopusTypes.h`) has exactly 4 values: LEFT_BOTTOM, RIGHT_BOTTOM, LEFT_TOP, RIGHT_TOP.
- `AOctopusBackpackActor` declares **named components per hand** (`OctopusBackpackActor.h:33-85`):
  4× StartMesh + 4× each of StartArrow / GrabArrow / WalkArrow / WalkGrabArrow / FallingArrow (24 arrows).
- 4 per-hand `UAnimBlueprint*` pointers (`leftBottomHandAnimationClass`, ...).
- `GenerateOneHand` uses a `switch(EOcto_Hand)` to wire each named arrow set + anim class.

### Where "from behind" comes from
Tentacle root position/orientation = the **Arrow components' transforms**, authored in the
`BP_OctopusBackpackActor` Blueprint child (positions set in the editor). The component reads them via
`GetHandPointTransform` / `GetHandOriginTransform`. So "behind vs underneath" is about where those
roots sit. Making it code-driven (compute a ring of root transforms under the pelvis) both moves them
underneath AND naturally scales to N tentacles.

## Measured facts from the live editor (2026-07-24, via UnrealMCPServer)

### How the rig currently attaches to the character  ← this is why it's "behind"
- `BP_OctoPlayer` (parent `Character`) holds the rig via a `ChildActorComponent` named **`ChildActor`**,
  whose ChildActorClass is `BP_OctopusBackpackActor`.
- **`ChildActor` is attached to `CharacterMesh0` (the SkeletalMeshComponent) at socket `spine_05`**,
  relative offset ~(-19.6, -13.5, 0.3). `spine_05` is high on the back → backpack look, AND it's
  bound to the Manny/UE5 skeleton (fragile if the mesh/skeleton is swapped). **This is the thing to change.**
- The character root is `CollisionCylinder` (CapsuleComponent). Mesh is offset (0,0,-89) under the capsule.

### Two different skeletal meshes — don't confuse them
- **Character mesh** (Manny) — only used as the *attach point* (socket `spine_05`). Moving the rig to the
  capsule needs NO calculation change; it's just a different parent + base transform.
- **Tentacle TIP mesh** (`SK_Hand` + per-hand Anim BPs, inside the plugin) — self-contained, NOT tied to
  the character skeleton. The spline/physics math is independent of it. **Keep as-is.**

### Measured Arrow anchor transforms (relative to the rig's SceneComponent), current 4-hand layout
Frame convention: +X forward, +Y right, +Z up. Bottom pair = Z−, Top pair = Z+; Left = Y−, Right = Y+.
- StartMesh bases (tentacle roots): L/R Bottom ≈ (-6, ∓9, +10), L/R Top ≈ (-6, ∓9, +28) — tight cluster near origin.
- StartArrow (FLY idle rest): (50, ±90, ±120), yaw ±30.
- WalkArrow (WALK idle rest): Bottom (116, ±115, +115), Top (80, ±70, +154), pitch −14..−25 (reach forward/down).
- GrabArrow (FLY grab hold): (50, ±150, ±200), pitch −65.
- WalkGrabArrow (WALK grab hold): (50, ±150, +60..+200), pitch −65.
- FallingArrow (dangle when falling): Bottom (250, ±200, −400), Top (−150, ±200, −400), pitch −90 (straight down).

These give concrete magnitudes to base the new N-tentacle ring/anchor offsets on.

## Recommended approach (not yet implemented)

**Build FRESH C++ classes (new actor + component), reusing the proven physics/spline generation,
rather than mutating the existing plugin BPs.** (User preference: avoid conflicts with existing
Blueprints; keep originals intact as reference.) Data-driven N tentacles, attached to the CAPSULE.

Design:
1. New `UCthulhuTentacleComponent` (state machine) + `ACthulhuTentacleRig` actor (visual/physics),
   ported from `UOctopusBackpackComponent` / `AOctopusBackpackActor`. Keep the sphere-chain + physics
   constraint + spline-mesh + junction ISM generation and the tip `SK_Hand` mesh + anim BP + curves.
2. `int TentacleCount = 6` (EditDefaultsOnly). Both actor and component loop `0..TentacleCount-1`
   (replaces the two hardcoded `for i<4` at OctopusBackpackActor.cpp:599 and OctopusBackpackComponent.cpp:60).
3. **Procedural roots on a ring UNDER the capsule.** For tentacle i: angle = 360/N·i; base position on a
   ring of radius R, at Z near the capsule bottom (character capsule half-height ≈ 88, mesh at −89), so
   tentacles emerge from underneath. No named-per-hand components; no `EOcto_Hand` switch — index-driven.
4. **Attach the rig to `CapsuleComponent`, not a skeleton socket.** Robust to mesh/skeleton swaps;
   independent of animation. (Fixes the `spine_05` fragility.) Anim BP for the tip can be assigned
   round-robin from an array instead of 4 named slots.
5. Derive the 5 per-tentacle anchor frames (fly/walk idle, fly/walk grab, falling) from the measured
   magnitudes above, rotated by the tentacle's ring angle + tuned in-editor.
6. Port `BP_OctoPlayer` interaction (input → Attack/Grab/Throw) into the new C++ component / a C++
   player base. The interaction graphs can be read live via UnrealMCPServer (no screenshots needed).

Physics/spline generation is already loop-over-array, so the heavy code ports almost verbatim; the real
change is *setup* (count, procedural capsule-relative roots) + *attachment*.

## Key files

- `Plugins/OctopusBackpack/Source/OctopusBackpack/Public/OctopusTypes.h` — enums + `FOctoHandStatus`, `FOctoTentaclesStruct`.
- `.../Public/OctopusBackpackActor.h` / `Private/OctopusBackpackActor.cpp` — visual/physics rig (the "4" lives here).
- `.../Public/OctopusBackpackComponent.h` / `Private/OctopusBackpackComponent.cpp` — state machine + BP API.
- `Plugins/OctopusBackpack/Content/Blueprint/Octo/BP_OctopusBackpackActor.uasset` — subclass: mesh/curve/arrow authoring + interaction events.
- `Plugins/OctopusBackpack/Content/Blueprint/Player/BP_OctoPlayer.uasset` — player; holds the component + input → action glue (to be ported to C++).
- `.../Content/Blueprint/Player/Input/` — Enhanced Input actions (IA_Move, IA_Look, IA_Jump, IA_UpDown) + IMC_Default.
- Game module: `Source/ChutlluSim/` (currently near-empty; interaction C++ could live here or in the plugin).

## New C++ implementation (in progress) — game module `ChutlluSim`

Decision: new classes live in the existing **ChutlluSim game module** (not a new plugin) — game-specific
code, less build friction, old plugin untouched as reference. Strategy: copied the plugin C++ into the
module and renamed all reflection types to `Cth*` (avoids collisions with the still-enabled plugin).

New files (Source/ChutlluSim/):
- `Public/ChutlluTentacleTypes.h` (from OctopusTypes.h) — enums `ECth_*`, structs `FCthHandStatus`, `FCthTentaclesStruct`.
- `Public/ChutlluTentacleRig.h` + `Private/ChutlluTentacleRig.cpp` (from the Actor) — `AChutlluTentacleRig`.
- `Public/ChutlluTentacleComponent.h` + `Private/ChutlluTentacleComponent.cpp` (from the Component) — `UChutlluTentacleComponent`.

Done so far:
- Build.cs: added `EnhancedInput`, `Niagara`, `Slate`, `SlateCore`.
- Rig: removed 24 named-per-hand components; root `SceneComponent` now **Movable**. Added data-driven
  layout props (`TentacleCount=6`, `ringRadius`, `ringZOffset=-55`, `rootDownPitch=-60`, `ringYawOffset`,
  5 anchor offset vectors, `startHandStaticMesh`, `tipAnimBlueprints[]`). New helpers
  `GetTentacleRootRelative()` / `MakeAnchorWorld()` compute a ring-under-body frame per tentacle.
  `GenerateHands()` loops `TentacleCount`; `GenerateOneHand(struct,index)` creates the start mesh + 5
  anchor arrows + target arrow procedurally and picks the tip anim BP round-robin. No `ECth_Hand` switch.
- Component: `InitializeOcto()` builds `handsArr` from `backPackOcto->TentacleCount` (was hardcoded 4).

Remaining:
- Wire the plugin assets into C++ via `ConstructorHelpers` (paths captured below) so the creature is
  fully self-contained (no BP needed): rig meshes/curves/anim BPs/effects + component curves.
- Write `AChutlluCharacter` (ACharacter): capsule + Manny mesh + spring arm + camera +
  `UChutlluTentacleComponent` + a `UChildActorComponent(AChutlluTentacleRig)` **attached to the capsule**
  (this is the capsule-attach fix). Enhanced Input (Move/Look/Jump/UpDown + battle/fly/attack). Exact
  input→action mapping to be refined from BP_OctoPlayer's graph (readable via MCP).
- Write `AChutlluGameMode` defaulting to the new pawn; set it for the test map.
- Regenerate project files, close editor, build via UBT, fix errors, reopen, verify 6 tentacles underneath.

### Asset object-paths to reference from C++ (captured live 2026-07-24)
Rig: tentaclesStaticMesh `/OctopusBackpack/Mesh/SM_Cilynder_1`; tentaclesBigStaticMesh `/OctopusBackpack/Mesh/SM_Cilynder_2`;
handSkeletalMesh `/OctopusBackpack/Mesh/SK_Hand`; junctionStaticMesh `/OctopusBackpack/Mesh/SM_Junction`;
tip anim BPs `/OctopusBackpack/Animation/SK_Hand_Skeleton_AnimBlueprint_Child{,1,2,3}`;
curves `/OctopusBackpack/Blueprint/Octo/Curve/C_OctoWalkHandStretch`, `C_OctoAttackHandStretch`, `C_OctoAngularLimitCurve`;
effects `/OctopusBackpack/Blueprint/Octo/EffectActors/BP_LaserBeam{,_4X}`, `BP_Electric` (…_C classes).
Component curves `/OctopusBackpack/Blueprint/Octo/Curve/`: C_OctoWalkMoveHand, C_OctoWalkMoveReturnHand,
C_OctoWalkRotateHand, C_OctoAttackMoveHand, C_OctoThrowMoveHand, C_OctoFallingHand.
Player: skeletal mesh `/OctopusBackpack/DemoUE/Mesh/SKM_Manny_Simple`; anim `/OctopusBackpack/DemoUE/Animations/ABP_Quinn`.
Input assets: `/OctopusBackpack/Blueprint/Player/Input/Actions/IA_{Move,Look,Jump,UpDown}` + `.../IMC_Default`.

## How to test (for the user)

1. Open the project in the editor.
2. The new fully-C++ creature is driven by `AChutlluGameMode` (set as `GlobalDefaultGameMode` in
   `Config/DefaultEngine.ini`). If your test map (`OctupusTest`) has a **World Settings → GameMode Override**
   or a manually-placed `BP_OctoPlayer`, that will win — clear the override (or open a fresh Basic level with
   a PlayerStart) to see the C++ creature. (I can wire this precisely on your map via MCP when the editor is open.)
3. Press Play. Controls: WASD move, mouse look, Space jump, **Z = toggle Battle Mode (spawns tentacles)**,
   **C = toggle Flying**, LMB = attack, RMB = throw all, 1–4 = attack with a specific tentacle, mouse wheel = zoom.
4. Expect **6 tentacles emerging from underneath** the body. Tune in the `AChutlluTentacleRig` defaults:
   `TentacleCount`, `ringRadius`, `ringZOffset`, `rootDownPitch`, and the 5 anchor offset vectors
   (all under categories "Cthulhu | Layout" / "Cthulhu | Anchors").

Build command (editor must be closed):
`"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ChutlluSimEditor Win64 Development -Project="D:/UE_Games/13_Chutllu_Sim/OctopusBackpack.uproject" -WaitMutex`

## Known runtime bugs fixed

- **Character launches into the air on Battle Mode (Z):** the physics tentacle spheres blocked ALL
  channels; with the rig now under the capsule they collided with the owner and shot it up. Fix:
  `SetSphereParameters` now ignores `ECC_Pawn`. (Tentacles still collide with world geometry.)
- **Crash on retracting tentacles (HideHandsTick):** the junction ISM removal loop did `RemoveInstance(i)`
  in an ascending loop while the count shrank → `InstanceIndex < GetMaxInstanceIndex()` assert. Fix:
  remove from index 0, clamped to the live count. Also guarded the empty-spline-mesh-array case in the
  same retract path.

## Session log

- **2026-07-27 (PIVOT — no city-gen, military-only waves, Voyager + ArmyVFX integration)** — User dropped a
  whole new direction: no procedural building generation anymore; a hand-built static level
  `Chutllu_GameMap` is now the main map; gameplay is military-only waves (no civilians); installed
  `/Game/Voyager` (soldier BP framework) and `/Game/ArmyVFX` (vehicle meshes/VFX) and asked for them to be
  wired in, explicitly leaving "reuse BP vs rewrite in C++" to my judgment.
  1. **Recon:** `list_assets`/`get_blueprint_info` over both packs. Voyager = ~4800 assets, a full
     professional TPS framework (`BP_SoldierCharParent` has `HealthComp`/`WeaponComp`/`AIComp`/etc.
     components, its own AI controller + behavior trees). ArmyVFX = 147 assets, purely meshes
     (`SM_Tank/APC/Arty/AA/MLRS/SPG/Jet/Heli/Missile`) + Niagara (muzzle/shells/explosions/smoke) + Sequencer
     showcases — no gameplay code at all.
  2. **Level setup:** `Chutllu_GameMap` had a GameMode override (`ChutlluGamemodeChild`, empty BP wrapper
     over our C++ `AChutlluGameMode`) and `DefaultPawnClass` already correctly wired, `bAutoSpawnCityIfMissing
     =false`, but **no PlayerStart** — added one at a validated street location (traced via nearest
     StaticMeshActor, a parking meter) and saved. First PIE attempt failed to spawn the player
     (`SpawnDefaultPawnAtTransform` — location was blocked by nearby clutter); moved it, worked after.
  3. **Voyager integration attempt:** confirmed via Blueprint introspection (`get_blueprint_info`,
     `get_node_pins`) that `HealthComp.OnDeath` fires with signature `(FName BoneName, FVector
     ShotFromDirection, UPrimitiveComponent* HitComponent, AController* InstigatedBy, AActor* DamageCauser,
     TSubclassOf<UDamageType> DamageType)` and binds the NATIVE `OnTakePointDamage`/`OnTakeRadialDamage`
     delegates — meaning our existing tentacle attack (`UGameplayStatics::ApplyDamage`) would damage a
     Voyager soldier with zero extra code. Also confirmed the tentacle grab code
     (`ChutlluTentacleComponent.cpp`) already handles any `ACharacter` generically (disables movement +
     capsule collision, attaches to hand) and only auto-consumes (civilian-style, destroy+souls) if the
     grabbed actor implements `IChutlluConsumable` — which `BP_Soldier` doesn't, so tagging it `"octo"`
     gives clean grab-and-hold/throw with no consume risk. Built the bridge: `FChutlluEnemySpawn::EnemyClass`
     loosened `TSubclassOf<AChutlluEnemyBase>` → `TSubclassOf<AActor>`; `SpawnNextEnemy` branches
     `Cast<AChutlluEnemyBase>` (native path, existing `OnEnemyDied`) vs generic (new
     `UChutlluGenericEnemyListener` — a tiny `UObject` bound via `FMulticastDelegateProperty::AddDelegate`
     reflection to bridge `HealthComp.OnDeath` back to `WaveManagerComponent::HandleGenericEnemyDied`, since
     the delegate signature carries no actor reference so one listener instance per spawned enemy tracks
     which one died). Wired `BP_Soldier` into the default wave seed. **Live-tested in PIE: it did NOT
     chase/attack** — Voyager's behavior-tree AI needs its own ecosystem (waypoints/blackboard/specific
     controller classes) not present in `Chutllu_GameMap`. **Reverted** the wave seed to the native
     `AChutlluSoldier` (proven tick-chase-attack AI); the generic-actor plumbing stays in the codebase
     (compiles, works for death-tracking) in case a properly-configured Voyager soldier is attempted again
     later.
  4. **ArmyVFX integration (kept, working):** added `MuzzleFlashFX`/`DestroyedExplosionFX` (optional
     `UNiagaraSystem*`) to `AChutlluEnemyBase`, spawned via `UNiagaraFunctionLibrary::SpawnSystemAtLocation`
     at the existing ranged-fire and death-handling call sites. `AChutlluTank` → `SM_Tank` +
     `NS_MuzzleFlash_Tank_Maingun_1` + `NS_Expl_Tank_1`. `AChutlluPlane` → `SM_Jet` (no jet-specific muzzle
     VFX in the pack, reuses the tank explosion on death). `AChutlluAttackHelicopter` → `SM_Heli` + APC
     muzzle flash (closest fit) + tank explosion. All via a small `ApplyRealMesh` helper
     (`ChutlluEnemyVariants.cpp`) that lazy-`LoadObject`s the mesh onto the existing `PlaceholderMesh`
     component (falls back to the engine-shape placeholder if the pack isn't present) — same pattern as the
     CitySample prop mesh swap.
  5. **Health bar bug found + fixed (unrelated to Voyager, affected ALL enemies including native ones):**
     `HealthBarWidget` (on `AChutlluEnemyBase`) never had a `WidgetClass` assigned — it needed a
     hand-authored `WBP_EnemyHealthBar` Blueprint that was never made, and the C++ base
     (`UChutlluEnemyHealthBar`) had no visuals at all, just data (`Percent`, `SetHealthPercent`). Fixed by
     building the widget tree **entirely in C++** — `Initialize()` override does
     `WidgetTree->ConstructWidget<USizeBox/UOverlay/UBorder/UProgressBar>` and assembles
     background+fill, `SetHealthPercent` also drives a green→red color lerp — and setting
     `HealthBarWidget->SetWidgetClass(UChutlluEnemyHealthBar::StaticClass())` in the base constructor. No
     UMG asset needed; works immediately for every enemy.
  6. **Big environmental discovery — the real root cause of "AI not working":** the level has two `Emitter`
     actors (`snow_particles`, `snow_particles2`, Cascade `p_snow_particles_green`) spawning tens of
     thousands of GPU particles **every frame continuously** (`Failed to allocate tiles` spam), even outside
     Play. This throttled the editor so badly that a single Python `execute_python` call sat unprocessed for
     **10-30+ minutes** twice, and forced two editor force-closes/restarts (`taskkill /F` + relaunch) to
     recover. User explicitly said **keep the effect** (likes it) after I flagged it — do not touch without
     asking again. Root-caused the "AI not working" report the same way: added a temporary `UE_LOG` in
     `SpawnNextEnemy`, recompiled via Live Coding **while PIE was running** (worked fine — hot-swaps in
     place), and measured **~55 seconds of real time** between calling `StartGame()` and the first spawn
     tick actually firing. The wave/spawn/AI system was correct all along; it was just running at a tiny
     fraction of normal speed. Removed the debug logging after confirming, recompiled clean.
  7. **Also learned:** Live Coding successfully hot-compiled a **brand-new UCLASS file**
     (`ChutlluGenericEnemyListener.h/.cpp`) once this session — contradicts the earlier assumption
     (memory `headless-automation`) that new files always need a closed-editor build. Still treat a closed
     build as the reliable fallback if Live Coding of a new file ever fails.
  All changes compiled clean (one closed-editor `Build.bat` round for a missed `#include "Blueprint/
  WidgetTree.h"`, everything else via Live Coding). See memory `voyager-armyvfx-integration.md`.

- **2026-07-26 (Building system PIVOT — pre-merged single-mesh buildings, perf fix)** — User: the previous
  session's real-building build ("CivMesh" — the per-piece HISM modular buildings) caused severe lag from
  the sheer object/instance count, and asked to go back to cube-based building, then rebuild the real
  buildings so **every building is one merged object** (safe to swap without perf cost), reusing the
  existing `Kit_.../Kit_Bldg_...` levels under `/Game/Building` (some are complete buildings, others
  single-story sections meant to stack). Decisions (via AskUserQuestion): keep `AChutlluModularBuilding`
  C++ as reference (don't delete), and pre-bake fixed height variants rather than assemble-and-merge per
  building at runtime.
  1. **Reverted the laggy build:** called `ClearModularBuildings()` live via MCP on the placed generator —
     restores the box blockout, saved. (The 2.5M facade-piece instances are gone; boxes are back.)
  2. **New offline bake pipeline** — `Scripts/bake_merged_buildings.py`: for each of the 4 kits × 7 floor
     buckets, spawns `AChutlluModularBuilding`, calls `BuildNow()`, reads every baked HISM instance's
     (mesh, world transform) back out, spawns one `StaticMeshActor` per piece (the merge API only accepts
     true `StaticMeshActor`s, not our custom actor), and merges them via
     `StaticMeshEditorSubsystem.merge_static_mesh_actors` into one asset under `/Game/Building/Merged/`.
     Gotchas hit + fixed: `merge_static_mesh_actors` lives on `StaticMeshEditorSubsystem` (not
     `EditorStaticMeshLibrary`, that method doesn't exist in 5.7); `MeshMergingSettings` field is
     `merge_physics_data` not `b_merge_physics_data`; `base_package_name`/`new_actor_label` are silently
     ignored when `spawn_merged_actor=False` (it dumps to a generic `/Game/.../SM_Merged` instead) — fixed
     by spawning the actor (`spawn_merged_actor=True`), reading the produced mesh's real path off it, then
     `rename_asset` to the intended path, then destroying the spawned actor. Ran live via MCP: **28/28
     baked** (kit0 F6 = 613 piece actors ... kit0 F80 = 8161 piece actors; ~5s-2min each, ~30min total).
  3. **New placement function** `AChutlluCityGenerator::PlaceMergedBuildings()` (new UFUNCTION, needed a
     full closed-editor `Build.bat` rebuild to register for Python/reflection — Live Coding alone doesn't
     register brand-new UFUNCTIONs). Picks nearest baked floor bucket per lot, loads the matching merged
     mesh, adds **ONE** HISM instance per building (reuses the existing `FacadeHISMs`/`GetOrCreateFacadeHISM`
     map — one HISM component per unique mesh, not per piece). Verified live: **5384/5384 lots placed using
     only 28 unique meshes** (down from 2.5M piece instances); screenshot-confirmed real facade detail
     (visible window rows on a merged skyscraper) — object/instance count problem solved. Level saved.
  4. **Known follow-up:** the core box inside each merged mesh is still flat grey (no Color param on
     BasicShapeMaterial) — fix the material before the next bake. FPS in Play still untested with the new
     5384-instance city. See updated PENDING list above and memory `citysample-mass-integration`.

- **2026-07-25 (Living city PIVOT — import CitySample Mass/Traffic/Crowd systems)** — User clarified the real
  goal: a real city where **cars drive on roads (Traffic)** + **NPCs walk on roads (MassCrowd), random only on
  panic** — i.e. CitySample's Mass/ZoneGraph tech, which our box generator fundamentally can't produce. User
  chose (over "build on CitySample project" / "fake it") to **import the systems into our project**. Phased
  plan: **(1 DONE)** copy plugins + enable, compile; (2) bring Mass config content + DefaultMass.ini; (3) THE
  CRUX: generate a ZoneGraph lane network on our street grid; (4) place MassSpawners → cars drive + crowds
  walk; (5) hook gameplay (consume/panic) into Mass agents (near agents = actors, grabbable; far = instanced).
  * **Phase 2 done:** copied `Content/AI` (15MB), `Config/DefaultMass.ini`, `Config/DefaultPlugins.ini`
    (ZoneGraph lane profiles + tags + MassCrowd/MassTraffic settings). Config-only, no rebuild.
  * **Phase 4+5 code done (needs editor verify):** ped sidewalk lanes + `BP_MassCrowdSpawner` + one-button
    `BuildLivingCity()` (regen city w/ facades + zonegraph + traffic & crowd spawners). Phase 5:
    `AChutlluCharacter::ConsumeNearbyCrowd` — Mass entity query (FMassCrowdTag+FTransformFragment) →
    BatchDestroyEntities near the body while tentacles out → souls/panic/"DEVOURED". Added MassEntity/MassCommon/
    MassCrowd to Build.cs. All 5 phases compile; NONE verified in-editor. Modular real-mesh buildings still opt-in.
  * **Phase 3 code done (needs editor verify):** `AChutlluZoneGraphGen` — `CallInEditor BuildZoneGraph()`
    spawns Polygon intersections + Spline roads on the grid (ZoneShapes are editor-only), lane profile
    "Vehicle ↓↑ 400" by name, reads grid from the city generator. Added `ZoneGraph` to Build.cs. Compiles
    clean. Test small (3x3) first; then editor Build > Build ZoneGraph. See [[citysample-mass-integration]].
  * **Phase 1 done:** copied `Traffic`(MassTraffic), `CitySampleMassCrowd`, `RuleProcessor` plugins into
    `Plugins/` (deps are engine-only, NOT the CitySample game module — verified via .uplugin). Enabled them +
    ZoneGraph/ZoneGraphAnnotations/MassEntity/MassGameplay/MassAI/MassCrowd/StateTree/GameplayStateTree/
    SmartObjects/ChaosVehiclesPlugin/AnimToTexture/ContextualAnimation in OctopusBackpack.uproject. **UBT
    build succeeded** (196 actions, ~4min). Note: Mass config assets (EntityConfigs/spawners) + DefaultMass.ini
    are NOT in our project yet (Traffic plugin ships some Content; crowd configs are in the moved Content/Crowd;
    Content/AI + Content/Gameplay + Config/DefaultMass.ini still need bringing → Phase 2). See memory
    [[citysample-mass-integration]].
- **2026-07-26 (Whole city = real buildings, instanced in the generator)** — Refactored modular buildings from
  per-building ACTORS to HISM INSTANCES inside `AChutlluCityGenerator` (user: "all need inside the generator
  like cubes"). New static `AChutlluModularBuilding::ResolveKit` + `BuildFacadeInstances(...Emit)` compute piece
  transforms; generator has `ModularCoreHISM` (dark core/roof) + dynamic per-mesh `FacadeHISMs` (via
  `GetOrCreateFacadeHISM` + `AddInstanceComponent`). `PlaceModularBuildings` now COLLECTS transforms and
  BATCH-`AddInstances` (one-at-a-time was too slow). Converted ALL **5384 lots** (`MaxModularBuildings`=6000) =
  ~2.5M instances in **116s**; outliner = **7 actors** (was 203). Fit fix + 4-kit variety + footprint-driven
  floor proportions all in. TODO/known: flat GREY roofs (core cube tint fails — BasicShapeMaterial has no Color
  param; needs a real dark/roof material), one kit renders dark, runtime FPS with 2.5M instances + tentacle sim
  UNTESTED (HISM culls; reduce if heavy), **ZoneGraph lanes were lost (not saved) → rebuild for traffic**, user
  must Ctrl+S (large save). Full-build required for the refactor (Live Coding can't add UPROPERTY components);
  batched-add tweak was Live-Coded. See [[citysample-mass-integration]], [[headless-automation]].
- **2026-07-25 (Modular buildings WORK + wired to city; ZoneGraph lanes verified; live MCP workflow)** — Big
  session via **live editor MCP** (`http://127.0.0.1:13579/mcp`, `execute_python` + `take_screenshot`; see
  [[headless-automation]]). (1) **ZoneGraph fix:** shapes were never baked → BuildZoneGraph now broadcasts
  `UE::ZoneGraphDelegates::OnZoneGraphRequestRebuild`; verified 160 lanes(4x4)/7440(30x30). Cars STILL clump
  in PIE = MassTraffic spawner config issue (next). (2) **Modular buildings verified beautiful** (NYA kit,
  real facades) via MCP screenshots. Fixed piece-fit: `AChutlluModularBuilding::AddEdgeRow` was placing bays
  at tile CENTRE but the wall mesh pivot is at one end (bounds Y −175..0) → ~half-tile (87cm) gap at corners;
  now centres each bay on its tile via `ExtendDir = Rot.RotateVector(-Y)` (works all 4 edges). (3) Added
  `AChutlluCityGenerator::PlaceModularBuildings()` (clads nearest `MaxModularBuildings` lots with real
  buildings, hides boxes) + ClearModularBuildings. All compiled via **Live Coding** (Ctrl+Alt+F11, editor
  stayed open). Buildings look great. TODO: run PlaceModularBuildings city-wide; fix traffic distribution;
  optional roof caps (tops are flat box).
- **2026-07-25 (NYC city — Approach A start: modular building assembler)** — Began real building silhouettes
  from the CitySample kit. Mapped the kit: most complete set = **NYA** (New York, 579 pcs) at
  `/Game/Building/NY/A/Kit_Bldg_NYA_L{1-7}_{A,B}/Mesh/SM_BLDG_NYA_L{n}_{v}_{Wall|CornerEx|CornerIn|Entrance|
  Column}_N1`; L1=ground(entrances), L2-7=window walls. Built **`AChutlluModularBuilding`** (compiles clean):
  clads a solid core box with HISM facade rows — ground floor = storefront wall + 1 entrance on the +X edge,
  upper floors = window walls, exterior CornerEx at the 4 corners. Tile width/floor height auto-derived from
  the wall mesh bounds (Y=tile, Z=floor); horizontal fit-scaled to avoid gaps. Core box owns collision
  (ignores Visibility); facade HISMs are visual/no-collision. **Orientation is tunable live** (can't read kit
  pivots offline): knobs `PieceYaw`, `VerticalOffset`, `EdgeInset`, `TileWidth/FloorHeightOverride`,
  `bPlaceCorners`, `bBuildCore` + **Build Now / Clear Build** CallInEditor. `ConfigureFromLot()` ready for the
  generator to drive it. **NOT yet wired to streaming** — deliberately: user drops ONE, tunes it right (esp.
  PieceYaw for facade facing + VerticalOffset for pivot), then we wire into the near-building streaming
  (cladding all ~3.5k buildings = millions of instances, so only near ones get mesh detail; far stay
  material'd boxes). Awaiting user's tuned knob values.
- **2026-07-25 (NYC city — Phase B fixes: persistence, spawn-fall, facade materials)** — User feedback:
  city vanished on PIE exit (was rebuilding every play), character spawned high + drifted down ~30s,
  buildings still square/grey. Confirmed **no whole-building meshes exist** in CitySample (only modular
  wall/entrance/column SMs; no assembled building BPs) — so "real building meshes" isn't a swap. Fixes
  (built clean):
  * **Persistence:** the vanishing city was the **transient GameMode runtime fallback** (spawned on Play,
    destroyed on exit). Gated it behind `AChutlluGameMode::bAutoSpawnCityIfMissing` (**default false**). Real
    levels must now **place a generator → click Regenerate → Save** = baked HISM instances persist in the
    .umap, zero runtime rebuild. (Told user: if the map is World Partition, Ctrl+S saves the actor's external
    file too.)
  * **Spawn fall:** `AChutlluCharacter::BeginPlay` now line-traces down (500 up → 30000 down, ECC_WorldStatic)
    and snaps the capsule onto the ground before caching StartTransform — kills the float-down from a high
    PlayerStart / spawn-collision push-up.
  * **Square/grey look:** generator got `BuildingMaterial`/`RoadMaterial`/`SidewalkMaterial` slots
    (`Chutllu | City|Look`) + `bUsePlainTint`. New `ApplySurface` assigns them (auto-loads defaults
    `MI_Bldg_Glass`, `MI_FreewayAsphalt_RoadDark`, `M_Sidewalk` if null), grey tint as fallback. NOTE: facade
    materials on plain boxes may tile oddly (cube UVs) — user can swap/clear per slot. Re-Regenerate + Save to
    apply. Buildings staying box-shaped is expected (blockout); real building silhouettes = future modular
    assembly or use Content/City/Big_City prebuilt.
- **2026-07-25 (NYC city — Phase B art: real CitySample prop/vehicle meshes wired)** — User **moved** the
  CitySample art folders into the project (`Content/Prop` 12.5GB, `Vehicle` 5GB, `Building` 13GB, `Road`,
  `Megascans` 11GB, `Material`, `Textures`, `Crowd`, `Environment`, `City`, etc. — ~60GB; `/Game` paths
  preserved so material refs resolve since the shared Megascans/Material/Textures came too). Wired real
  meshes into `AChutlluDestructibleProp::SetupMeshForPropType` (lazy `LoadObject` at BeginPlay, real-world
  scale, engine-shape fallback if missing): Car=`/Game/Vehicle/vehCar_vehicle02/Mesh/SM_vehCar_vehicle02`,
  StreetLamp=`Prop/Kit_StreetLamp_A/Mesh/SM_StreetLamp_A_Pole_Large`, Barricade=`Prop/Kit_Barricade_A/Mesh/
  SM_Barricade_A`, Barrel=`Prop/Kit_ConstructionTrash_RR/Mesh/SM_Constrash_a_N1` (explosive),
  TrashCan=`Prop/Kit_bench_RR/mesh/SM_street_bench`, Crate=`Prop/Kit_Bicycle_A/Mesh/SM_Bicycle_A_01`. Built
  clean. **Caveats:** first editor open = huge one-time asset scan + Nanite/shader compile; Vehicle/Crowd/
  Building **blueprints** will log missing-class load errors (ChaosVehicles/Mass/PCG/RuleProcessor not
  enabled in our uproject) — harmless since we only use the static meshes. Next: street-prop + parked-car
  scatter across the generated city; real facade materials on the blockout buildings; humans pass.
- **2026-07-25 (NYC city — Phase B: destructible buildings + proximity streaming, pure C++)** — Made the
  city destructible without tanking perf. Built + **compiled clean (UBT)**:
  * **`AChutlluBuilding`** (new actor) — a destructible building: scaled static-mesh (cube for now) +
    `UChutlluHealthComponent`; blocks `ECC_Visibility` so tentacle **attack** sweeps hit it, but is **NOT
    tagged "octo"** (attack-only, not grabbable — a skyscraper shouldn't attach to a hand). `InitBuilding`
    scales HP + soul/score/panic rewards by floor count. On death → `SpawnCollapseDebris` (4–10 tumbling
    physics chunks up the height), awards souls/score via GameState, floating "TOWER FELLED!" text, audio,
    fires `AChutlluCityManager::NotifyPanicEvent`, and calls back `NotifyBuildingDestroyed` so the lot stays
    cleared. Mirrors the `AChutlluDestructibleProp` integration points.
  * **`AChutlluCityGenerator` streaming** — `GenerateCity` now also fills an index-aligned
    `TArray<FChutlluBuildingLot>` (transform+floors, **serialized with the level**) parallel to the
    BuildingISM instances. At runtime (`BeginPlay`, only if lots==instance count) a repeating timer
    (`StreamUpdateInterval` 0.3s) promotes buildings within `ActivationRadius` (130m) to `AChutlluBuilding`
    actors (throttled `MaxActivationsPerUpdate`=5) and **hides the instanced twin** via
    `UpdateInstanceTransform`→~0 scale; retires them past `ActivationRadius+DeactivationPadding` (restores
    the instance) unless destroyed. So only a ~ring of buildings around the player is ever fully simulated
    (the "balance" perf choice). `BuildingClass` + `BuildingMeshes[]` are exposed for the Phase-B art pass
    (real CitySample meshes) — currently everything uses the engine cube so the swap is seamless.
  * **IMPORTANT for the already-baked `ChutluMainLevel`:** the old bake has instances but NO `BuildingLots`
    → streaming self-disables until the user **re-clicks Regenerate + Saves** (repopulates lots). Told user.
  * Next (needs editor open): migrate a curated set of CitySample building/vehicle/crowd meshes and fit them
    to lots (real-mesh transform fitting differs from the unit-cube math), then wire building destruction to
    the rage meter.
- **2026-07-25 (NYC city — Phase A: procedural street-grid shell, pure C++)** — Started the "city as big as
  NYC" milestone. Researched all city-gen options (see memory `city-generation-options.md`): project is 5.7;
  Epic **CitySample is a 5.7 exact match** (71GB, real cars=`Traffic` + crowds=`MassCrowd`), CityBLD is 5.3
  (C++, needs port), the two "Procedural City Creator" packs are BP-only 5.4/5.5, and 5.8's "PCG city" is
  actually **PCG Biome** (vegetation, NOT a city). User picked **custom hybrid, balanced**: generate our own
  controllable/destructible city + borrow CitySample art. Phase A built + **compiled clean (UBT, first try)**:
  * **`AChutlluCityGenerator`** (new actor, `Source/ChutlluSim/`) — pure-C++ Manhattan grid from engine
    primitive meshes (`/Engine/BasicShapes/Cube` + `Plane`), no asset migration. Whole-city asphalt plane +
    per-block raised sidewalk platforms (ISM) + blockout skyline (**HISM** = GPU-culled/auto-LOD, cheap at
    scale). Seeded/deterministic; mid-rise-biased heights with rare skyscrapers; open central plaza so the
    player doesn't spawn inside a building. All layout knobs are EditAnywhere (`BlocksX/Y`, block/lot sizes,
    avenue/street widths, floor counts, `TallChance`, seed…) + a `Regenerate` CallInEditor button; rebuilds
    live via `OnConstruction`. City shell ignores `ECC_Visibility` so it doesn't hijack tentacle aim/grab yet
    (buildings become real grab/destruct targets in Phase B when they get health). Default **30×30 blocks
    (~5.7km × 2.9km), ~3.5k buildings**; scale is one knob.
  * **`AChutlluGameMode::BeginPlay`** now auto-spawns the generator if none is placed (mirrors CityManager).
  * **Bake-once workflow (added same session):** first pass regenerated at runtime every Play (costly +
    invisible in-editor). Now: `bAutoGenerate=false` by default → `OnConstruction` does NOT build; user
    places the actor, clicks **Regenerate** (CallInEditor) ONCE, Saves → instances persist in the .umap and
    Play does zero regeneration. **ClearCity** button empties it. `bAutoGenerate=true` = live rebuild on
    edit (transient, re-cooks each load — for tuning only). GameMode runtime fallback now calls
    `Regenerate()` on the spawned generator so bare maps still build once. **Lighting:** `bEnsureLighting`
    (default on) makes Regenerate spawn a Movable Directional (sun, intensity 8) + Sky light (fill) if none
    exist — persisted in-editor so the level isn't dark. For a full sky, user can add Window > Environment
    Light Mixer. Built clean.
  * **TEST (baked, persistent):** map = `ChutluMainLevel`. Place an `AChutlluCityGenerator` (or select the
    existing one) → tune knobs under **Chutllu | City** → click **Regenerate** → **Save**. City + lights are
    now visible in-editor and won't rebuild on Play. Awaiting user feel/scale check before Phase B (real
    meshes + destructibility).
- **2026-07-25 (held-object collision, death-restart reset, ranged soldiers)** — Three gameplay fixes
  (compiles pending: editor was open / Live Coding — headers processed clean via UHT, .cpp compile via
  Ctrl+Alt+F11 or a closed-editor UBT build):
  * **Held objects no longer shove/trap the player:** on grab, `ChutlluTentacleComponent::AttackHandTick`
    now sets the held component's collision to `NoCollision` (skeletal + generic-prop branches; character
    capsule was already handled). Restored to `QueryAndPhysics` on release everywhere — `Drop`/`DropAll`
    already did; added it to `ThrowObjectTick`.
  * **Restart after death resets the character:** `AChutlluCharacter` caches `StartTransform` at BeginPlay
    and gains `ResetForNewRun()` (teleport to spawn, refill health via `SetHealth` [clears dead flag],
    re-enable input, reset rage/unleashed/eyebeam/trauma, re-deploy tentacles). `AChutlluHUD::StartNewRun`
    now calls it. Fixes the "stuck character" (previously input stayed disabled where it fell).
  * **Soldiers are ranged with visible fire:** `AChutlluSoldier` → `bRanged=true`, `AttackRange=1500`,
    `ProjectileSpeed=6000`, interval 1.4. `ChutlluEnemyBase` ranged fire now draws a muzzle flash +
    bright tracer (DrawDebug, no-op in shipping); `AChutlluProjectile` round is bigger (0.55) + emissive.
- **2026-07-25 (tentacle wobble pt.3 — curvature limiter + fly-drop grace)** — Two remaining issues. Built clean:
  * **"Absurd angles" / too-fast jiggle:** added a Laplacian curvature-limiter pass over the smoothed
    interior points in `AChutlluTentacleRig::Tick` (restructured: fill `smoothedSplinePts` in the sphere
    loop, then Laplacian, then build spline in a 2nd pass). Caps how sharply the rendered curve can bend →
    only smooth arcs, no kinks. Knobs `curveSmoothIterations` (3), `curveSmoothStrength` (0.5). Also
    lowered `splineSmoothSpeed` 14→10 (slower = less fast jiggle). Endpoints stay exact.
  * **Character "falls for no reason" (fly mode dropping):** grips break + re-form constantly while moving;
    the code dropped `bIsFlyingModeActivate` (→ MOVE_Walking → gravity) on the FIRST fully-ungripped frame.
    Added a grace window `flyLossGrace` (0.6s) in `ChutlluTentacleComponent` TickComponent: only fall if no
    grip re-forms within the window (`flyLossTimer`); any grip resets it. Not damage-related.
- **2026-07-25 (tentacle wobble pt.2 — clean-curve shape blend + freer idle)** — Physics tuning + render
  smoothing weren't enough; a rigid chain folds/oscillates when compressed (idle) or pulled taut (gripping).
  Decision: stop letting physics own the shape. Built clean:
  * **Clean-curve blend (`AChutlluTentacleRig::Tick`):** interior spline points are now blended toward a
    CLEAN root→tip line (with downward sag) before the temporal smoothing. Knobs `cleanCurveBlend`
    (0=pure physics, 1=pure clean; default 0.6) + `cleanCurveSag` (25). The clean line owns the silhouette
    so a folded/taut chain can't visibly buckle; endpoints still exact. This is the general cure for both
    the grip-shake ("only one holds, others tremble") and the idle fold-jiggle.
  * **Freer idle:** `boxRandAnim` 200→380 (now EditAnywhere) so idle tips roam a bigger, more organic
    volume; idle anchors pushed out (`flyOriginOffset` 110→200 X, `walkOriginOffset` 100→170 X) so the
    ~5m chain is closer to its natural length near idle reach (less over-length folding).
  * All knobs under "Cthulhu | Physics" (rig) / component params. If grip-shake persists, lower
    `cleanCurveBlend` toward pure-clean (0.8+) or revisit the multi-tentacle re-seek logic. Awaiting test.
- **2026-07-25 (tentacle wobble — the architectural fix: decouple physics from render)** — Damping/projection
  /substep tuning only helped "a bit"; the wobble is structural. Two root causes found + fixed (built clean):
  * **The chain fought the world.** The 25 sim spheres/tentacle *blocked* world geometry, so hanging near
    the floor/walls/props they were shoved every frame (contact popping). Gameplay never used this (attacks
    /grabs are separate sweeps that ignore the rig). Fix: `bChainWorldCollision=false` (default) → spheres
    now generate NO contacts (`SetCollisionResponseToChannels(ECR_Ignore)`); the chain simulates for SHAPE
    ONLY, driven by its root+tip endpoints + joints. Set true to restore physical wall-draping.
  * **The render spline was built from raw physics positions** (jitter → mesh 1:1). Fix: temporal low-pass
    of the interior spline control points with **pinned endpoints** (root + tip stay exact so hand/grab
    precision is kept). New per-tentacle `smoothedSplinePts` cache (`FCthTentaclesStruct`), driven in
    `AChutlluTentacleRig::Tick`; big per-frame jumps snap (rebuild-safe). Speed knob `splineSmoothSpeed`
    (default 14; lower = smoother/floatier, higher = snappier, 0 = off). All under "Cthulhu | Physics".
- **2026-07-25 (tentacle speed + aim-based attack revert)** — Follow-ups after the de-rubber pass. Built clean:
  * **Slower, "robotic-arm" motion:** `tentacleSpeedScale` 0.5 → 0.3 (now EditAnywhere on the component).
    Slowing the tip target reduces the real-time rubber-band whip/sway. Tune this + `physDamping_` for feel.
  * **Attack aims again (reverted the auto-magnet):** `Attack`/`AttackHand` (`ChutlluTentacleComponent.cpp`)
    used `GetSmartMagnetAimTarget`, which grabbed the nearest civilian within 2800u regardless of aim
    (felt "random"). Removed that helper; both now shoot at the camera aim: latch to
    `targetHitResult.ImpactPoint` if the aim sweep hit, else reach to `TraceEnd`.
  * **Aim = line trace WITH radius:** `AChutlluCharacter::CameraTrace` sphere-sweep radius is now the
    tunable `AimTraceRadius` (EditAnywhere, default **50** = the requested 50 cm; was hardcoded 140).
    Anything within that radius of the aim line is latched.
- **2026-07-25 (tentacle smoothness — de-rubber the physics chain)** — User: tentacles stretch & jitter
  like rubber, jiggle too heavy, worst when held / at attack start / retracting (as the spline shortens).
  Root causes in the 25-sphere-per-tentacle constraint chain (`AChutlluTentacleRig`), all fixed + built clean:
  * **Chain self-collision** — spheres are `ECC_Destructible` and *blocked* `ECC_Destructible`, so all 25
    shoved each other whenever the chain compressed (idle/held/retract/short). Now they ignore their own
    object type (`bDisableChainSelfCollision`, `SetSphereParameters`); still block world geometry.
  * **No constraint projection** — added `SetProjectionEnabled` + `SetProjectionParams` in
    `SetConstraintParameters` (`bUseConstraintProjection`, `projectionLinear/AngularAlpha`). Snaps segments
    that drift past their joint back into place — the main cure for the elastic rubber-band stretch.
  * **Underdamped** — `physDamping_` 3 → 12 (now EditAnywhere linear+angular damping on the spheres).
  * **Too stretchy** — `stretchLimitScale` (0.45) scales down the authored linear stretch limits in
    `SetStretchLinearLimitsForTentacle` so segments separate less (flesh, not elastic).
  * **No substepping** — added `[/Script/Engine.PhysicsSettings]` bSubstepping=True (MaxSubstepDeltaTime
    0.0083, MaxSubsteps 6) to `Config/DefaultEngine.ini`; fixed-dt solve kills frame-rate-dependent jitter.
  All 5 knobs live under "Cthulhu | Physics" on the rig; physics knobs re-apply on tentacle regeneration
  (toggle Battle Mode / Z). Awaiting user in-editor feel test + tuning.
- **2026-07-25 (Phase 3 cont. — Proximity Auto-Targeting, Collision Toggling & Soldier Death Fix)** — Refined combat responsiveness and death physics:
  * **Proximity Auto-Targeting for Civilians**: Upgraded `GetSmartMagnetAimTarget` in `ChutlluTentacleComponent.cpp` to scan within 2800 units of Cthulhu automatically. Pressing attack or clicking automatically locks tentacles onto the nearest living civilian in range without needing to aim directly at them!
  * **Grab & Release Collision Toggle**: Disabled collision (`SetCollisionEnabled(ECollisionEnabled::NoCollision)`) on all held objects/characters when grabbed, and restored collision (`ECollisionEnabled::QueryAndPhysics`) and `CharacterMovement` upon release/drop/throw. Eliminates physics jitter and collision clipping during grabs.
  * **Soldier Death Teleport Fix**: Replaced massive `DeathImpulseStrength * Mass` impulse in `ChutlluEnemyBase.cpp` with a gentle velocity change (`AddImpulse(..., true)`). Soldiers now collapse naturally right where they die without superfast teleporting across the screen.
  * **Character Attachment Fix (No More Ground Floating)**: Automatically calling `DisableMovement()` and setting capsule collision to `NoCollision` on grab now firmly locks civilians directly onto the tentacle tip!
  * **140u Sphere Sweep Camera Trace**: Upgraded `CameraTrace()` from a thin 1-pixel ray trace to a 140-unit sphere sweep. Left-clicking anywhere near a civilian in crosshair view now instantly registers an aim hit!
  * **Null Pointer Crash Fix**: Fixed `EXCEPTION_ACCESS_VIOLATION` in `AttackHandTick` line 281 by adding strict `IsValid` checks for `hitResult_.Component` and `hitResult_.GetActor()`.
  * **Unhold/Drop Release Reset**: Updated `SetHandToReturn`, `Drop`, `DropAll`, and `Throw` to explicitly reset `bIsConsuming = false`, `ConsumeTimer = 0.f`, and `grabbedObject = nullptr`.
  * **Multi-Human UI Stack**: Rendered individual 3D world progress bars over each tentacle and a dedicated top-right HUD stack for all active digesting humans (`HUMAN DIGEST #X: YYs (ZZ%)`).
  * Built clean with 0 compilation errors.
- **2026-07-25 (Phase 3 cont. — Attack Helicopters, Parachute Supply Drops & Cthulhu Rage Transformation)** — Expanded aerial combat, supply events, and ultimate rage transformation:
  * **`AChutlluAttackHelicopter`**: Airborne military enemy hovering at high altitude (750u). Strafes Cthulhu with Gatling gun bursts and homing missile volleys with red warning laser lines. Integrated into Wave 3+ spawn tables.
  * **`AChutlluSupplyCrate`**: Parachute supply crates dropping from the sky on wave start. Grabbable/smashable by tentacles, granting 1 of 3 temporary power-ups (*Soul Magnet*, *Eldritch Shield +150 HP*, *Tentacle Frenzy*).
  * **Cthulhu Rage Transformation (`[R]` Key)**: `RageMeter` fills by devouring civilians (+10), destroying props (+5), and killing enemies (+8). Pressing `[R]` at 100% triggers **UNLEASHED FORM** for 15s (1.8x character scale expansion, zero ability cooldowns, purple aura pulse, screen shake roar).
  * **Rage HUD Bar**: Canvas HUD bar showing Rage % and interactive `[R] UNLEASH FORM` button.
  * Built clean with 0 compilation errors.
- **2026-07-25 (Phase 3 cont. — Active Eldritch Abilities, Cooldown HUD & Wave Balancing)** — Added active abilities, HUD indicators, and difficulty balancing:
  * **`[Q] Void Nova`**: 900-unit expanding energy ring dealing 120 base damage + knockback impulse to surrounding enemies & props, triggering screen shake and panic events.
  * **`[E] Eye Beam`**: Concentrated high-damage laser beam channeled for 2.5s along camera line-of-sight (200 DPS) with visual ray traces and impact flashes.
  * **`[F] Ground Slam`**: 1200-unit earth-shattering wave causing all tentacles to crush the ground, knocking up military forces and dealing 180 base damage.
  * **Ability Cooldown HUD**: Bottom-left canvas HUD readouts (`READY` / countdown timers) with hover/click button support for mouse activation.
  * **Wave Balancing**: Escalating spawn cadence, boss health budget scaling, and tuned soul economy.
  * Built clean with 0 compilation errors.
- **2026-07-25 (Phase 3 cont. — City Generation, Destructible Props, Crowd Panic & Audio Subsystem)** — Implemented environmental destruction, procedural city layout, crowd panic behavior, and dynamic audio:
  * **`AChutlluDestructibleProp`**: C++ actor for destructible city objects (cars, lamp posts, barricades, explosive barrels, crates). Equipped with `UChutlluHealthComponent` and tagged `"octo"`. Destructive hits/throws collapse props into physics debris, award souls & score, pop floating combat text (`SMASH!`), and trigger explosion AOE if explosive.
  * **`AChutlluCityManager`**: Procedural environment spawner that builds a city prop grid around the level and dynamically maintains crowd density. Exposes `NotifyPanicEvent` to trigger widespread panic across nearby civilians. Automatically spawned by `AChutlluGameMode::BeginPlay`.
  * **`UChutlluAudioSubsystem`**: `UWorldSubsystem` managing sound effect triggers (tentacle swing, civilian scream, consume gulp, prop destruction, explosions, boss slams) and adaptive music intensity based on wave state and boss waves. Integrates with Master/Music/SFX volume settings.
  * **`AChutlluCivilian`**: Added **Panic State** (`TriggerPanic`). Nearby combat, explosions, or consumes trigger panic screams and rapid fleeing at 1.8x speed away from danger zones.
  * **Interactive Mouse Buttons**: Added `DrawCanvasButton` to `AChutlluHUD` with mouse hover highlight boxes, golden borders, audio clicks, and direct left-click actions for all Start Menu options, Save Slots, Options Adjusters, Pause Menu, Intermission Draft cards, Void/Cave shop items, and Game Over screens.
  * **Crash Fix**: Fixed runtime assertion crash in `AChutlluDestructibleProp::SetupMeshForPropType()` where `ConstructorHelpers::FObjectFinder` was called inside `BeginPlay()`. Replaced with runtime `LoadObject<UStaticMesh>` calls.
  * Built clean with 0 compilation errors.
- **2026-07-25 (Phase 3 — Start Menu, Multi-Slot Save/Load Screen, Pause & Options)** — Expanded the prototype
  into an advanced, multi-screen game system (all C++, styled canvas fallback UI + UMG framework support):
  * **Start Menu / Title Screen**: Boots directly into an eldritch Title Menu (`[1]` New Run, `[2]` Save/Load Screen, `[3]` The Abyss Meta Shop, `[4]` Options & Controls, `[5]` Exit Game). Automatically manages mouse cursor and UI input mode.
  * **Multi-Slot Run Save/Load System**: Extended `UChutlluSaveGame` and `UChutlluMetaSubsystem` to manage 3 independent run save slots (`Slot 1`, `Slot 2`, `Slot 3`). Saves current wave, souls, score, player health/max health, timestamp, and owned passives. Restores run state, active passives, health, and wave progress upon load.
  * **In-Game Pause Menu**: Pressing `ESC` or `P` pauses world tick (`SetGamePaused`), brings up pause overlay (`[1]` Resume, `[2]` Save Run, `[3]` Load Run, `[4]` Options, `[5]` Main Menu).
  * **Options & Controls Menu**: Adjustable sliders/toggles for Master/Music/SFX Volume, Mouse Sensitivity (applied live to `Look` input), Screen Shake toggle, and full Controls Guide diagram.
  * Built clean with 0 compilation errors.
- **2026-07-24 (Phase 3 — Meta-Progression + Save/Load System)** — Built a persistent meta-progression
  system and save game structure across runs:
  * **`UChutlluSaveGame`**: `USaveGame` subclass holding `BankedSouls`, `PurchasedMetaUpgrades`, `TotalRunsCompleted`, and `BestScore`. Saves to slot `"ChutlluMetaSave"`.
  * **`UChutlluMetaSubsystem`**: `UGameInstanceSubsystem` managing persistence across level transitions. Converts 25% of souls collected in a run into persistent `BankedSouls`. Exposes 8 permanent meta-upgrades (*Ancient Flesh*, *Elder Blood*, *Void Claws*, *Deep-Sea Fury*, *Abyssal Stride*, *Soul Drinker*, *Regenerating Mass*, *Eldritch Speed*).
  * **`UChutlluUpgradeComponent`**: Reads permanent meta-upgrade stat additions and multipliers at `BeginPlay` and applies them to base stats prior to per-run passives.
  * **`AChutlluGameState`**: Hooks into GameOver state transitions to trigger `RecordRunEnd()`, calculating and storing banked souls and high scores.
  * **`AChutlluHUD`**: Extended GameOver screen with **THE ABYSS** meta-shop panel (dark purple/crimson theme). Shows run summary, score, banked souls earned, and 8 permanent upgrades (`[1-8]` keys to purchase, `[R]` to descend anew). Built clean (0 errors).
- **2026-07-24 (Phase 2 cont. — Cave vendor + synergy passives + shop refresh)** — Implemented the
  second intermission vendor and synergy passive system (all C++, no art assets). Changes built clean (0
  errors, 11/11 actions):
  * **`UChutlluPassiveEffect`**: `SynergyRequires` (passives that must be owned for mods to activate)
    and `bCaveItem` (routes passive into the Cave sub-pool vs Void sub-pool).
  * **`UChutlluUpgradeComponent`**: `GetStat` skips a passive when its synergy isn't met. `RollOffer`
    draws from Void or Cave sub-pool via `bCavePool` param. 5 new Cave passives seeded: *Hungering Void*
    (Legendary synergy: +50% soul gain if Soul Hunger + Devour owned), *Razor Feast* (Rare synergy:
    +30% dmg + 3 lifesteal if Barbed Tentacles owned), plus 3 non-synergy Cave physicals. `IsSynergyActive`
    is public/BlueprintPure so the HUD can query it.
  * **`UChutlluWaveManagerComponent`**: three-phase intermission: Draft → Void shop → Cave shop → Next
    Wave. `AdvanceToCave`/`BuyCaveItem`/`RefreshCaveShop`/`GetCaveRefreshCost` added. Both vendors have
    soul-cost refresh (`[T]`); cost doubles per use per intermission and resets each wave.
  * **`AChutlluHUD`**: `DrawVoidShop` (cyan/purple) + `DrawCaveShop` (amber/brown) panels; both show
    rarity star badges (★/★★/★★★) and synergy labels (orange = needs conditions, green = active).
    `PollIntermissionInput` handles all three phases.
- **2026-07-24 (gameplay layer — wave/soul/roguelike foundation)** — Started the actual game on top of the
  tentacle tech. New design doc: **Docs/GAMEPLAY_DESIGN.md** (pitch, core loop, architecture, phased TODO,
  editor wiring checklist). Implemented + built clean a full C++ foundation in the ChutlluSim module:
  `ChutlluGameTypes.h` (enums/structs), `UChutlluHealthComponent`, `AChutlluEnemyBase` + `Soldier/Tank/Plane`,
  `AChutlluCivilian` (+ `IChutlluConsumable`), `AChutlluGameState` (souls/score/wave + delegates),
  `UChutlluWaveManagerComponent` (on GameMode; spawns waves + ambient civilians, drives intermission draft),
  `UChutlluPassiveEffect` + `UChutlluUpgradeComponent` (roguelike stat aggregation), `UChutlluShopItem` +
  shop widget, and UI C++ bases (`AChutlluHUD`, HUD/enemy-health-bar/upgrade-select/shop widgets). Wired:
  tentacle attack now deals real damage (×TentacleDamage); consuming a pulled-in civilian grants souls
  (×SoulGain) + lifesteal; enemies chase/attack the player via tick-AI + AIController; player got Health +
  Upgrades, death → GameOver. GameMode sets GameState/HUD/DefaultPawn + owns the WaveManager. **Next: author
  the BPs/UMG/data assets per the GAMEPLAY_DESIGN.md editor-wiring checklist to make it playable.**
- **2026-07-24 (zero-asset playable pass)** — Made the game run with NO editor authoring: canvas HUD in
  `AChutlluHUD::DrawHUD` (souls/score/wave/enemy count + player health bar + on-screen enemy health bars +
  a 1/2/3 intermission draft panel + "YOU HAVE FALLEN / press R" restart). Enemies + civilians get engine
  basic-shape placeholder meshes (cylinder/cube/cone) so they're visible without skeletal meshes. Wave
  manager seeds 6 default waves + defaults CivilianClass; upgrade component seeds an 8-passive default pool.
  All C++ auto-detects UMG/BP overrides and steps aside if present. Built clean. Just press Play with
  `AChutlluGameMode` active (check the map has no GameMode override / no placed BP_OctoPlayer).
- **2026-07-24 (Phase 2 — juice + combat depth, all C++/no-asset)** — Added `UChutlluFeedbackSubsystem`
  (tickable world subsystem): floating combat text (damage numbers / +souls / +score), a kill+consume
  **combo** (chained within ~3.5s → up to x3 score multiplier), and **wave banners**; the HUD draws all of
  it. Enemies now **pop-scale + emit a damage number** on hit and use a **telegraphed wind-up** (`WindupTime`
  + "!" indicator, dodge window) before landing damage. Chutllu gets **trauma-based screen shake** on damage
  (shakes the FollowCamera, decays in Tick). **Boss** enemy (`AChutlluBoss`, huge/tanky) spawns every 5th
  wave with a "BOSS WAVE" banner; default waves extended to 8. Built clean.

- **2026-07-24 (Phase 2 cont. — projectiles + shop/reroll)** — `AChutlluProjectile` (moving sphere,
  damages only the player on overlap, ignores enemies, dies on world/lifespan). Enemy base gained
  `bRanged`/`ProjectileClass`/`ProjectileSpeed`; **tanks + planes fire dodgeable projectiles** at wind-up
  end, soldiers/boss stay melee. Draft `RollOffer` is now **rarity-weighted** (common>rare>legendary).
  Intermission is **two-step**: draft (with a **paid reroll**, cost scales per use) → **Void shop** (canvas;
  rarity-priced passives bought with souls) → press ENTER to start the next wave. Wave manager owns the
  draft/shop state (`IsDraftActive`/`IsShopActive`/`RerollOffer`/`BuyShopItem`/`StartNextWave`); HUD reads it
  directly and polls 1/2/3 + R + ENTER. Built clean.

- **2026-07-24 (crosshair + auto-deploy tentacles & grab at start)** — Added a center **crosshair** to the
  HUD (`AChutlluHUD::DrawCrosshair`, hidden on game over) since attack/grab aim from screen centre. Game now
  **auto-starts in battle mode** (tentacles deployed, `bStartInBattleMode`) via a 0.3s timer after BeginPlay,
  and then **auto-enables flying/grab mode** (the C mode) once the tentacles finish deploying — flying can
  only start when `bBattleMode && rig->bIsHandsShowed`, so the character polls `TentacleComponent->
  AreHandsReady()` (new) every 0.25s and flips it on when ready (`bStartInFlyMode`). Both are EditAnywhere
  toggles on the character; Z/C still toggle manually. Built clean.
- **2026-07-24 (fix: couldn't attack/grab enemies or civilians)** — Only physics objects responded to the
  tentacles. Cause: both `CameraTrace()` and the tentacle attack/grab sweep use `ECC_Visibility`, but the
  ACharacter "Pawn" collision profile ignores Visibility by default, so all traces passed through enemies +
  civilians. Fix: enemy + civilian capsule constructors now `SetCollisionResponseToChannel(ECC_Visibility,
  ECR_Block)`. Enemies now take tentacle damage; civilians (tagged "octo") get grabbed + consumed. Built clean.
  (Enemies are attack-to-kill, not grabbable — tagging them "octo" would strand a non-consumable in the hand;
  grab-and-throw for enemies is a possible follow-up.)

- **2026-07-24 (mesh-swap UX + root-follow regression fix)** — User couldn't change the tentacle meshes
  (SM_Cilynder_1/2, SM_Junction, SK_Hand) and reported socket-root body-follow broke.
  * **Mesh swap:** child-actor-template edits on the rig are unreliable, so exposed 4 override fields on
    the CHARACTER ("Cthulhu | Tentacle Meshes": TentacleInnerMesh/OuterMesh/JunctionMesh/HandMesh),
    forwarded to the rig in `AChutlluCharacter::BeginPlay` before it generates (non-null overrides win
    over the rig's auto-loaded defaults). Reqs: spline meshes must be +X, ~unit, pivot-at-end cylinders;
    the hand mesh needs a socket named exactly `Arrow_home` (grab attaches held objects there).
  * **Regression fix:** the earlier `RigClass` + `OnConstruction`→`SetChildActorClass` deferred the child
    actor respawn, so `RigChild->GetChildActor()` was null in BeginPlay → `SetRootAttachMesh` never ran →
    `rootAttachMesh` null → per-frame socket-root follow skipped (roots stopped following the body).
    Removed `RigClass`/`OnConstruction`; child actor class is set once in the constructor again (build-#2
    behavior). Mesh swapping no longer needs a rig BP, so RigClass wasn't needed anyway.

- **2026-07-24 (release glitch + taper + custom roots)** — Three changes, all built clean:
  * **Release glitch fix:** `Drop`/`DropAll` set `action=IDLE` in one frame (skipping the smooth
    `RETURNS` path), so the rigid held chain snapped → vibrate/wiggle + occasional broken-hand one-frame
    fly-off. Now they call `SetHandToReturn(...)` like `Throw` does (ChutlluTentacleComponent.cpp).
  * **Root-to-tip taper:** rig now thickens toward root, thins toward tip. New props under
    "Cthulhu | Taper": `rootThickness` (3), `tipThickness` (1), `bigMeshThickness` (1.1, replaces the old
    hardcoded 1.1), optional `taperProfileCurve`. `GetTaperAtRatio`/`ApplyTentacleTaper` set per-segment
    SplineMesh start/end scale (root=0..tip=1); re-applied every frame in Tick (survives rebuild paths) +
    at creation + hide-path; junction beads tapered too. Nub NOT scaled (would scale attached spheres).
  * **Custom per-tentacle roots:** new `FCthTentacleRoot{ FName Socket; FTransform RelativeTransform }`
    and rig `TArray<FCthTentacleRoot> tentacleRoots` ("Cthulhu | Roots"). Empty = old under-body ring.
    Per-index: Socket set → root grows from that character bone/socket and follows animation (rig gets the
    mesh via `SetRootAttachMesh`, set in `AChutlluCharacter::BeginPlay`); Socket None → manual transform
    relative to the capsule. `GetTentacleRootRelative` resolves these; anchor arrows now derive yaw from
    the root (so custom roots orient correctly); startHandMesh follows the socket each frame in Tick.
    Set `TentacleCount` to match the number of roots defined.


- **2026-07-24** — First pass. Explored full project + plugin C++. Documented architecture, identified
  the two hardcoded `for i<4` loops and the named-per-hand component structure as the barrier to N tentacles.
  Established plan direction (data-driven refactor + code-driven underneath roots + BP→C++ port).
  **Decisions:** 6 tentacles (configurable `TentacleCount`); BP→C++ port to follow user screenshots.
  **Identified blocker:** the per-tentacle Arrow anchor transforms live in the BP (unreadable here), so
  correct placement needs either those values or a code-driven-ring-tuned-in-editor approach (recommended).
  Nothing edited in code yet — next action is the Actor data-driven refactor.
- **2026-07-24 (build complete)** — Wrote C++ asset loading into rig+component constructors (all plugin
  assets by path), `AChutlluCharacter` (Manny mesh + spring-arm camera + tentacle component + rig as a
  ChildActor on the CAPSULE, full Enhanced Input + legacy-key port from BP_OctoPlayer), and `AChutlluGameMode`.
  Closed the editor and **built ChutlluSimEditor — succeeded on the first attempt** (only an unrelated MCP-plugin
  warning). Set `GlobalDefaultGameMode` to the new gamemode. Awaiting user's in-editor visual test/tuning.
- **2026-07-24 (startup crash fixed)** — User's editor crashed on open: EXCEPTION_ACCESS_VIOLATION in
  CoreUObject. Root cause: `ConstructorHelpers` in the new classes' constructors force-loaded OctopusBackpack
  plugin content during CDO construction, racing the plugin module's init → "/Script/OctopusBackpack" import
  failures → plugin BP CDOs null → crash. Fix: moved ALL cross-plugin asset loads out of constructors into
  lazy `LoadObject` calls at BeginPlay (rig `LoadRigAssets()`, component BeginPlay curves, character
  `LoadContentAssets()`). Rebuilt + verified headlessly (`-run=CompileAllBlueprints`): 0 errors, 0 CDO
  failures, 0 crash markers. Editor opens clean now. Lesson saved to memory (cross-plugin-asset-loading).
- **2026-07-24 (runtime bugs)** — User tested: (1) pressing Z launched the character endlessly upward
  (heavy tentacle spheres colliding with the now-under-capsule owner) — fixed by ignoring ECC_Pawn on the
  spheres; (2) retracting tentacles crashed in HideHandsTick (junction ISM RemoveInstance index assert) —
  fixed by removing index 0 clamped + guarding empty arrays. Rebuilt (succeeded). Awaiting user re-test.
- **2026-07-24 (fly movement + idle float)** — User: Z open/close works, C sticks to walls, but (1) WASD
  wrong in fly mode, (2) tentacles hang passively under instead of floating around. Traced BP_OctoPlayer's
  `OnOctopusReturnMovement` handler via MCP: it does AddMovementInput along control-yaw forward/right scaled
  by inScale — my port wrongly passed the raw local vector as a WORLD direction. Fixed HandleReturnMovement
  to decompose forward/right/up by control yaw. For the float: raised idle anchor offsets (flyOriginOffset
  ->(110,0,50), walkOriginOffset->(100,0,20)) so the per-tentacle ring reaches outward+up (float around the
  body) while roots stay under the capsule. Rebuilt (succeeded). Awaiting user re-test + anchor tuning.
- **2026-07-24 (idle liveliness + multi-grip)** — User feedback: idle reached at the camera (disliked),
  random laser fired for no reason, tentacles snapped static while moving; and fly-mode gripped with only
  ONE tentacle (released others on each new grip). Fixes in the component:
  * `GenerateIdleAnimateTransform`: removed the camera-reach branch and the random `EnableIdleEffect`
    (the mystery laser); roam-only with varied interp speed; removed the 5s world-time gate.
  * `IdleHandStateTick`: roam runs whenever `animIdleTime<=0` (dropped the `inputScale==0` gate) so
    tentacles keep floating alongside even while walking, instead of snapping to a static rest pose.
  * Multi-grip: added `maxAnchorHands` (default 3). On attach, keep up to N gripping and release only the
    oldest excess (by new `timeAttached`); and the fly-seek now also triggers while `attachedCount < N`,
    so 2-3 tentacles proactively hold on. Rebuilt (succeeded). Awaiting user re-test.
  * Idle "floating" = random target roam (this code) + the tip SK_Hand anim BP animating the tip shape.
    If tips still look stiff, the tip anim BP state isn't being driven (the old BlueprintImplementableEvents
    are C++ no-ops) — next step if needed.
- **2026-07-24 (airborne regression)** — Multi-grip regression: character glided to the ground while
  attached in the air. Cause: the break-distance check dropped flying mode (`bIsFlyingModeActivate=false`)
  whenever ANY single attached hand stretched past `breakDistance`; with 2-3 spread grips that fired
  constantly, toggling gravity on/off. Fix: on break, release just that hand (decrement attachedCount) and
  drop flying mode ONLY after the loop when `attachedCount<=0` (fully un-anchored). Rebuilt (succeeded).
- **2026-07-24 (tuning)** — Doubled `ringRadius` (25->50) for wider root spacing. Added `tentacleSpeedScale`
  (default 0.5) on the component: multiplies every curve-time step (grip/attack/return/throw/falling) and
  the idle interp rate, halving overall tentacle motion speed. Both live-tunable. (Tip SK_Hand anim BP
  playrate is separate, not covered by this knob.)
- **2026-07-24 (cont.)** — Confirmed UnrealMCPServer live access (editor open). Measured via Python bridge:
  rig attaches to `CharacterMesh0` socket **`spine_05`** (hence "behind" + skeleton-bound); captured all
  Arrow anchor transforms (see Measured facts). Decided design direction with user: **fresh C++ classes**
  (new actor+component, originals kept as reference), **attach to CAPSULE not skeleton**, keep tip SK_Hand
  mesh/anim/curves + physics math unchanged. Awaiting user go-ahead to start building the new classes.
