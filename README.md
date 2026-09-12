# Chutllu Sim — Eldritch Horror & Roguelike Simulator

<img align="left" width="30%"  height="160" src="https://github.com/kubrvk/cthulhusimulator/blob/main/Docs/img/logo.jpg"/>
<h3><a href="https://github.com/kubrvk/cthulhusimulator">11-) Chutllu Sim</a><a href="https://kubrik.itch.io/cthulhusim"> <img src="https://img.shields.io/badge/itch.io: https://kubrik.itch.io/cthulhusim-000000?style=flat-square&logo=itch.io&logoColor=white&labelColor=000000" height="25"/> </a></h3>

![](https://img.shields.io/badge/Action-a13636?style=) ![](https://img.shields.io/badge/Rogue--like-a00d7c?style=) ![](https://img.shields.io/badge/Wave--Rush-a17736?style=) ![C++](https://img.shields.io/badge/C++-00599C?style=logo=c%2B%2B&logoColor=white)  ![C++](https://img.shields.io/badge/Unreal_Engine_5.7-0E1128?style=for-the-badges&logo=unrealengine&logoColor=white)  ![Status](https://img.shields.io/badge/Status-Early_Development-851d10?style=for-the-badges)
<br>
Chutllu Sim is a high-octane 3D action roguelike built in Unreal Engine 5. Using procedurally animated, physics-driven tentacles, your objective is simple: crush urban infrastructure, grab and devour fleeing populations, harvest their souls, and withstand the full retaliatory might of the military.
<br clear="left"/>
<p align="center">
<img src="https://github.com/kubrvk/cthulhusimulator/blob/main/Docs/img/11_Combat_Eye_Beam_Laser.jpg" width="25%"/><img src="https://github.com/kubrvk/cthulhusimulator/blob/main/Docs/img/Screenshot 2026-09-12 14-22-46.png" width="25%"/><img src="https://github.com/kubrvk/cthulhusimulator/blob/main/Docs/img/Screenshot 2026-09-12 14-22-57.png" width="25%"/><img src="https://github.com/kubrvk/cthulhusimulator/blob/main/Docs/img/Screenshot 2026-09-12 14-19-32.png" width="25%"/>
</p>

---

<p align="center">
  <img src="https://img.shields.io/badge/Unreal_Engine-5.5+-0E1128?style=for-the-badge&logo=unrealengine&logoColor=white" />
  <img src="https://img.shields.io/badge/Language-C++20-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" />
  <img src="https://img.shields.io/badge/Genre-Eldritch_Action_Roguelike-8B0000?style=for-the-badge" />
  <img src="https://img.shields.io/badge/Status-Active_Development-success?style=for-the-badge" />
</p>

---

## Overview

        ┌─────────────────────────────────────────────────────────────┐
        │  WAVE COMBAT                                                │
        │   - Escalating military threat (Infantry -> Tanks -> Jets)   │
        │   - Grab & devour civilians  -> +Souls & Health             │
        │   - Crush vehicles & bosses  -> +Massive Souls & Score      │
        └──────────────────────────────┬──────────────────────────────┘
                                       ▼
        ┌─────────────────────────────────────────────────────────────┐
        │  WAVE INTERMISSION & MUTATIONS                              │
        │   - Roguelike Drafting: Choose 1 of 3 Eldritch Passives     │
        │   - Abyss Sanctum: Unlock permanent meta-progression traits │
        │   - Evolve reach, lifesteal, damage, and tentacle count     │
        └─────────────────────────────────────────────────────────────┘
```

---

## Gameplay Features and Systems

<p align="center">
  <img src="Docs/img/Screenshot 2026-09-12 14-22-57.png" width="49%" alt="Tentacles Reaching Skyscrapers" />
  <img src="Docs/img/11_Combat_Eye_Beam_Laser.jpg" width="49%" alt="Street Devour Combat" />
</p>

### Procedural Tentacle Combat and Consumption
* **Physics & Inverse Kinematics:** Real-time tentacle reach, physics grabs, and dynamic surface attachment allowing you to whip, slam, and seize targets anywhere on screen.
* **Grab & Devour Mechanic:** Snatch infantry, fleeing civilians, and light vehicles. Pulling victims into your abyssal maw instantly consumes them for **Souls** and regenerates vital health.
* **Devastating Eldritch Powers:**
  * **[Q] Void Nova:** Discharges an expanding shockwave of eldritch energy that obliterates nearby infantry and projectiles.
  * **[E] Eye Beam:** High-intensity concentrated cosmic laser that tears through armored vehicles and aircraft.
  * **[F] Tentacle Slam:** Earth-shattering ground pound flattening barricades and heavy tanks.
  * **[R] Eldritch Rage:** Build adrenaline through slaughter to unleash temporary invulnerability and maximum reach.

---

<p align="center">
  <img src="Docs/img/01_StartMenu.png" width="49%" alt="Main Menu" />
  <img src="Docs/img/02_AbyssShop_Upgrades.png" width="49%" alt="Abyss Upgrades" />
</p>

### Roguelike Drafting and The Abyss
* **In-Run Passive Drafts:** Between combat waves, draft from three randomly offered eldritch blessings affecting damage, tentacle reach, movement speed, and lifesteal.
* **The Abyss (Permanent Meta-Progression):** Bank souls across runs to purchase permanent ascension passives:
  * **Ancient Flesh:** Exponentially scales base Max Health.
  * **Elder Blood:** Endows passive regeneration.
  * **Void Claws & Deep-Sea Fury:** Enhances tentacle damage and structural penetration.
  * **Abyssal Stride & Eldritch Speed:** Increases traversal velocity and attack cadence.
  * **Soul Drinker:** Increases soul multipliers for faster in-run scaling.

---

<p align="center">
  <img src="Docs/img/Screenshot 2026-09-12 14-22-10.png" width="49%" alt="Sanctuary Gateway" />
  <img src="Docs/img/13_City_Aerial_Skyline.jpg" width="49%" alt="City Breach Portal" />
</p>

### The City Fights Back (Enemy Roster)
The military escalates its response in structured, intense combat waves:
* **Infantry & Special Forces:** Heavy gunners, snipers, flame troopers, and rocket squads.
* **Ground Armor:** Armored personnel carriers (APCs), Anti-Air tanks, and Main Battle Tanks firing heavy ballistic shells.
* **Air Superiority:** Attack helicopters strafing with chainguns and unguided rockets, accompanied by high-altitude supersonic fighter jets.

---

## Technical Architecture (C++)

Built strictly in native C++ for maximum tick performance and physics reliability:

| Component | Responsibility |
|---|---|
| **`AChutlluCharacter`** | Core leviathan pawn: procedural tentacle management, grab/consume state machines, health components, and ability execution. |
| **`UChutlluHealthComponent`** | Unified damage resolution component utilized across the player, destructible city props, and military enemies. |
| **`UChutlluWaveManagerComponent`** | Wave director on the GameMode: oversees spawn budgets, wave state transitions, ambient civilian dispersal, and escalation curves. |
| **`UChutlluUpgradeComponent`** | Aggregates active `UChutlluPassiveEffect` data assets into live combat multipliers. |
| **`AChutlluEnemyBase` & Variants** | High-performance C++ AI enemies driving custom skeletal meshes, Niagara particle trails, and weapon sockets with zero Blueprint bloat. |

---

## Controls

| Key | Action |
|---|---|
| **`WASD`** | Traverse the city streets |
| **`Mouse`** | Aim tentacle focus & target crosshair |
| **`Left Mouse Button`** | Tentacle strike / Grab & Pull |
| **`Q`** | Cast **Void Nova** |
| **`E`** | Fire **Eye Beam** |
| **`F`** | Execute **Tentacle Slam** |
| **`R`** | Trigger **Eldritch Rage** |
| **`Esc / P`** | Open **The Lull** (Pause & Run Stats) |

---

## Build and Installation

1. Clone repository:
   ```bash
   git clone https://github.com/kubrvk/cthulhusimulator.git
   ```
2. Open `ChutlluSim.uproject` with **Unreal Engine 5.5+**.
3. Generate Visual Studio project files and build `ChutlluSimEditor` in **Development Editor - Win64**.
4. Launch the editor and open `Content/Maps/` or test in the Sanctuary starter area.

---

## License and Credits

Developed by [Kubrick](https://github.com/kubrvk). All rights reserved.
