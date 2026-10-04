#pragma once

#include "scene/MeshNode.h"
#include <glm/glm.hpp>

class HandController;

class JoystickNode : public MeshNode {
public:
    JoystickNode(const std::string& name, bool isLeftJoystick);
    ~JoystickNode() override = default;

    void setHeld(bool held) { m_isHeld = held; }
    bool isHeld() const { return m_isHeld; }
    
    void followHand(const glm::vec3& handPos, const glm::quat& handRot);
    
    glm::vec3 getHomePosition() const { return m_homePosition; }
    void setHomePosition(glm::vec3 pos) { m_homePosition = pos; }
    
    float getGrabRadius() const { return m_grabRadius; }

private:
    void createJoystickMesh();

    glm::vec3 m_homePosition{0.0f};
    bool m_isHeld = false;
    float m_grabRadius = 0.1f;
};
