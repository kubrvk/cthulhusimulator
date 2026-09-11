# Chutllu Sim — Gameplay Design & Build Plan

> Companion to DEVELOPMENT.md. DEVELOPMENT.md = the tentacle creature tech. This file = the *game* on
> top of it: waves, souls, enemies, roguelike passives, shop, UI.
> Last updated: 2026-07-28

## 1. The pitch

You are **Chutllu**, an eldritch horror that has surfaced beneath a modern city. Using your
procedurally-simulated tentacles you **grab, throw, crush, and consume** the population. Consuming humans
yields **Souls** — the game's single currency. Souls are spent in the **Void** (upgrades from beyond) and
the **Cave beneath the city** (physical/relic upgrades) to grow stronger.

The city fights back. **Waves** of escalating military force arrive — soldiers, then tanks, then aircraft —
each with health bars, each dealing damage to you. Survive the wave, then **draft one of three passives**
(roguelike / wave-rush style). Repeat, scaling both the threat and your power, until you either fall or
raze the city.

Core fantasy: *power growth through consumption*. Two pressures in tension — **feed** (chase civilians for
souls/score) vs. **fight** (the army interrupts your feeding). Passives let each run branch differently.

## 2. Core loop

```
        ┌─────────────────────────────────────────────────────────────┐
        │  WAVE (InProgress)                                           │
        │   • Enemies spawn over time (soldiers→tanks→planes)         │
        │   • Civilians roam the streets                              │
        │   • You consume civilians  → +Souls                        │
        │   • You kill enemies       → +Souls (+ score)              │
        │   • Enemies damage you     → your Health drops             │
        │   • Wave ends when quota cleared / timer done              │
        └───────────────┬─────────────────────────────────────────────┘
                        ▼
        ┌─────────────────────────────────────────────────────────────┐
        │  INTERMISSION                                               │
        │   • Draft: pick 1 of 3 random passives (roguelike)         │
        │   • Optional: open Void / Cave shop, spend Souls           │
        │   • "Begin next wave" when ready                           │
        └───────────────┬─────────────────────────────────────────────┘
                        ▼   (loop, difficulty scales)
                   Death → run summary → meta (later)
```

## 3. Systems (architecture)

All gameplay lives in the **ChutlluSim game module** alongside the creature classes. Single-player, so the
GameMode/GameState hold authority; no networking concerns for v1.

| System | Class | Notes |
|---|---|---|
| Shared types | `ChutlluGameTypes.h` | enums (`EChutlluEnemyType`, `EChutlluWaveState`, `EChutlluStat`, `EChutlluStatOp`, `EChutlluTeam`) + structs (`FChutlluEnemySpawn`, `FChutlluWaveDefinition`, `FChutlluStatModifier`). |
| Health/damage | `UChutlluHealthComponent` | Max/Current, `OnHealthChanged`, `OnDied`. Binds engine `OnTakeAnyDamage`. Reused by enemies **and** the Chutllu player. |
| Player health | on `AChutlluCharacter` | a `UChutlluHealthComponent`; death → game over. |
| Enemies | `AChutlluEnemyBase` (+ variants below) | health, simple tick-AI (chase + attack in range), turn-to-face while shooting, soul reward, floating health-bar widget. Planes fly. |
| Voyager mobs | `AChutlluVoyagerEnemy` + 13 variants | Voyager meshes/anims/weapons driven by our native AI; random weapon loadout per spawn. See DEVELOPMENT.md. |
| ArmyVFX vehicles | `Tank`/`Plane`/`AttackHelicopter`/`APC`/`Artillery`/`AAGun`/`MLRS`/`SPG` | ArmyVFX meshes + per-vehicle muzzle flash, shell mesh, smoke trail, ground splash, explosion. |
| Civilians | `AChutlluCivilian` | roam/flee; grabbed+pulled-in → consumed → souls. |
| Economy + run state | `AChutlluGameState` | `Souls`, `CurrentWave`, `WaveState`, `Score` + delegates; `AddSouls`/`SpendSouls`. |
| Wave director | `UChutlluWaveManagerComponent` (on GameMode) | data-driven wave list; spawns over time around the player; tracks alive; intermission; scales. |
| Roguelike passives | `UChutlluPassiveEffect` (DataAsset) + `UChutlluUpgradeComponent` (on player) | passives = lists of `FChutlluStatModifier`; component aggregates into a live stat block the game reads. |
| Shop (Void/Cave) | `UChutlluShopItem` (DataAsset) + logic on GameState/Upgrade comp | spend souls for items/passives; two vendors differ by pool + theme. |
| HUD | `AChutlluHUD` + `UChutlluHUDWidget` | souls/wave/health readouts. |
| UI widgets | `UChutlluEnemyHealthBar`, `UChutlluUpgradeSelectWidget`, `UChutlluShopWidget` | C++ bases expose data + `BlueprintImplementableEvent`s; **visuals authored in UMG** (Blueprint subclasses). |

### Stat model (how passives affect play)
A small, extensible enum `EChutlluStat`: `TentacleDamage`, `AttackSpeed`, `MoveSpeed`, `MaxHealth`,
`SoulGain`, `TentacleReach`, `TentacleCount`, `Lifesteal`. A passive/shop item carries
`FChutlluStatModifier { EChutlluStat Stat; EChutlluStatOp Op(Add/Mult); float Value; }`. The player's
`UChutlluUpgradeComponent` recomputes an aggregate `GetStat(EChutlluStat)` whenever passives change; combat
code (tentacle damage, movement, soul rewards, health) reads through it. Adding a new passive = new DataAsset,
no code.

## 4. Comprehensive TODO (phased)

### Phase 0 — Foundations (DONE — all compiles)
- [x] Design doc (this file).
- [x] `ChutlluGameTypes.h` — enums + structs. Added **UMG** to Build.cs.
- [x] `UChutlluHealthComponent` (+ tentacle attack now deals real damage scaled by TentacleDamage).
- [x] `AChutlluEnemyBase` + `Soldier`/`Tank`/`Plane`; floating health bar push wired.
- [x] `AChutlluCivilian` (tagged "octo" so tentacles grab it; consume-on-pull-in → souls).
- [x] `AChutlluGameState` (souls/score/wave/wave-state + delegates).
- [x] `UChutlluWaveManagerComponent` (spawn/track/advance + ambient civilians) on GameMode.
- [x] `UChutlluPassiveEffect` + `UChutlluUpgradeComponent` (draft 3, aggregate mods, stat reads).
- [x] `UChutlluShopItem` + `UChutlluShopWidget` (buy = spend souls → grant passive).
- [x] UI C++ bases: `AChutlluHUD`, `UChutlluHUDWidget`, `UChutlluEnemyHealthBar`,
      `UChutlluUpgradeSelectWidget`, `UChutlluShopWidget`.
- [x] Player (`AChutlluCharacter`) got Health + Upgrades; MoveSpeed/MaxHealth from passives; death → GameOver.

### Playable NOW with zero authored assets
The C++ seeds its own defaults so you can just press Play (as long as the active GameMode is
`AChutlluGameMode` — check World Settings has no override and no `BP_OctoPlayer` is placed):
- **Tentacles auto-deploy at start** (battle mode) and **grab/fly mode auto-enables** once they're out
  (`bStartInBattleMode` / `bStartInFlyMode` on the character) — no need to press Z/C first.
- A **center crosshair** marks where attacks/grabs aim (the trace fires from screen centre).
- **HUD** is drawn on the canvas (souls / score / wave / enemy count + a player health bar) — no UMG needed.
- **Enemies** spawn each wave as placeholder shapes (soldier = cylinder, tank = cube, plane = cone, boss =
  big cube) with **floating health bars**; they chase you, **telegraph** (wind-up "!") then hit, and tanks/
  planes fire **dodgeable projectiles**. Hit them and damage numbers pop.
- **Civilians** roam as cylinders; attack one to grab + consume it on pull-in → souls (with combo).
- **Waves** auto-start ~2s after Play (8 escalating waves; **boss every 5th**). Clear one → **draft** (pick
  **1/2/3**, or **R** to reroll for souls) → **Void shop** (buy with **1/2/3**, **ENTER** for next wave).
- **Death** → "YOU HAVE FALLEN" overlay, press **R** to restart.
Assign the UMG/BP versions later (below) to replace the canvas fallback — the C++ auto-detects and steps aside.

### Controls (current build)
| Input | Action |
|---|---|
| WASD / mouse | move / look |
| Space | jump |
| LMB | attack (damages enemies; grabs + consumes civilians) |
| RMB | throw all held |
| 1–4 | attack with a specific tentacle |
| Z | toggle battle mode (tentacles) — on by default at start |
| C | toggle fly/grab mode — on by default once deployed |
| mouse wheel | zoom |
| **Intermission** | draft: 1/2/3 pick, R reroll · shop: 1/2/3 buy, ENTER next wave |
| **Game over** | R restart |

### Editor wiring checklist (optional — replace placeholders with real art/UMG)
The C++ is the engine; these author the content it drives. Nothing here needs code.
1. **GameMode BP** — create `BP_ChutlluGameMode` (parent `AChutlluGameMode`). On its `WaveManager`
   component fill `Waves` (each wave → spawn entries of the enemy classes below), set `CivilianClass`.
   Set this GameMode on your map (World Settings) or as project default.
2. **Enemy BPs** — create `BP_Soldier`/`BP_Tank`/`BP_Plane` (parents `AChutlluSoldier`/`Tank`/`Plane`),
   give each a mesh + anim; assign `WBP_EnemyHealthBar` to the `HealthBar` widget component. Reference
   these BPs in the wave spawn tables.
3. **Civilian BP** — `BP_Civilian` (parent `AChutlluCivilian`) + a human mesh.
4. **UMG** — `WBP_HUD` (parent `UChutlluHUDWidget`; bind souls/score/wave/health/enemy-count events),
   `WBP_EnemyHealthBar` (parent `UChutlluEnemyHealthBar`; ProgressBar → `Percent`),
   `WBP_UpgradeSelect` (parent `UChutlluUpgradeSelectWidget`; build 3 cards from `Offer`, buttons →
   `ChooseIndex`), optional `WBP_Shop` (parent `UChutlluShopWidget`).
5. **HUD BP** — `BP_ChutlluHUD` (parent `AChutlluHUD`); set `HUDWidgetClass=WBP_HUD`,
   `UpgradeWidgetClass=WBP_UpgradeSelect`. Set it as the GameMode's HUD class (or leave the C++ default and
   just leave the widget classes null until the UMG exists).
6. **Passives** — create 8–12 `UChutlluPassiveEffect` assets (e.g. "+25% Tentacle Damage",
   "+1.5 Lifesteal", "+20% Move Speed"); add them to the player's `Upgrades` component `PassivePool`
   (on `AChutlluCharacter`/a player BP). Draft appears automatically each intermission.
7. **Ground** — the wave manager traces down for spawn points, so the map needs `WorldStatic` ground.

### Phase 1 — Make it playable (in-editor authoring, needs UMG assets)
- [ ] Author UMG: `WBP_HUD`, `WBP_EnemyHealthBar`, `WBP_UpgradeSelect`, `WBP_Shop`, bound to the C++ bases.
- [ ] Author enemy Blueprints (meshes/anim) on the 3 C++ enemy classes; civilian BP.
- [ ] Author 8–12 `PassiveEffect` data assets + 6–10 `ShopItem` assets (Void/Cave pools).
- [ ] `BP_ChutlluGameMode` with a real wave list; set as map GameMode.
- [ ] Player death → game-over screen + restart.

### Phase 2 — Depth & feel  (juice + combat depth DONE in C++; art/VFX still TODO)
- [x] **Feedback subsystem** (`UChutlluFeedbackSubsystem`): floating combat text (damage numbers,
      "+souls", "+score"), **combo** (chained kills/consumes → up to x3 score multiplier), **wave banners**.
- [x] **Damage numbers + hit reactions**: enemies pop-scale + spit a damage number on hit.
- [x] **Telegraphed attacks**: enemies pause and wind up (`WindupTime`, "!" telegraph) before the hit —
      a real dodge window.
- [x] **Screen shake** on Chutllu taking damage (trauma-based, code-only).
- [x] **Boss wave every 5th wave** (`AChutlluBoss` — huge, tanky, big payout) + "BOSS WAVE" banner.
- [x] **Ranged enemies + projectiles** (`AChutlluProjectile`): tanks & planes now fire dodgeable
      projectiles at the end of their wind-up; soldiers stay melee; boss slams.
- [x] **Passive rarity weighting** (common > rare > legendary in the draft) + **paid reroll**.
- [x] **Intermission shop** (canvas): after the draft, a Void shop offers rarity-priced passives to buy
      with souls; press ENTER to start the next wave (two-step intermission).
- [x] Consume feedback: pull-in animation + soul burst VFX + screen flash (C++ code-only, no art needed).
- [x] Second vendor (Cave) + item tooltips/refresh; synergy passives.
- [x] Enemy death ragdoll + shrink-dissolve (Manny physics asset; static mesh tumble fallback).

### Phase 3 — Meta & polish
- [x] Meta-progression between runs (permanent unlocks bought with banked souls, USaveGame, THE ABYSS meta shop).
- [x] Start Menu / Title Screen system (canvas fallback + UMG base classes, boot into title menu).
- [x] Multi-Slot Save & Load System (Slot 1, 2, 3 active run state persistence & management).
- [x] In-Game Pause Menu (`ESC`/`P` key, game tick pausing, resume, save/load, options, exit).
- [x] Options & Controls Menu (Master/Music/SFX volume controls, Mouse Sensitivity, Screen Shake toggle, Controls diagram).
- [x] City generation / destructible props / crowd density (`AChutlluCityManager`, `AChutlluDestructibleProp`, civilian panic flocking).
- [x] Audio pass & music intensity (`UChutlluAudioSubsystem`, dynamic volume/pitch intensity scaling).
- [x] Active Eldritch Abilities (`[Q]` Void Nova, `[E]` Eye Beam, `[F]` Ground Slam with Cooldown HUD & clickable buttons).
- [x] Military Attack Helicopters & Parachute Supply Drops (`AChutlluAttackHelicopter`, `AChutlluSupplyCrate` with Soul Magnet, Eldritch Shield, Tentacle Frenzy).
- [x] Cthulhu Rage Meter & Unleashed Form (`[R]` key, 1.8x scale expansion, zero ability cooldowns, purple aura, audio roar).
- [x] Multi-Tentacle Civilian Grab & Timed Consume Progress Bar System (1 Soul per military kill vs 35 Souls per consumed civilian; 1.8s digest timer locks holding tentacle from attacking).
- [x] Balancing pass (spawn curves, soul economy, passive power budget).

### Phase 4 — Real army content (2026-07-28)
- [x] **Voyager mob roster in native C++** — 13 types (rifleman/trooper/sniper/heavy gunner/shotgunner/
      flamer/electric gunner/rocket trooper/gruntling ×2/spiderling/spider boss/minibot). Voyager art,
      our AI. Soldiers use `SK_ShadowOps`, not the grey mannequin.
- [x] **Aiming** — Voyager's `AnimBP_Soldier` inputs (`IsAiming`/`AimPitch`/`Speed`/`Direction`) driven
      from C++ by reflection, plus enemies now turn to face their target while firing.
- [x] **Random weapon loadouts** — each soldier rolls assault rifle / heavy MG / shotgun / plasma, which
      re-scales its damage, cadence and range.
- [x] **Real projectiles + VFX + SFX** — Voyager trails/impacts/muzzle flashes and ArmyVFX shells, with
      per-weapon fire, impact and death sounds. (Required switching all projectile spawns to
      `SpawnActorDeferred` — see DEVELOPMENT.md.)
- [x] **Five more ArmyVFX vehicles** — APC, artillery, AA gun, MLRS, SPG.
- [x] **All 20 enemy types from wave 1**, counts scaling with level; `SpawnBatchSize` so squads arrive
      together (~30 enemies wave 1 → ~90 wave 8).
- [x] **Music + ambience** actually playing (the music component was never being created).
- [ ] **Live play verification** — not yet done for any of the above.

## 5. Data-driven knobs (so tuning ≠ recompiling)
- Wave list, per-wave spawn tables, spawn cadence, intermission length → `WaveManagerComponent` (BP_GameMode).
- Enemy stats (health/speed/damage/range/soul reward/flying) → per enemy class defaults / BP.
- Passives & shop items → DataAssets.
- Player base stats & death behavior → `AChutlluCharacter` defaults.

## 6. Integration with existing tentacle tech
- **Dealing damage:** tentacle `AttackHandTick` already calls `OctopusDamageActor(actor)` on a hit. We
  route that call site through engine `UGameplayStatics::ApplyDamage`, scaled by the player's
  `TentacleDamage` stat, so any actor with a `HealthComponent` takes damage. (`OctopusDamageActor` stays as
  a BP event for extra FX.)
- **Consuming civilians:** civilians are grabbable primitives. When a civilian becomes `GRABBED` (pulled to
  the body) it is consumed: award souls (× `SoulGain`), destroy it, fire consume FX.
- **Passives that touch the rig** (TentacleCount/Reach) feed the rig's existing `TentacleCount` / anchor
  offsets; most passives are pure stat reads and need no rig change.

## 7. Open questions (decide as we build)
- Player health: single bar, or regen by feeding (lifesteal-on-consume)? *Leaning: no passive regen; consume = small heal.*
- Wave end condition: kill-quota, survive-timer, or both? *Leaning: clear all spawns, with a soft timer that force-spawns stragglers.*
- Do civilians count toward wave completion? *Leaning: no — civilians are the soul economy, enemies are the wave.*
- Shop timing: intermission-only, or also mid-wave via a "descend to cave" beat? *Leaning: intermission-only for v1.*
