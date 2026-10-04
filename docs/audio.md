# Audio

KitBasher includes a built-in audio system powered by `miniaudio`. It allows you to easily play sound effects and background music directly from your Lua gameplay scripts. 

The audio system separates **Music** (long-running, single-track background audio) from **Sounds** (short, overlapping, one-shot or looping sound effects).

## Lua API Reference

All audio functions are exposed under the global `Audio` namespace.

### Sound Effects

#### `Audio.playSound(path, [volume])`
Plays a one-shot sound effect. Multiple sound effects can play simultaneously.
* **`path`** (string): The relative path to the `.wav` file (e.g., `"resources/audio/jump.wav"`).
* **`volume`** (number, optional): The volume level from `0.0` to `1.0`. Defaults to `1.0`.

#### `Audio.playSoundLoop(path, [volume])`
Plays a sound effect that loops continuously until stopped or the application exits.
* **`path`** (string): The relative path to the `.wav` file.
* **`volume`** (number, optional): The volume level from `0.0` to `1.0`. Defaults to `1.0`.

#### `Audio.stopAllSounds()`
Immediately stops and cleans up all currently playing sound effects.

---

### Music

#### `Audio.playMusic(path, [volume])`
Plays a background music track. 
* **Note:** The engine only supports **one** music track at a time. Calling this function will automatically fade out/stop any currently playing music track before starting the new one.
* **`path`** (string): The relative path to the `.wav` file.
* **`volume`** (number, optional): The volume level from `0.0` to `1.0`. Defaults to `0.8`.

#### `Audio.stopMusic()`
Immediately stops the currently playing background music track.

---

### Global Controls

#### `Audio.setMasterVolume(volume)`
Sets the global master volume for the entire audio engine. This affects both music and sound effects.
* **`volume`** (number): The master volume level from `0.0` (muted) to `1.0` (max).

---

## Example

Here is how you can integrate audio into your game loop:

```lua
local game = {}
local jumpSoundPlayed = false

function game.onStart(scene, animator)
    -- Set the global volume to 80%
    Audio.setMasterVolume(0.8)

    -- Start background music (loops automatically by default in C++)
    Audio.playMusic("resources/audio/synthwave_bg.wav", 0.5)

    -- Load a mesh
    local frog = scene:loadMesh("resources/frog.glb", "Frog")
    frog:setPosition(0.0, 3.0, 0.0)
    Engine.addRigidBody(frog, "dynamic", "convex", 0.6, 0.1)
end

function game.onUpdate(deltaTime, scene, animator)
    local frog = scene:findNode("Frog")
    
    if frog ~= nil then
        -- Example: Play a sound effect when the frog falls below a certain point
        if frog:getPositionY() < 0.5 and not jumpSoundPlayed then
            Audio.playSound("resources/audio/splash.wav", 1.0)
            jumpSoundPlayed = true
        end

        -- Reset the frog and allow the sound to play again
        if frog:getPositionY() < -100.0 then
            frog:setPosition(0.0, 3.0, 0.0)
            jumpSoundPlayed = false
        end
    end
end

return game
```