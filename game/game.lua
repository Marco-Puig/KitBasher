local game = {}

local elapsedTime = 0.0

local playerAvatars = {}
local localPlayerSlot = 0

local hostAvatarsSpawned = false
local offlineAvatarsSpawned = false

local function isDestructible(node)
    return node ~= nil
        and node.applyDamage ~= nil
        and node.isDestroyed ~= nil
end

local function tryApplyDamage(node, amount)
    if isDestructible(node) then
        node:applyDamage(amount)
    end
end

local function getPlayerColor(slot)
    if slot == 0 then
        -- Host / player 0: blue
        return 0.25, 0.75, 1.0
    elseif slot == 1 then
        -- Player 1: orange
        return 1.0, 0.55, 0.2
    else
        -- Player 2: green
        return 0.55, 1.0, 0.35
    end
end

local function spawnPlayerAvatar(scene, slot, x, y, z)
    if scene.createPlayerAvatar == nil then
        return nil
    end

    local name = "PlayerAvatar_" .. tostring(slot)

    local avatar = scene:findNode(name)

    if avatar ~= nil then
        playerAvatars[slot] = avatar
        avatar:setPosition(x, y, z)
        avatar:setScale(1.0, 1.0, 1.0)
        return avatar
    end

    local r, g, b = getPlayerColor(slot)

    avatar = scene:createPlayerAvatar(
        name,
        x,
        y,
        z,
        r,
        g,
        b
    )

    if avatar ~= nil then
        playerAvatars[slot] = avatar

        -- If we are the host, replicate this avatar to clients.
        if Net ~= nil and Net.replicateNode ~= nil and Net.isHost ~= nil then
            if Net.isHost() then
                Net.replicateNode(avatar)
            end
        end
    end

    return avatar
end

local function spawnHostAvatars(scene)
    for slot = 0, 2 do
        local x = 0.0
        local z = 1.0

        if slot == 1 then
            x = 1.5
            z = -1.5
        elseif slot == 2 then
            x = -1.5
            z = -1.5
        end

        spawnPlayerAvatar(scene, slot, x, 0.0, z)
    end

    hostAvatarsSpawned = true
end

local function spawnOfflineAvatars(scene)
    spawnPlayerAvatar(scene, 1, 1.5, 0.0, -1.5)
    spawnPlayerAvatar(scene, 2, -1.5, 0.0, -1.5)

    offlineAvatarsSpawned = true
end

local function hideOfflineAvatars()
    for slot = 1, 2 do
        local avatar = playerAvatars[slot]

        if avatar ~= nil then
            avatar:setScale(0.0001, 0.0001, 0.0001)
        end
    end

    offlineAvatarsSpawned = false
end

local function getAvatar(scene, slot)
    local avatar = playerAvatars[slot]

    if avatar == nil then
        avatar = scene:findNode("PlayerAvatar_" .. tostring(slot))
        playerAvatars[slot] = avatar
    end

    return avatar
end

local function updateLocalAvatar(scene)
    if Engine.getVRPlayerPositionX == nil then
        return
    end

    local avatar = getAvatar(scene, localPlayerSlot)

    if avatar == nil then
        return
    end

    local x = Engine.getVRPlayerPositionX()
    local y = Engine.getVRPlayerPositionY()
    local z = Engine.getVRPlayerPositionZ()

    -- Move locally.
    avatar:setPosition(x, y, z)

    -- If we are a client, send our avatar position to the host.
    if Net ~= nil
        and Net.isConnected ~= nil
        and Net.isConnected()
        and Net.isHost ~= nil
        and Net.sendAvatarTransform ~= nil then

        if not Net.isHost() then
            Net.sendAvatarTransform(x, y, z)
        end
    end
end

function game.onStart(scene, animator)
    local light = scene:createDirectionalLight("Sun")
    light:setIntensity(0.4)
    light:setColor(1.0, 0.9, 0.8)
    light:setPosition(0.0, 4.0, 0.0)
    
    -- Demo: Interaction objects
    -- We now spawn them explicitly. Since they are specialized nodes,
    -- we use the Interaction manager or manual instantiation if available.
    -- For now, we'll assume these are pre-spawned or created via a compatible method.
    local btn = scene:findNode("MyTestButton")
    if btn then
        btn:setPosition(0.0, 1.0, -0.5)
    end
    
    local lJoy = scene:findNode("LeftJoystick")
    if lJoy then
        lJoy:setPosition(-0.3, 1.0, -0.5)
    end
    
    local rJoy = scene:findNode("RightJoystick")
    if rJoy then
        rJoy:setPosition(0.3, 1.0, -0.5)
    end

    Engine.setSkybox("resources/graphics/skybox.jpg")


    local floor = scene:loadMesh("resources/models/plane.glb", "Floor")
    floor:setPosition(0.0, 0.0, 0.0)
    Physics.addRigidBody(floor, "static", "convex", 0.8, 0.0)
    animator:procedural(floor, "y", "positive", "rotation", 0.1)

    local frog = scene:loadMesh("resources/models/frog.glb", "Frog")
    if frog ~= nil then
        frog:setPosition(0.0, 3.0, 0.0)
        Physics.addRigidBody(frog, "dynamic", "convex", 0.6, 0.1)
    end

    if scene.createDestructibleBuilding ~= nil then
        local tower = scene:createDestructibleBuilding(
            "Tower",
            0.0,
            0.0,
            -4.0,
            3.0,
            100.0
        )

        if tower ~= nil then
            Physics.addRigidBody(tower, "static", "box", 0.8, 0.0)
        end
    end

    if scene.createFire ~= nil then
        local fire = scene:createFire("Fire", 0.0, 0.25, -2.0)
        if fire ~= nil then
            fire:setParticleEmissionRate(140.0)
        end
    end

    if scene.createSmoke ~= nil then
        local smoke = scene:createSmoke("Smoke", 0.0, 1.25, -2.0)
        if smoke ~= nil then
            smoke:setParticleEmissionRate(30.0)
        end
    end
end

function game.onUpdate(deltaTime, scene, animator)
    elapsedTime = elapsedTime + deltaTime

    
    -- Network state
    local networkActive = false

    if Net ~= nil and Net.isConnected ~= nil then
        networkActive = Net.isConnected()
    end

    if Net ~= nil and Net.getLocalPlayerSlot ~= nil then
        localPlayerSlot = Net.getLocalPlayerSlot()
    end

    if networkActive then
        if offlineAvatarsSpawned then
            hideOfflineAvatars()
        end

        -- Host is responsible for spawning all player avatars.
        if Net.isHost ~= nil and Net.isHost() and not hostAvatarsSpawned then
            spawnHostAvatars(scene)
        end
    else
        if not offlineAvatarsSpawned and not hostAvatarsSpawned then
            spawnOfflineAvatars(scene)
        end
    end

    
    -- Frog respawn logic
    local frog = scene:findNode("Frog")

    if frog ~= nil then
        if frog:getPositionY() < -100.0 then
            frog:setPosition(0.0, 3.0, 0.0)
        end
    end

    
    -- Tower damage demo
    local tower = scene:findNode("Tower")

    if isDestructible(tower) and not tower:isDestroyed() then
        if elapsedTime > 5.0 then
            tryApplyDamage(tower, 10.0 * deltaTime)
        end

        if elapsedTime > 15.0 then
            tryApplyDamage(tower, 9999.0)
        end

        if frog ~= nil then
            local dx = frog:getPositionX() - tower:getPositionX()
            local dz = frog:getPositionZ() - tower:getPositionZ()

            local distanceSquared = dx * dx + dz * dz

            if distanceSquared < 2.5 then
                tryApplyDamage(tower, 20.0 * deltaTime)
            end
        end
    end

    
    -- Particle system demo
    if elapsedTime > 30.0 then
        local fire = scene:findNode("Fire")
        if fire ~= nil and fire.stopParticles ~= nil then
            fire:stopParticles()
        end

        local smoke = scene:findNode("Smoke")
        if smoke ~= nil and smoke.stopParticles ~= nil then
            smoke:stopParticles()
        end
    end

    
    -- Player avatar movement
    updateLocalAvatar(scene)
    
    -- Demo: Interaction feedback
    local btn = scene:findNode("TestButton")
    if btn and btn:isPressed() then
        -- You could trigger an event or sound here
    end

    
    -- Offline avatar animation
    if not networkActive then
        local avatar1 = playerAvatars[1]
        local avatar2 = playerAvatars[2]

        if avatar1 ~= nil then
            avatar1:setPosition(
                math.sin(elapsedTime * 0.8) * 2.0,
                0.0,
                -2.0 + math.cos(elapsedTime * 0.8) * 2.0
            )
        end

        if avatar2 ~= nil then
            avatar2:setPosition(
                math.sin(elapsedTime * 0.8 + math.pi) * 2.0,
                0.0,
                -2.0 + math.cos(elapsedTime * 0.8 + math.pi) * 2.0
            )
        end
    end
end

return game