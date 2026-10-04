#pragma once
#include <vector>
#include <memory>
#include <string>
#include "scene/Node.h"
#include "rendering/Light.h"
#include "rendering/Shader.h"
#include <glm/glm.hpp>

struct PhysicsDebugLine;
class ArcRotateCamera;
class DirectionalLight;

class RenderSystem {
public:
    static RenderSystem& getInstance() {
        static RenderSystem instance;
        return instance;
    }

    void init();
    void render(Node* rootNode);
    void renderView(Node* rootNode, const glm::mat4& view,
                     const glm::mat4& projection, unsigned int framebuffer,
                     int width, int height);
    void drawCameraGizmo(const glm::mat4& view, const glm::mat4& projection);
    void updateShadowMap(Node* rootNode);

    void updateShadowMap(Node* rootNode, const glm::mat4& view);
    void addLight(Light* light) { m_lights.push_back(light); }
    void setDesktopCamera(ArcRotateCamera* camera) { m_desktopCamera = camera; }
    void setSkyboxPath(const std::string& path) { m_skyboxPath = path; }
    DirectionalLight* getDirectionalLight() const;

    bool shadowsEnabled = true;
    bool frustumCullingEnabled = true;

    bool fxaaEnabled = true;

    void applyFXAA(
        unsigned int targetFramebuffer,
        unsigned int sourceFramebuffer,
        int width,
        int height
    );

private:
    RenderSystem() = default;
    std::vector<Light*> m_lights;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_debugShader;
    std::unique_ptr<Shader> m_skyboxShader;
    std::unique_ptr<Shader> m_shadowShader;
    unsigned int m_debugVao = 0;
    unsigned int m_debugVbo = 0;
    unsigned int m_skyboxVao = 0;
    unsigned int m_skyboxVbo = 0;
    unsigned int m_skyboxTexture = 0;
    unsigned int m_shadowMap = 0;
    std::string m_skyboxPath;
    ArcRotateCamera* m_desktopCamera = nullptr;
    void traverseAndRender(Node* node, const glm::mat4& view, const glm::mat4& proj, bool isShadowPass, const glm::mat4& lightSpaceMatrix);
    void renderCollisionDebug(const glm::mat4& view, const glm::mat4& projection);
    void renderSkybox(const glm::mat4& view, const glm::mat4& projection);
    void renderShadowMap(Node* rootNode, DirectionalLight* light);
    
    unsigned int m_shadowFbo = 0;
    glm::mat4 m_lightSpaceMatrix = glm::mat4(1.0f);
    bool m_hasLightSpaceMatrix = false;

    glm::mat4 computeDirectionalLightSpaceMatrix(
        const glm::vec3& focusPosition,
        DirectionalLight* light
    ) const;

    void renderShadowMap(
        Node* rootNode,
        DirectionalLight* light,
        const glm::mat4& lightSpaceMatrix
    );

    void ensureFXAAResources(int width, int height);

    std::unique_ptr<Shader> m_fxaaShader;

    unsigned int m_fxaaFbo = 0;
    unsigned int m_fxaaTexture = 0;
    unsigned int m_fxaaVao = 0;

    int m_fxaaWidth = 0;
    int m_fxaaHeight = 0;
};
