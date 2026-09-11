# TIME SOUL

<img align="left" width="40%" src="https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/7892565cfcc766ed30ac1692a887a7e3f544b703/library_header.jpg?t=1731513472"/>
<h3> <a href="https://store.steampowered.com/app/2928270/TIME_SOUL/"><img src="https://img.shields.io/badge/Steam: https://store.steampowered.com/app/2928270/TIME_SOUL-000000?style=flat-square&logo=steam&logoColor=white" height="25"/> </a></h3>

![](https://img.shields.io/badge/Action-a13636?style=) ![](https://img.shields.io/badge/Souls--like-a17736?style=) ![](https://img.shields.io/badge/Parkour-33488d?style=) ![C++](https://img.shields.io/badge/C++-00599C?style=logo=c%2B%2B&logoColor=white)  ![C++](https://img.shields.io/badge/Unreal_Engine_5.1-0E1128?style=for-the-badges&logo=unrealengine&logoColor=white)  ![C++](https://img.shields.io/badge/Status-Shipped-success?style=for-the-badges) 
<br>
TIME SOUL is a souls-like action-platformer built entirely in Unreal Engine 5.1 using C++. The game features a generative world structure, a real-time countdown resource system(timeas health), multi-layered parkour movement, and a hybrid class framework.

The game ships on PC (Windows/ ) via Steam. The core design challenge was integrating a persistent 60-minute global timer as the primary resource , governing leveling, ability usage, death penalty, and world progression , while maintaining responsive, frame-accurate combat and traversal.
<br clear="left"/>
<p align="center">
<img src="https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_5e5adf2391616e017a65df159088f682db3acefd.800x600.jpg" width="25%"/><img src="https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_9c2181850a7147b47d3e10dfa693be97a084799b.800x600.jpg" width="25%"/><img src="https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_5da280e17d293d889b79b6b18ec71fd02b549018.800x600.jpg" width="25%"/><img src="https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_3e5a805e4134ec1816939b3d360962a3b9411569.1920x1080.jpg?t=1773568111" width="25%"/>
</p>

---

## Technical Details:

| Layer | Technology |
|---|---|
| Engine | Unreal Engine 5.1 |
| Primary Language | C++ (gameplay, AI, systems) |
| Scripting / Prototyping | Unreal Blueprint (visual scaffolding only) |
| Rendering | Lumen (GI), Nanite (static meshes), custom post-process materials |
| Physics | Chaos Physics , used for gravity manipulation, climbing normals, hook swing |
| Platform | PC (Win64/ ), Steam SDK |
| 3D Pipeline | ZBrush → Maya → Substance Painter → UE5 |
| Shader Authoring | UE Material Editor +   custom nodes |

---

## Code Overview

The codebase is structured around a component-driven architecture. Core systems are implemented as `UActorComponent` subclasses attached to the player `ACharacter`, with event-driven communication through delegates and a lightweight game event bus.

```
TimeSoul/
├── Source/
│   ├── Core/
│   │   ├── TimeSoulCharacter.h/.cpp          # Base player class
│   │   ├── TimeSoulGameMode.h/.cpp           # World/session management
│   │   └── TimeSoulGameState.h/.cpp          # Shared session state, clock sync
│   ├── Systems/
│   │   ├── TimeManager/                       # Global timer, clock resource logic
│   │   ├── CombatSystem/                      # Weapon slots, attack chains, stagger
│   │   ├── MovementExtension/                 # Parkour, glide, climb, gravity hook
│   │   ├── AIController/                      # Boss AI, mob patterns, state machines
│   │   ├── ProceduralGen/                     # World/platform seed generation
│   │   ├── EquipmentSystem/                   # Weapon slots, spells, tools, armor
│   │   ├── RPGStats/                          # Attribute scaling, leveling, perks
│   │   ├── SaveSystem/                        # Serialized save/load, checkpoint logic
│   │   └── NetworkSubsystem/                  # Steam session, replication layer
│   ├── UI/
│   │   ├── ClockHUD/                          # Radial clock UI, stat rings
│   │   └── Menus/                             # Character customization, inventory
│   └── Tools/                                 # Editor utilities, pipeline automation
```

---

## Core Systems:

### 1. Time Resource System

The central design pillar of TIME SOUL is treating time as the universal resource , replacing conventional health/mana bars. This is implemented via a global `UTimeManagerComponent`.

**Design:**
- Session is capped at 3600 seconds (60 minutes). The global timer decrements in real-time.
- Time functions simultaneously as: remaining session length, currency for abilities, death penalty source, and leveling resource.
- Six collectable Clock types each modify timer behavior via a `FClockModifierStruct` applied to the component:

| Clock | Effect | Implementation |
|---|---|---|
| Golden | +60 min flat grant | `AddTime(3600.f)` |
| Soul | ×2 multiplier on all time gains | `TimeGainMultiplier = 2.f` |
| Power | Deaths no longer deduct time | `bDeathCostsTime = false` |
| Light | Global time progression at ×0.5 rate | `TimeDrainRate = 0.5f` |
| Energy | Grail (stamina-restore) usage free | `bGrailCostsTime = false` |
| Dark | Enables 5× angel activations, no hook limit | `AngelUseLimit = 5; bHookLimited = false` |

**Death Penalty:**
- Default: -300 seconds on death. Timer pickups appear at death location (reclaim window).
- `TimelessMode`: if session timer reaches 0, world-end sequence triggers , `GameMode` broadcasts `OnWorldEnd` delegate, triggering a final boss encounter and end-state.

**Clockworks:**
- Static structures located above each planet. On approach, player is teleported to last active Clockwork (5-min cost). On death with no time remaining, auto-return in 60 seconds.
- Implemented as `AClockworkActor` with overlap detection and async teleport using `UGameplayStatics::OpenLevelBySoftObjectPtr` with state preservation.

---

### 2. Procedural World Generation
![image](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_5e5adf2391616e017a65df159088f682db3acefd.800x600.jpg)

Worlds and platforms are generated using a seeded procedural system.

- Each run is initialized with a `int32 GlobalSeed` passed at `GameMode::BeginPlay`.
- Platform layouts, enemy spawn tables, loot tables, and boss variants are all driven by `FRandomStream` instances seeded from `GlobalSeed + PlanetIndex`.
- Planet biomes are defined in `UDataTable` rows (`FPlanetBiomeRow`) referencing modular mesh sets, material overrides, and encounter configs.
- Moons are procedurally placed as child actors with guaranteed legendary loot spawns, seeded separately.
- Enemy variants and boss phase tables are selected from weighted arrays using `FWeightedRandomSampler`.

```cpp
// Simplified seed-based platform spawner
void APlanetGenerator::GeneratePlatforms(int32 Seed)
{
    FRandomStream Stream(Seed);
    for (int32 i = 0; i < PlatformCount; ++i)
    {
        FVector Offset = FVector(
            Stream.FRandRange(-Radius, Radius),
            Stream.FRandRange(-Radius, Radius),
            Stream.FRandRange(MinHeight, MaxHeight)
        );
        SpawnPlatformAt(Offset, Stream.RandRange(0, PlatformMeshes.Num() - 1));
    }
}
```

---

### 3. Movement Extension System

Parkour movement is implemented as a `UMovementExtensionComponent` extending `UCharacterMovementComponent`. Each traversal ability is a discrete state with entry/exit conditions and stamina/time cost checks.

**Implemented Traversal Abilities:**

| Ability | Implementation Notes |
|---|---|
| **Glide** | Custom air-control override; applies upward drag force while stamina > 0; stamina-drain per tick |
| **Wall Run** | Detects flat vertical surfaces via line traces; overrides gravity; applies lateral movement force; exits on stamina depletion or surface loss |
| **Gravity Climb** | Surface normal detection via sphere sweep; re-orients character capsule to surface normal using `FQuat::Slerp`; works on angled geometry |
| **Gravity Hook** | Physics impulse-based pull toward target actor; 1-minute time cost per activation; target detection via sphere trace with `ECC_GameTraceChannel` |
| **Bunny Hop** | Jump buffering within a frame window; successive hops accumulate speed up to a capped velocity |
| **Star Surf** | Speed and time bonus on angled surfaces , slope angle evaluated via dot product against world up; bonus scales linearly with angle delta |
| **Angel Mode** | Flight + sonic dash; activates time drain at ×10 rate; costs 5 minutes on entry; max concurrent activations governed by Dark Clock modifier |

**Character Movement Override (key excerpt):**
```cpp
void UMovementExtensionComponent::TickComponent(float DeltaTime, ...)
{
    Super::TickComponent(DeltaTime, ...);

    if (bIsGliding && CharacterOwner->GetCharacterMovement()->IsFalling())
    {
        FVector Velocity = CharacterOwner->GetVelocity();
        Velocity.Z = FMath::FInterpTo(Velocity.Z, GlideTerminalVelocity, DeltaTime, GlideDragCoefficient);
        CharacterOwner->GetCharacterMovement()->Velocity = Velocity;
    }

    if (bIsWallRunning)
    {
        UpdateWallRunMovement(DeltaTime);
    }
}
```

---

### 4. Combat System
![image](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_9c2181850a7147b47d3e10dfa693be97a084799b.800x600.jpg)

Combat is built on a dual-mode framework: **Melee (right-hand)** and **Ranged (left-hand)**, switchable in real-time. Up to 6 weapons total are equippable (3 per mode). The combat system uses a `UCombatComponent` managing attack chains, cooldowns, hit detection, and stagger state.

**Weapon & Slot Architecture:**
- `FWeaponSlot` struct stores weapon asset reference, attack montage array, damage profile, and type tag (`Melee` / `Ranged` / `Magic`).
- Weapon switching triggers a blended animation transition via `UAnimMontage` with a slot-specific blend mask.
- Shields only equip alongside one-handed weapons , enforced via `EWeaponHoldType` enum check at equip time.

**Attack Chain (Combo System):**
- Combo sequences are authored as ordered `TArray<UAnimMontage*>` per weapon.
- Input buffering: attack input within the last N frames (configurable per weapon) queues the next combo step.
- Chain advancement is event-driven: `AnimNotify_ComboWindow` opens the buffer; `AnimNotify_ComboEnd` resets if no input received.

**Stagger System:**
- Each enemy and the player has a `FStaggerState` struct tracking `CurrentStagger` and `MaxStagger`.
- Hits accumulate stagger. On threshold breach: stagger animation plays, brief control lock applied, stagger resets.
- **Guard-Parry**: blocking within a defined input window on enemy attack triggers a parry , applies full stagger to attacker, no damage taken.

```cpp
void UCombatComponent::ApplyStagger(float StaggerAmount)
{
    StaggerState.CurrentStagger += StaggerAmount;
    if (StaggerState.CurrentStagger >= StaggerState.MaxStagger)
    {
        TriggerStaggerAnimation();
        StaggerState.CurrentStagger = 0.f;
    }
}
```

**Lock-On System:**
- Soft lock-on: iterates visible enemies within cone angle, selects closest to screen center.
- Hard lock-on: camera rig lerps to maintain target in view; movement input remapped to target-relative space.
- Target switch: directional input while locked triggers next-target evaluation.

**Air Combo System:**
- On launching an enemy or entering airborne state, `bInAirCombo` flag activates.
- Air combo moves are a separate montage set; gravity is reduced during air combo window to extend hang time.
- Hits trigger hit-stop via a brief `CustomTimeDilation` reduction on both player and target actors.

**TIMESTEAL Mechanic:**
- 3 attack variants that drain time from enemies of matching clock type (Golden/Light, Powered/Energy, Souls/Dark).
- `AEnemy` base class exposes `FClockType ClockAffiliation`; on hit, `UCombatComponent` checks affiliation match and calls `TimeManager->AddTime(StealAmount)`.

**Cosmic Bullets:**
- Ranged projectiles with a random roll per-hit: either deal standard damage or steal time from target.
- Implemented via `FGameplayTagContainer` on projectile , hit behavior resolved at `UProjectileComponent::OnHit`.

---

### 5. RPG Stat & Leveling System
![image](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/ss_5da280e17d293d889b79b6b18ec71fd02b549018.800x600.jpg)

Stats are structured around 9 primary attributes modeled after a classic ARPG framework:

| Attribute | Primary Effect |
|---|---|
| Vigor | Max Health scaling |
| Endurance | Stamina pool and regen rate |
| Strength | Physical damage scaling |
| Dexterity | Attack speed, bleed scaling |
| Intelligence | Magic damage, spell power |
| Mind | Mana pool |
| Faith | Miracle/holy scaling |
| Arcane | Item discovery, status effect potency |
| Souls | Resistance and perk slot expansion |

- Leveling occurs at Clockworks: accumulated time is spent to increase one attribute per level.
- Scaling curves are defined in `UCurveFloat` assets per attribute; damage formulas reference these at runtime.
- **Perks**: passive buffs unlocked at stat thresholds, displayed as icons around the clock HUD ring.

---

### 6. Equipment System

```
Tools       → Up to 10 equippable active items
Weapons     → 6 total (3 melee, 3 ranged)
Spells      → 5 total equippable; 2 active at once
Armor       → 5 piece slots
Fairies     → Up to 4; grant passive bonuses
Grails      → Stamina restore consumables; limited uses, refill at Clockworks
Pouch       → Secondary slot for tool quick-access
```

- All equipment is defined via `UItemDataAsset` with stats, type tags, mesh references, and effect configs.
- Equipment changes are replicated for co-op via `UPROPERTY(Replicated)` on `UEquipmentComponent`.
- **Item Burn**: converts item to time currency at 25% item value. Implemented as a context action on any inventory slot.

---

### 7. Boss AI System

Boss behaviors are implemented as hierarchical state machines using Unreal's Behavior Tree system, extended with custom `UBTTask` and `UBTDecorator` nodes in C++.

Each boss (`ABossCharacter`) has:
- **Phase transitions**: health/time thresholds trigger `OnPhaseChange` delegate; new BT subtree injected dynamically.
- **Bullet-hell patterns**: spawned via `ABulletPatternActor` , pattern definitions stored in data assets (`FBulletPatternConfig`), with configurable projectile count, spread, rotation speed, and time-steal probability per projectile.
- **Randomized variants**: boss stats, pattern sets, and cosmetic variants are selected from seeded tables at spawn , no two runs present identical bosses.
- **5 Time Guardian bosses**, each affiliated with one Clock type; defeated guardian drops corresponding Clock.

---

### 8. Damage & Defense Model

**Three damage types:**
- `Physical` , reduced by armor percentage
- `Magic` , partially evaded; evasion stat reduces effective incoming magic damage
- `Time` , bypasses armor; directly deducts from global timer on hit

**Defense:**
- Armor applies flat percentage reduction to physical damage.
- Evasion stat provides a chance to fully negate time-based attacks (not physical dodge , a passive resistance stat).

**Critical Hit System:**
- `CritRate` stat governs crit probability per hit.
- On crit: damage × 2 (`CritDamage` multiplier), hit-stop extended, distinct VFX triggered.

---


### 9. Save System

- Full game state serialized via `USaveGame` subclass.
- Saved data: player stats, equipment loadout, inventory, collected clocks, world seed, elapsed time, active clockwork checkpoint.
- Async save/load using `UGameplayStatics::AsyncSaveGameToSlot` / `AsyncLoadGameFromSlot` to avoid hitching.
- Multiple save slots supported with metadata (session time, level, timestamp) stored separately in a lightweight slot index file.

---

### 10. Character Customization

- Morphtarget-driven face/body customization system.
- Character mesh is a modular assembly: head, body, hair, and makeup layers are separate skeletal mesh components sharing a master pose.
- Customization data serialized as `FCharacterAppearanceData` struct saved alongside player save state.
- Real-time preview in character select uses a dedicated `ACustomizationPreviewActor` with isolated lighting setup.

---

### 11. Clock HUD Architecture

The HUD centers on a radial clock widget mirroring the in-game stopwatch.

- Clock face rendered as a `UUserWidget` with a custom `UImage` driven by a `UMaterialInstanceDynamic`.
- Time remaining passed to the material as a scalar parameter controlling the sweep mask.
- Stat rings (Health, Stamina, Mana) are separate circular progress bars positioned around the clock face.
- Equipment slots, perk icons, and clock collection status are laid out in concentric rings using custom panel widgets with procedural slot placement via `UCanvasPanelSlot`.

---

## Performance Targets & Optimization

| Target | Approach |
|---|---|
| 60 fps (PC, 1080p+) | LOD chains on all meshes; Nanite for static geo; draw call budgeting |
| Memory | Async asset loading via `TSoftObjectPtr`; unload on biome transition |
| Tick optimization | Disable tick on idle actors; use `FTimerHandle` for periodic logic instead of per-frame tick |
| Physics | Chaos only where required (hook swing, cloth); ragdoll on-demand |
| AI | Perception system with range-gated updates; behavior tree paused for off-screen actors |
| Shader complexity | Custom material functions reused across biomes; parameter collections for global tweaks |

---

## Development Scope

| Category | Detail |
|---|---|
| Developer count | 1  |
| Engine | Unreal Engine 5.1 |
| Languages | C++,   (custom shader nodes) |
| 3D Assets | All original , modeled, textured, rigged, animated by developer |
| Total items | 300+ |
| Weapons | 50+ |
| Gameplay systems | 12+ discrete systems (see above) |
| Platforms | PC Windows / (Steam) |
| Development tools | UE5 Editor, ZBrush, Maya, Blender, Substance Painter, Photoshop |

---

## Build & Platform Notes

- Developed and shipped on UE 5.1. Not upgraded to later engine versions to preserve stability of shipped systems.
-   shipping tested via cross-compilation toolchain; physics and movement behavior verified consistent cross-platform.
- Mobile builds (iOS/Android) not in scope for this title; see [Royal Jump](https://play.google.com/store/apps/details?id=com.Kubrick.RoyalJump) for mobile-specific UE development.

---

## Related Projects

| Project | Description |
|---|---|
| [U.N Owen Was Her](https://store.steampowered.com/app/3420540/UN_Owen_Was_Her) | Third-person action shooter; boss AI, bullet-hell patterns, horror environments |
| [Olympus of the Heavens](https://store.steampowered.com/app/3358020/Olympus_of_the_Heavens) | Isometric co-op ARPG; procedural gen, Steam co-op networking, crafting |
| [Blood Garden](https://kubrik.itch.io/bloodgarden) | Souls-like melee combat; stamina system, parry, enemy AI |
| [Royal Jump](https://play.google.com/store/apps/details?id=com.Kubrick.RoyalJump) | Mobile platformer; touch controls, physics movement, mobile perf optimization |
| [ArtStation Portfolio](https://www.artstation.com/kubrik) | 3D modeling work , characters, creatures, props, environments |

---

## Developer

**Kubrik** , Developer & 3D Artist  

[Steam](https://store.steampowered.com/search/?developer=Kubrik) · [ArtStation](https://www.artstation.com/kubrik) 

![image](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2928270/a4b24ed070a65a05539b1a91aace24df50169210/library_hero.jpg?t=1731513472)

---

*All code, design, and custom assets produced by a single developer. No third-party gameplay code used in core systems.*
