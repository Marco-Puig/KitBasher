#include "JoystickNode.h"
#include <glad/glad.h>

JoystickNode::JoystickNode(const std::string& name, bool isLeftJoystick) 
    : MeshNode(name) {
    createJoystickMesh();
    
    Material mat;
    mat.baseColorFactor = isLeftJoystick ? glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(0.5f, 0.0f, 0.5f, 1.0f);
    setMaterial(mat);
}

void JoystickNode::followHand(const glm::vec3& handPos, const glm::quat& handRot) {
    setPosition(handPos);
    setRotation(handRot);
}

void JoystickNode::createJoystickMesh() {
    // Stretched cube: 2cm x 2cm x 15cm (0.02, 0.02, 0.15)
    float w = 0.01f, h = 0.01f, d = 0.075f;

    std::vector<float> vertices = {
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
        3, 2, 6, 6, 7, 3,
        0, 4, 7, 7, 3, 0,
        1, 5, 6, 6, 2, 1,
        0, 1, 5, 5, 4, 0
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
