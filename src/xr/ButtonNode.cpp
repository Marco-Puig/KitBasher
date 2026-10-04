#include "ButtonNode.h"
#include <glad/glad.h>

ButtonNode::ButtonNode(const std::string& name) : MeshNode(name) {
    createButtonMesh();
    updateColor();
}

void ButtonNode::updateColor() {
    Material mat = getMaterial();
    mat.baseColorFactor = m_isPressed ? m_pressedColor : m_unpressedColor;
    setMaterial(mat);
}

void ButtonNode::createButtonMesh() {
    float s = 0.05f;
    std::vector<float> vertices = {
        -s, -s,  s,  0, 0, 1,  0, 0,
         s, -s,  s,  0, 0, 1,  1, 0,
         s,  s,  s,  0, 0, 1,  1, 1,
        -s,  s,  s,  0, 0, 1,  0, 1,

        -s, -s, -s,  0, 0, -1, 0, 0,
         s, -s, -s,  0, 0, -1, 1, 0,
         s,  s, -s,  0, 0, -1, 1, 1,
        -s,  s, -s,  0, 0, -1, 0, 1,
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
    setBounds(glm::vec3(-s, -s, -s), glm::vec3(s, s, s));
}
