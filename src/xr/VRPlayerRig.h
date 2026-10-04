#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "VRInput.h"

class VRPlayerRig {
public:
    static VRPlayerRig& getInstance();

    void update(float dt, const VRInputFrame& input, const glm::mat4& rawHeadView);
    glm::mat4 applyToView(const glm::mat4& rawXRView) const;

    glm::mat4 getRigToWorld() const;
    glm::mat4 getWorldToRig() const;

    void setPosition(float x, float y, float z);
    float getPositionX() const;
    float getPositionY() const;
    float getPositionZ() const;
    
    void setYaw(float yaw);
    float getYaw() const;

    void setMoveSpeed(float speed);
    float getMoveSpeed() const;

    void setSnapTurn(bool enabled);
    bool isSnapTurnEnabled() const;

    // Tuning
    bool useSnapTurn = true;
    float moveSpeed = 2.0f;
    float smoothTurnSpeed = 1.8f;
    float snapTurnAngle = 0.5235987756f;

private:
    VRPlayerRig() = default;
    ~VRPlayerRig() = default;
    VRPlayerRig(const VRPlayerRig&) = delete;
    VRPlayerRig& operator=(const VRPlayerRig&) = delete;

    static glm::vec2 applyDeadzone(glm::vec2 value, float deadzone = 0.15f);

    glm::vec3 m_position{0.0f, 0.0f, 0.0f};
    float m_yaw = 0.0f;

    bool m_snapTurnLeftActive = false;
    bool m_snapTurnRightActive = false;
};