#include "HandController.h"
#include "JoystickNode.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

HandController::HandController(const std::string& name, bool isLeftHand) 
    : MeshNode(name), m_isLeftHand(isLeftHand) {
    createCubeMesh();
    
    Material mat;
    mat.baseColorFactor = isLeftHand ? glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
    setMaterial(mat);
}

void HandController::updateHand(const glm::vec3& pos, const glm::quat& rot, bool grip, bool trigger, float gripVal, float triggerVal) {
    setPosition(pos);
    setRotation(rot);
    m_gripPressed = grip;
    m_triggerPressed = trigger;
    m_gripValue = gripVal;
    m_triggerValue = triggerVal;
}

void HandController::attachJoystick(JoystickNode* joystick) {
    m_heldJoystick = joystick;
}

void HandController::detachJoystick() {
    m_heldJoystick = nullptr;
}

void HandController::loadHandModel(const std::string& path) {
    // TODO: Implement GLB hand model loading and replacement of primitive cube
}

void HandController::createCubeMesh() {
    // Small cube: 6cm x 4cm x 10cm (0.06, 0.04, 0.10)
    float w = 0.03f, h = 0.02f, d = 0.05f;

    std::vector<float> vertices = {
        // Pos                // Norm               // UV
        -w, -h,  d,  0, 0, 1,  0, 0,
         w, -h,  d,  0, 0, 1,  1, 0,
         w,  h,  d,  0, 0, 1,  1, 1,
        -w,  h,  d,  0, 0, 1,  0, 1,

        -w, -h, -d,  0, 0, -1, 0, 0,
         w, -h, -d,  0, 0, -1, 1, 0,
         w,  h, -d,  0, 0, -1, 1, 1,
        -w,  h, -d,  0, 0, -1, 0, 1,
    };

    std::vector<unsigned int> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        4, 0, 3, 3, 7, 4,
        1, 5, 6, 6, 2, 1,
        3, 2, 6, 6, 7, 3,
        0, 4, 7, 7, 3, 0
    };

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
    setMesh(vao, vbo, ebo, indices.size(), true);
    setBounds(glm::vec3(-w, -h, -d), glm::vec3(w, h, d));
}
