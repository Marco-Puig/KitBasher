#pragma once

#include "xr/XRManager.h"

#include <GLFW/glfw3.h>
#include <array>

struct HandState {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    bool poseValid = false;

    bool gripPressed = false;
    bool triggerPressed = false;

    float gripValue = 0.0f;
    float triggerValue = 0.0f;
};

struct VRInputFrame {
    glm::vec2 move{0.0f};
    glm::vec2 turn{0.0f};
    bool snapTurnLeft = false;
    bool snapTurnRight = false;

    std::array<HandState, 2> hands;
};

inline VRInputFrame gatherVRInput(const XRManager& xr) {
    VRInputFrame input;

    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1)) {
        GLFWgamepadstate state{};

        if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) {
            input.move.x = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
            input.move.y = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];

            input.turn.x = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
            input.turn.y = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];

            input.snapTurnLeft =
                state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] == GLFW_PRESS;

            input.snapTurnRight =
                state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] == GLFW_PRESS;
        }
    }

    const glm::vec2 leftThumbstick = xr.getThumbstick(0);
    const glm::vec2 rightThumbstick = xr.getThumbstick(1);

    if (glm::length(leftThumbstick) > glm::length(input.move)) {
        input.move = leftThumbstick;
    }

    if (glm::length(rightThumbstick) > glm::length(input.turn)) {
        input.turn = rightThumbstick;
    }

    for (int i = 0; i < 2; ++i) {
        XRControllerState state = xr.getControllerState(i);
        input.hands[i].position = state.position;
        input.hands[i].orientation = state.orientation;
        input.hands[i].gripValue = state.grip;
        input.hands[i].triggerValue = state.trigger;
        input.hands[i].gripPressed = state.grip > 0.5f;
        input.hands[i].triggerPressed = state.trigger > 0.5f;
        input.hands[i].poseValid = true; // In a real impl, we'd check if pose was successfully located
    }

    return input;
}
