# KitBasher
**A easy to use and portable VR Game Engine.**

KitBasher is a easy to use VR game engine built on **OpenXR** and **OpenGL**. The goal is to use this as a way to avoid using more bloated, general purpose engines to create specific games and simulations in the VR space.

<img width="980" height="614" alt="Screenshot 2026-09-27 013940" src="https://github.com/user-attachments/assets/5a13754b-50e1-4195-8f8e-0f436b22f4eb" />

---

## Design Philosophy

KitBasher is designed so that a developer can focus on gameplay logic without needing to be an expert in OpenGL, OpenXR, or multi-threaded physics.

### Node-Based Scene Graph (Babylon.js Inspired)
Everything in the world is a `Node`. Whether it's a camera, a light, or a 3D model, they all inherit from a common base. This allows for:
- **Hierarchical Transforms**: Parent-child relationships for complex objects.
- **Uniform Interaction**: An `Animator` can drive any `Node`, regardless of what it is.

### Lua-First Game Development
The engine runtime is native C++, but game-specific scene setup and gameplay
live in the top-level `game/` project folder. Developers normally edit
`game/game.lua` rather than changing the engine's `Game` C++ plumbing. This
keeps the engine reusable while making iteration on a game script fast.

### VR-First Workflow
VR development is traditionally slow because of the "Headset Cycle" (Put on headset -> Test -> Take off headset -> Code). KitBasher breaks this with **VR Simulation Mode**, allowing developers to test movements and logic in a desktop window before deploying to hardware.

---

## Engine Modules

| Module | Responsibility | Key Components |
| :--- | :--- | :--- |
| **Core** | The main part of the engine. Handles initialization, the main loop, and shutdown. | `engine.cpp`, `Game` class |
| **Platform** | Interfaces with the OS and Windowing system. | `Window`, `SceneEditor`, `PhysicsSystem` |
| **XR** | Manages the VR Headset, controllers, and OpenXR session. | `XRManager`, `Swapchains` |
| **Rendering**| The OpenGL pipeline. Handles shaders, buffers, and lighting. | `RenderSystem`, `Light`, `GLBLoader` |
| **Scene** | High-level object management and animation. | `Node`, `MeshNode`, `Animator` |

---

## Developer's Guide: Build a game in Lua

Game developers should normally work in `game/`, not in `src/game/`. The native
`Game` class only creates the scene/runtime plumbing, loads `game/game.lua`, and
drives its lifecycle. The script is the single game entry point and returns a
table with these optional callbacks:

```lua
local game = {}

function game.onStart(scene, animator)
    -- Create the initial scene and register gameplay behavior.
end

function game.onUpdate(deltaTime, scene, animator)
    -- Run per-frame gameplay logic.
end

return game

```

### Creating a scene

The first-pass Lua API lets the script create lights and load GLB meshes:

```lua
local sun = scene:createDirectionalLight("Sun")
sun:setColor(1.0, 0.9, 0.8)
sun:setPosition(0.0, 4.0, 0.0)
sun:setIntensity(1.0)
sun:setExposure(0.0)

local bulb = scene:createPointLight("Bulb", 2.0, 4.0, 2.0)
bulb:setColor(1.0, 0.8, 0.4)
bulb:setIntensity(2.0)
bulb:setExposure(0.0)
bulb:setRadius(15.0)

Engine.setSkybox("resources/skybox.jpg")

local floor = scene:loadMesh("resources/plane.glb", "Floor")
floor:setPosition(0.0, 0.0, 0.0)
Engine.addRigidBody(floor, "static", "box", 0.8, 0.0)

```

`scene:loadMesh(path, name)` loads the asset through the native glTF pipeline,
attaches the resulting node to the scene root, and returns the node to Lua.
The available body types are `"static"`, `"dynamic"`, and `"kinematic"`. The available collider types are `"box"` and `"convex"`.

### Transform and scene queries

Nodes currently expose:

```lua
node:getName()
node:setName("NewName")
node:setPosition(x, y, z)
node:getPositionX()
node:getPositionY()
node:getPositionZ()
node:setScale(x, y, z)

local node = scene:findNode("Floor")

```

Lua does not own scene nodes. The native scene retains ownership and controls
their lifetime.

### Animation Controls (Procedural & GLB Node Animations)

Register a continuous transform update through the native `Animator`. The `procedural` call returns an identifier so you can stop or play it dynamically:

```lua
local frogRotationId = animator:procedural(
    frog,
    "y",
    "positive",
    "rotation",
    1.0
)

-- Stop or resume the procedural animation later
animator:stopProcedural(frogRotationId)
animator:playProcedural(frogRotationId)

```

The axis may be `"x"`, `"y"`, or `"z"`. The direction may be `"positive"` or
`"negative"`, the type may be `"rotation"` or `"position"`, and the speed is
expressed in radians or world units per second.

You can also target animations embedded directly in GLB/gltf files attached to nodes:

```lua
-- Play or stop an embedded model animation by name
animator:playAnimation(frog, "Idle")
animator:stopAnimation(frog, "Idle")

```

### VR Player Rig & Locomotion
KitBasher includes a built-in VR Player Rig that handles headset tracking, smooth/snap turning, and thumbstick/gamepad locomotion. The rig decouples the physical headset position from the virtual world, allowing you to move the player without causing VR motion sickness.

You can control and query the VR player's position and comfort settings directly from Lua using the `Engine` namespace:

```lua
function game.onStart(scene, animator)
    -- Set initial spawn point and comfort settings
    Engine.setVRPlayerPosition(0.0, 0.0, 0.0)
    Engine.setVRPlayerYaw(0.0)
    Engine.setVRMoveSpeed(2.5)
    Engine.setVRSnapTurn(true) -- Snap turn is recommended for VR comfort
end

function game.onUpdate(deltaTime, scene, animator)
    -- Example: Respawn the player if they fall out of the world
    if Engine.getVRPlayerPositionY() < -5.0 then
        Engine.setVRPlayerPosition(0.0, 0.0, 0.0)
    end
end
```

### Demo project

The checked-in `game/game.lua`
creates the demo light, skybox, floor, frog mesh, physics bodies, and frog
rotation entirely from Lua:

```lua
function game.onStart(scene, animator)
    local floor = scene:loadMesh("resources/plane.glb", "Floor")
    Engine.addRigidBody(floor, "static", "box", 0.8, 0.0)

    local frog = scene:loadMesh("resources/frog.glb", "Frog")
    frog:setPosition(0.0, 3.0, 0.0)
    Engine.addRigidBody(frog, "dynamic", "convex", 0.6, 0.1)
    animator:procedural(frog, "y", "positive", "rotation", 1.0)
end

```

---

## Quick Start

**Option A - batch scripts:**
```
build.bat
run.bat
```

`build.bat` configures and builds the engine. `run.bat` launches the built executable. Currently is defaulted to Debug. Pass `Release` for a Release build, `run.bat Release`. The default project loads `game/game.lua`, which defines the demo scene.

**Option B - run the commands yourself:**
```powershell
Remove-Item -Recurse -Force build
cmake -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.6"
cmake --build build
```
Then run the KitBasher.exe file in `\build\bin\Debug\KitBasher.exe`

