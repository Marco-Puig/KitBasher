#pragma once

#include "scene/MeshNode.h"
#include <string>

class JoystickNode;

class HandController : public MeshNode {
public:
    HandController(const std::string& name, bool isLeftHand);
    ~HandController() override = default;

    void updateHand(const glm::vec3& pos, const glm::quat& rot, bool grip, bool trigger, float gripVal, float triggerVal);

    bool isGripping() const { return m_gripPressed; }
    bool isTriggerPressed() const { return m_triggerPressed; }
    float getGripValue() const { return m_gripValue; }
    float getTriggerValue() const { return m_triggerValue; }

    void attachJoystick(JoystickNode* joystick);
    void detachJoystick();
    JoystickNode* getHeldJoystick() const { return m_heldJoystick; }

    void loadHandModel(const std::string& path); // TODO: implement GLB loading

private:
    void createCubeMesh();

    bool m_isLeftHand;
    bool m_gripPressed = false;
    bool m_triggerPressed = false;
    float m_gripValue = 0.0f;
    float m_triggerValue = 0.0f;
    JoystickNode* m_heldJoystick = nullptr;
};
