# VR Interaction System

The VR Interaction system provides a way for players to interact with objects in the scene using VR controllers.

## Hand Controllers
Hands are automatically managed by the `InteractionManager`. They follow the OpenXR controller poses and are transformed by the `VRPlayerRig`.

## Interactable Objects

### ButtonNode
A `ButtonNode` is a simple interactable object that triggers a "pressed" state when a hand controller is within its interaction radius and the trigger is pressed.

**Lua API:**
- `ButtonNode:isPressed()`: Returns `true` if the button is currently being pressed.
- `ButtonNode:press()`: Manually set the button to pressed state.
- `ButtonNode:release()`: Manually set the button to released state.

### JoystickNode
A `JoystickNode` is a grabbable object. When a hand controller is within its grab radius and the grip button is pressed, the joystick attaches to the hand.

**Lua API:**
- `JoystickNode:isHeld()`: Returns `true` if the joystick is currently held by a hand.
- `JoystickNode:setHomePosition(vec3)`: Sets the position the joystick returns to when released.

## Lua Integration

To use custom interactable nodes in your script, you must register them with the `Interaction` namespace so the engine knows to check for hand collisions.

### Registering Buttons
When you create a button node, use `Interaction.addButton()` to enable it:

```lua
local myBtn = scene:createNode("MyButton", "ButtonNode")
myBtn:setPosition(0, 1, -0.5)
Interaction.addButton(myBtn)
```

### Registering Joysticks
Similar to buttons, joysticks must be registered:

```lua
local myJoy = scene:createNode("MyJoy", "JoystickNode")
myJoy:setPosition(0.2, 1, -0.5)
Interaction.addButton(myJoy) -- Use relevant registration function
```

## Implementation
Interaction logic is handled by the `InteractionManager` singleton, which performs distance-based checks between hands and interactable nodes every frame.
