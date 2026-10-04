# VR Player Rig API

The VR Player Rig is the core locomotion and tracking system for VR experiences in KitBasher. It decouples the physical headset position (OpenXR tracking) from the virtual world position, allowing developers to implement teleportation, artificial locomotion (thumbstick movement), and comfort turning (snap or smooth).

## Core Concepts

- **The Rig:** An invisible "root" transform in the 3D world that represents the player's virtual body.
- **Headset Offset:** The physical position and rotation of the HMD relative to the Rig.
- **Final View Matrix:** Calculated internally as `RawOpenXRView * Inverse(RigWorldTransform)`.

By moving the Rig, you move the player in the game world without causing the severe VR sickness that comes from moving the camera directly independent of the user's physical head.

## Lua API Reference

The VR Player Rig is exposed to Lua via the global `Engine` namespace.

### Position & Orientation

#### `Engine.setVRPlayerPosition(x, y, z)`
Instantly teleports or moves the VR Rig to the specified world coordinates.
* **Use case:** Teleporting the player to a checkpoint, resetting their position if they fall out of the world, or spawning them at a specific location.

#### `Engine.getVRPlayerPositionX()` / `Y()` / `Z()`
Returns the current world-space coordinates of the VR Rig's center.
* **Use case:** Trigger zones, proximity checks, or UI elements that need to know where the player is.

#### `Engine.setVRPlayerYaw(yawRadians)`
Sets the Rig's facing direction in radians.
* **Use case:** Forcing the player to look at a specific point of interest, or resetting their orientation when entering a vehicle or teleporting.

#### `Engine.getVRPlayerYaw()`
Returns the current yaw (facing direction) of the Rig in radians.

### Locomotion Settings

#### `Engine.setVRMoveSpeed(unitsPerSecond)`
Adjusts the speed of artificial thumbstick/gamepad movement.
* **Default:** `2.0`

#### `Engine.setVRSnapTurn(enabled)`
Toggles between Snap Turn and Smooth Turn.
* `true` (**Snap Turn**): The player rotates in fixed increments (default 30 degrees) when pushing the right thumbstick or pressing bumpers. **Highly recommended for VR comfort.**
* `false` (**Smooth Turn**): The player rotates continuously based on thumbstick input. Can cause motion sickness in sensitive users.
* **Default:** `true`

## Input Mapping

The VR Player Rig automatically listens to standard VR controllers (Oculus Touch, Valve Index, Windows MR, Vive) and Xbox controllers via GLFW.

| Action | VR Controller | Xbox Controller | Keyboard (Simulation Mode) |
| :--- | :--- | :--- | :--- |
| **Move** | Left Thumbstick | Left Stick | WASD |
| **Turn** | Right Thumbstick | Right Stick | Arrow Keys |
| **Snap Turn Left** | Right Stick (Push Left) | Left Bumper | Left Arrow |
| **Snap Turn Right** | Right Stick (Push Right) | Right Bumper | Right Arrow |
| **Interact/Select** | Trigger | A / Right Trigger | Left/Right Mouse Click |

## Example: Checkpoint & Respawn System

```lua
local checkpoints = {
    {x = 10.0, y = 0.0, z = 5.0},
    {x = -15.0, y = 0.0, z = 20.0}
}
local currentCheckpoint = 1

function game.onStart(scene, animator)
    -- Configure comfort settings from Lua
    Engine.setVRSnapTurn(true)
    Engine.setVRMoveSpeed(3.5)
end

function game.onUpdate(deltaTime, scene, animator)
    local px = Engine.getVRPlayerPositionX()
    local py = Engine.getVRPlayerPositionY()
    local pz = Engine.getVRPlayerPositionZ()

    -- If the player falls into a pit, respawn them
    if py < -10.0 then
        local cp = checkpoints[currentCheckpoint]
        Engine.setVRPlayerPosition(cp.x, cp.y, cp.z)
    end
    
    -- Simple proximity trigger to advance checkpoint
    local target = checkpoints[currentCheckpoint]
    local dist = math.sqrt((px - target.x)^2 + (pz - target.z)^2)
    
    if dist < 2.0 then
        currentCheckpoint = currentCheckpoint + 1
        if currentCheckpoint > #checkpoints then 
            currentCheckpoint = 1 
        end
    end
end
```