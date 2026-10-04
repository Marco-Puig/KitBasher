# Multiplayer System

## Setup

Please ensure you download the Steamworks SDK and place it in the third_party folder. Additionally, the folder's name needs to just be ``steamworks``

## Overview

KitBasher features a built-in **host-authoritative** multiplayer system designed for private co-op lobbies (up to 3 players). 

In this model:
* **The Host (Player 0)** runs the authoritative game simulation, physics, and gameplay logic.
* **Clients (Players 1 & 2)** send their inputs to the host and receive world state updates.
* The engine automatically interpolates and smooths remote transforms for replicated nodes.

All multiplayer functions are exposed under the global `Net` namespace.

---

## Connection & Session Info

Use these functions to determine the current state of the network session and the local player's role.

#### `Net.isConnected()`
Returns `true` if the local instance is currently connected to a lobby (either as a host or a client).
```lua
if Net.isConnected() then
    print("Playing online!")
else
    print("Playing offline.")
end
```

#### `Net.isHost()`
Returns `true` if the local player is the authoritative host of the session. 
*Use this to gate gameplay logic, physics spawning, and AI so they only run on one machine.*
```lua
if Net.isHost() then
    -- Only the host should simulate enemy AI or spawn networked items
end
```

#### `Net.getLocalPlayerSlot()`
Returns the integer slot ID of the local player.
* `0` = Host
* `1` = First Client
* `2` = Second Client

#### `Net.getPlayerCount()`
Returns the total number of players currently connected to the lobby (1 to 3).

---

## Scene Replication

To make an object visible and synchronized across all players, the host must explicitly tell the engine to replicate it.

#### `Net.replicateNode(node)`
**(Host Only)** Marks a node for network replication. The host will send a spawn message to all clients, and automatically begin sending transform/physics snapshots for this node every frame.
```lua
local cube = scene:loadMesh("resources/cube.glb", "NetworkedCube")
cube:setPosition(0, 1, 0)

if Net.isHost() then
    Net.replicateNode(cube)
end
```

#### `Net.despawnNode(node)`
**(Host Only)** Removes a replicated node from the network. The host will send a despawn message to all clients, and the node will be safely destroyed everywhere.
```lua
if Net.isHost() then
    Net.despawnNode(networkedCube)
end
```

---

## Example: Host-Authoritative Spawning

Here is a complete example of how to safely spawn a networked object in `game.lua`.

```lua
local game = {}
local networkedTower = nil

function game.onStart(scene, animator)
    -- 1. Setup local environment (Skybox, Floor, etc.)
    Engine.setSkybox("resources/graphics/skybox.jpg")
    local floor = scene:loadMesh("resources/plane.glb", "Floor")
    Physics.addRigidBody(floor, "static", "box", 0.8, 0.0)

    -- 2. Host-only setup
    if Net.isConnected() and Net.isHost() then
        print("I am the host. Spawning networked tower...")
        
        networkedTower = scene:createDestructibleBuilding("NetTower", 0, 0, -5, 4.0, 100.0)
        
        -- Add physics ONLY on the host. 
        -- Clients will receive physics snapshots automatically.
        Physics.addRigidBody(networkedTower, "dynamic", "box", 0.5, 0.1)
        
        -- Tell the engine to sync this node to all clients
        Net.replicateNode(networkedTower)
    end
end

function game.onUpdate(deltaTime, scene, animator)
    -- Only the host should apply gameplay logic to networked objects
    if Net.isHost() and networkedTower ~= nil then
        -- Example: Host applies an upward force every 5 seconds
        -- All clients will see the tower fly up smoothly via interpolation
        networkedTower:addForce(0, 50.0, 0) 
    end
end

return game
```

---

## Best Practices & Rules

1. **Never run authoritative physics on clients for replicated nodes.** 
   If the host replicates a dynamic physics object, the client's local physics engine might fight the network updates. The host should be the only machine calling `Physics.addRigidBody()` on shared networked objects.
2. **Always check `Net.isHost()` before modifying shared game state.**
   If a client tries to move a replicated node via Lua, the host's next network snapshot will overwrite it, causing visual jitter.
3. **Local vs. Networked Avatars.**
   Your local VR rig/avatar is controlled locally. Remote player avatars should be spawned by the host using `Net.replicateNode()` so their transforms are automatically synced.
4. **Late Joining.**
   If a player joins halfway through the game, the engine's Replication Manager will automatically send them the current state of all nodes previously marked with `Net.replicateNode()`.