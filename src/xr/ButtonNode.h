#pragma once

#include "scene/MeshNode.h"
#include <glm/glm.hpp>

class ButtonNode : public MeshNode {
public:
    ButtonNode(const std::string& name);
    ~ButtonNode() override = default;

    void press() { m_isPressed = true; updateColor(); }
    void release() { m_isPressed = false; updateColor(); }
    bool isPressed() const { return m_isPressed; }

    float getInteractionRadius() const { return m_interactionRadius; }

private:
    void updateColor();
    void createButtonMesh();

    bool m_isPressed = false;
    float m_interactionRadius = 0.15f;
    glm::vec4 m_unpressedColor{0.7f, 0.7f, 0.7f, 1.0f};
    glm::vec4 m_pressedColor{0.2f, 0.8f, 0.2f, 1.0f};
};
