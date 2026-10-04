#include "RenderSystem.h"
#include "scene/MeshNode.h"
#include "platform/PhysicsSystem.h"
#include "scene/ArcRotateCamera.h"
#include "rendering/Light.h"
#include "xr/XRManager.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr int kShadowMapSize = 4096;
constexpr float kShadowHalfExtent = 8.0f;
constexpr float kShadowFocusDistance = 5.0f;
constexpr float kLightDistance = 30.0f;

glm::vec3 computeShadowFocusPosition(const glm::mat4& view) {
    const glm::mat4 invView = glm::inverse(view);
    const glm::vec3 cameraPosition = glm::vec3(invView[3]);

    glm::vec3 forward = -glm::vec3(invView[2]);

    if (glm::length(forward) < 1e-5f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward = glm::normalize(forward);
    }

    return cameraPosition + forward * kShadowFocusDistance;
}

struct Plane {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;

    float signedDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }
};

struct Frustum {
    Plane planes[6];

    static Frustum fromMatrix(const glm::mat4& m) {
        Frustum frustum;

        frustum.planes[0].normal = glm::vec3(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0]);
        frustum.planes[0].distance = m[3][3] + m[3][0];

        frustum.planes[1].normal = glm::vec3(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0]);
        frustum.planes[1].distance = m[3][3] - m[3][0];

        frustum.planes[2].normal = glm::vec3(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1]);
        frustum.planes[2].distance = m[3][3] + m[3][1];

        frustum.planes[3].normal = glm::vec3(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1]);
        frustum.planes[3].distance = m[3][3] - m[3][1];

        frustum.planes[4].normal = glm::vec3(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2]);
        frustum.planes[4].distance = m[3][3] + m[3][2];

        frustum.planes[5].normal = glm::vec3(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2]);
        frustum.planes[5].distance = m[3][3] - m[3][2];

        for (auto& plane : frustum.planes) {
            const float length = glm::length(plane.normal);

            if (length > 0.0f) {
                plane.normal /= length;
                plane.distance /= length;
            }
        }

        return frustum;
    }

    bool isBoxVisible(
        const glm::vec3& minBounds,
        const glm::vec3& maxBounds,
        const glm::mat4& worldTransform
    ) const {
        const glm::vec3 corners[8] = {
            {minBounds.x, minBounds.y, minBounds.z},
            {maxBounds.x, minBounds.y, minBounds.z},
            {minBounds.x, maxBounds.y, minBounds.z},
            {maxBounds.x, maxBounds.y, minBounds.z},
            {minBounds.x, minBounds.y, maxBounds.z},
            {maxBounds.x, minBounds.y, maxBounds.z},
            {minBounds.x, maxBounds.y, maxBounds.z},
            {maxBounds.x, maxBounds.y, maxBounds.z}
        };

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());

        for (const auto& corner : corners) {
            const glm::vec3 transformed = glm::vec3(worldTransform * glm::vec4(corner, 1.0f));

            worldMin = glm::min(worldMin, transformed);
            worldMax = glm::max(worldMax, transformed);
        }

        const glm::vec3 center = (worldMin + worldMax) * 0.5f;
        const glm::vec3 extents = (worldMax - worldMin) * 0.5f;

        for (int i = 0; i < 6; ++i) {
            const float r =
                extents.x * std::abs(planes[i].normal.x) +
                extents.y * std::abs(planes[i].normal.y) +
                extents.z * std::abs(planes[i].normal.z);

            if (planes[i].signedDistance(center) < -r) {
                return false;
            }
        }

        return true;
    }
};

}

DirectionalLight* RenderSystem::getDirectionalLight() const {
    for (Light* light : m_lights) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(light)) {
            return directional;
        }
    }

    return nullptr;
}

namespace {

const char* fxaaVertexShaderSource = R"glsl(#version 450 core
out vec2 vUv;

void main() {
    vec2 vertex = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);

    vUv = vertex;

    gl_Position = vec4(vertex * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char* fxaaFragmentShaderSource = R"glsl(#version 450 core
in vec2 vUv;

uniform sampler2D uTexture;
uniform float uTexelWidth;
uniform float uTexelHeight;

out vec4 FragColor;

float luma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texel = vec2(uTexelWidth, uTexelHeight);

    float lumaNW = luma(texture(uTexture, vUv + vec2(-1.0, -1.0) * texel).rgb);
    float lumaNE = luma(texture(uTexture, vUv + vec2( 1.0, -1.0) * texel).rgb);
    float lumaSW = luma(texture(uTexture, vUv + vec2(-1.0,  1.0) * texel).rgb);
    float lumaSE = luma(texture(uTexture, vUv + vec2( 1.0,  1.0) * texel).rgb);
    float lumaM  = luma(texture(uTexture, vUv).rgb);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float fxaaReduceMin = 1.0 / 128.0;
    const float fxaaReduceMul = 1.0 / 8.0;
    const float fxaaSpanMax = 8.0;

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * fxaaReduceMul,
        fxaaReduceMin
    );

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(
        vec2(fxaaSpanMax),
        max(vec2(-fxaaSpanMax), dir * rcpDirMin)
    ) * texel;

    vec3 rgbA = 0.5 * (
        texture(uTexture, vUv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(uTexture, vUv + dir * (2.0 / 3.0 - 0.5)).rgb
    );

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(uTexture, vUv + dir * -0.5).rgb +
        texture(uTexture, vUv + dir *  0.5).rgb
    );

    float lumaB = luma(rgbB);

    if (lumaB < lumaMin || lumaB > lumaMax) {
        FragColor = vec4(rgbA, 1.0);
    } else {
        FragColor = vec4(rgbB, 1.0);
    }
}
)glsl";

}

void RenderSystem::init() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glClearColor(0.08f, 0.1f, 0.14f, 1.0f);

    m_shader = std::make_unique<Shader>(
        R"glsl(#version 450 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpaceMatrix;

out vec3 vNormal;
out vec3 vWorldPosition;
out vec2 vTexCoord;
out vec4 vFragPosLightSpace;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);

    vWorldPosition = world.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vTexCoord = aTexCoord;
    vFragPosLightSpace = uLightSpaceMatrix * world;

    gl_Position = uProjection * uView * world;
}
)glsl",

        R"glsl(#version 450 core
in vec3 vNormal;
in vec3 vWorldPosition;
in vec2 vTexCoord;
in vec4 vFragPosLightSpace;

uniform vec3 uLightPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uCameraPosition;

uniform float uLightIntensity;
uniform float uLightExposure;
uniform int uLightType;
uniform float uLightRadius;

uniform vec4 uBaseColorFactor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform vec3 uEmissiveFactor;
uniform float uNormalScale;
uniform float uOcclusionStrength;

uniform bool uHasBaseColorTexture;
uniform sampler2D uBaseColorTexture;

uniform bool uHasMetallicRoughnessTexture;
uniform sampler2D uMetallicRoughnessTexture;

uniform bool uHasNormalTexture;
uniform sampler2D uNormalTexture;

uniform bool uHasEmissiveTexture;
uniform sampler2D uEmissiveTexture;

uniform bool uHasOcclusionTexture;
uniform sampler2D uOcclusionTexture;

uniform int uShadowsEnabled;
uniform sampler2D uShadowMap;

out vec4 FragColor;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    float shadow = 0.0;

    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }

    shadow /= 9.0;
    return shadow;
}

void main() {
    vec4 base = uBaseColorFactor;

    if (uHasBaseColorTexture) {
        base *= texture(uBaseColorTexture, vTexCoord);
    }

    float metallic = uMetallicFactor;
    float rough = uRoughnessFactor;

    if (uHasMetallicRoughnessTexture) {
        vec4 mr = texture(uMetallicRoughnessTexture, vTexCoord);
        rough *= mr.g;
        metallic *= mr.b;
    }

    rough = clamp(rough, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    float ao = 1.0;

    if (uHasOcclusionTexture) {
        float sampledAO = texture(uOcclusionTexture, vTexCoord).r;
        ao = mix(1.0, sampledAO, uOcclusionStrength);
    }

    vec3 n = normalize(vNormal);

    if (uHasNormalTexture) {
        vec3 dp1 = dFdx(vWorldPosition);
        vec3 dp2 = dFdy(vWorldPosition);
        vec2 duv1 = dFdx(vTexCoord);
        vec2 duv2 = dFdy(vTexCoord);

        vec3 t = normalize(dp1 * duv2.y - dp2 * duv1.y);
        vec3 b = normalize(cross(n, t));

        vec3 normalSample = texture(uNormalTexture, vTexCoord).xyz * 2.0 - 1.0;
        normalSample.xy *= uNormalScale;

        n = normalize(mat3(t, b, n) * normalSample);
    }

    vec3 l;
    float attenuation = 1.0;

    if (uLightType == 0) {
        l = normalize(-uLightDirection);
    } else {
        l = normalize(uLightPosition - vWorldPosition);

        float dist = length(uLightPosition - vWorldPosition);
        attenuation = clamp(1.0 - (dist / uLightRadius), 0.0, 1.0);
    }

    vec3 v = normalize(uCameraPosition - vWorldPosition);
    vec3 h = normalize(l + v);

    float diff = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, h), 0.0), mix(128.0, 4.0, rough));

    vec3 specColor = mix(vec3(1.0), base.rgb, metallic);
    float shadow = 0.0;

    if (uShadowsEnabled == 1 && uLightType == 0) {
        shadow = ShadowCalculation(vFragPosLightSpace, n, l);
    }

    vec3 ambient = base.rgb * 0.1 * ao;
    vec3 diffuse = base.rgb * diff * uLightColor;
    vec3 specular = specColor * spec * uLightColor * mix(0.04, 0.96, metallic);

    vec3 lighting =
        ambient +
        (1.0 - shadow) * (diffuse + specular);

    lighting *= uLightIntensity * attenuation;

    vec3 color = vec3(1.0) - exp(-lighting * exp2(uLightExposure));
    vec3 emissive = vec3(0.0);

    if (uHasEmissiveTexture) {
        emissive = texture(uEmissiveTexture, vTexCoord).rgb * uEmissiveFactor;
    } else {
        emissive = uEmissiveFactor;
    }

    color += emissive;

    FragColor = vec4(color, base.a);
}
)glsl"
    );

    m_debugShader = std::make_unique<Shader>(
        R"glsl(#version 450 core
layout(location=0) in vec3 aPosition;

uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)glsl",

        R"glsl(#version 450 core
out vec4 FragColor;

void main() {
    FragColor = vec4(1.0, 0.7, 0.1, 1.0);
}
)glsl"
    );

    glGenVertexArrays(1, &m_debugVao);
    glGenBuffers(1, &m_debugVbo);

    m_skyboxShader = std::make_unique<Shader>(
        R"glsl(#version 450 core
layout(location=0) in vec3 aPosition;

out vec3 vDirection;

uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vDirection = aPosition;

    vec4 p = uProjection * mat4(mat3(uView)) * vec4(aPosition, 1.0);
    gl_Position = p.xyww;
}
)glsl",

        R"glsl(#version 450 core
in vec3 vDirection;

uniform sampler2D uEnvironment;

out vec4 FragColor;

const float PI = 3.14159265359;

void main() {
    vec3 d = normalize(vDirection);

    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = 1.0 - (asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5);

    FragColor = vec4(texture(uEnvironment, vec2(u, v)).rgb, 1.0);
}
)glsl"
    );

    const float cube[] = {
        -1, -1, -1,  1, -1, -1,  1,  1, -1,
         1,  1, -1, -1,  1, -1, -1, -1, -1,

        -1, -1,  1,  1, -1,  1,  1,  1,  1,
         1,  1,  1, -1,  1,  1, -1, -1,  1,

        -1,  1,  1,  1,  1,  1,  1,  1, -1,
         1,  1, -1, -1,  1, -1, -1,  1,  1,

        -1, -1,  1,  1, -1,  1,  1, -1, -1,
         1, -1, -1, -1, -1, -1, -1, -1,  1,

         1, -1,  1,  1,  1,  1,  1,  1, -1,
         1,  1, -1,  1, -1, -1,  1, -1,  1,

        -1, -1, -1, -1,  1, -1, -1,  1,  1,
        -1,  1,  1, -1, -1,  1, -1, -1, -1
    };

    glGenVertexArrays(1, &m_skyboxVao);
    glGenBuffers(1, &m_skyboxVbo);

    glBindVertexArray(m_skyboxVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_skyboxVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    m_shadowShader = std::make_unique<Shader>(
        R"glsl(#version 450 core
layout(location=0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;

void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPosition, 1.0);
}
)glsl",

        R"glsl(#version 450 core
void main() {
}
)glsl"
    );

    glGenTextures(1, &m_shadowMap);
    glBindTexture(GL_TEXTURE_2D, m_shadowMap);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT,
        kShadowMapSize,
        kShadowMapSize,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    const float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &m_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D,
        m_shadowMap,
        0
    );

    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[Render] Shadow framebuffer is incomplete; shadows will be disabled\n";

        glDeleteFramebuffers(1, &m_shadowFbo);
        glDeleteTextures(1, &m_shadowMap);

        m_shadowFbo = 0;
        m_shadowMap = 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_fxaaShader = std::make_unique<Shader>(
        fxaaVertexShaderSource,
        fxaaFragmentShaderSource
    );

    glGenVertexArrays(1, &m_fxaaVao);
}

void RenderSystem::render(Node* rootNode) {
    GLFWwindow* window = glfwGetCurrentContext();

    if (window == nullptr) {
        return;
    }

    const glm::mat4 view = m_desktopCamera != nullptr
        ? m_desktopCamera->getViewMatrix()
        : glm::lookAt(
              glm::vec3(0.0f, 0.0f, 4.0f),
              glm::vec3(0.0f),
              glm::vec3(0.0f, 1.0f, 0.0f)
          );

    const glm::mat4 proj = m_desktopCamera != nullptr
        ? m_desktopCamera->getProjectionMatrix()
        : glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    int width = 0;
    int height = 0;

    glfwGetFramebufferSize(window, &width, &height);

    if (width <= 0 || height <= 0) {
        return;
    }

    if (shadowsEnabled) {
        updateShadowMap(rootNode, view);
    }

    renderView(rootNode, view, proj, 0, width, height);
}

glm::mat4 RenderSystem::computeDirectionalLightSpaceMatrix(
    const glm::vec3& focusPosition,
    DirectionalLight* light
) const {
    if (light == nullptr) {
        return glm::mat4(1.0f);
    }

    glm::vec3 lightDirection = light->getDirection();

    if (glm::length(lightDirection) < 1e-5f) {
        lightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    lightDirection = glm::normalize(lightDirection);

    glm::vec3 up(0.0f, 1.0f, 0.0f);

    if (std::abs(glm::dot(lightDirection, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const glm::vec3 lightPosition = focusPosition - lightDirection * kLightDistance;

    const glm::mat4 lightView = glm::lookAt(
        lightPosition,
        focusPosition,
        up
    );

    const float zNear = 1.0f;
    const float zFar = kLightDistance + kShadowHalfExtent * 4.0f;

    const glm::mat4 lightProjection = glm::ortho(
        -kShadowHalfExtent, kShadowHalfExtent,
        -kShadowHalfExtent, kShadowHalfExtent,
        zNear,
        zFar
    );

    return lightProjection * lightView;
}

void RenderSystem::updateShadowMap(Node* rootNode) {
    if (!shadowsEnabled) {
        return;
    }

    DirectionalLight* light = getDirectionalLight();

    if (light == nullptr) {
        return;
    }

    m_lightSpaceMatrix = computeDirectionalLightSpaceMatrix(glm::vec3(0.0f), light);
    m_hasLightSpaceMatrix = true;

    renderShadowMap(rootNode, light, m_lightSpaceMatrix);
}

void RenderSystem::updateShadowMap(Node* rootNode, const glm::mat4& view) {
    if (!shadowsEnabled) {
        return;
    }

    DirectionalLight* light = getDirectionalLight();

    if (light == nullptr) {
        return;
    }

    const glm::vec3 focusPosition = computeShadowFocusPosition(view);

    m_lightSpaceMatrix = computeDirectionalLightSpaceMatrix(focusPosition, light);
    m_hasLightSpaceMatrix = true;

    renderShadowMap(rootNode, light, m_lightSpaceMatrix);
}

void RenderSystem::renderShadowMap(Node* rootNode, DirectionalLight* light) {
    if (!shadowsEnabled || light == nullptr) {
        return;
    }

    m_lightSpaceMatrix = computeDirectionalLightSpaceMatrix(glm::vec3(0.0f), light);
    m_hasLightSpaceMatrix = true;

    renderShadowMap(rootNode, light, m_lightSpaceMatrix);
}

void RenderSystem::renderShadowMap(
    Node* rootNode,
    DirectionalLight* light,
    const glm::mat4& lightSpaceMatrix
) {
    if (light == nullptr) {
        return;
    }

    if (m_shadowShader == nullptr || m_shadowFbo == 0 || m_shadowMap == 0) {
        std::cerr << "[Render] Shadow resources are unavailable; skipping shadow pass\n";
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);

    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glViewport(0, 0, kShadowMapSize, kShadowMapSize);
    glClear(GL_DEPTH_BUFFER_BIT);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    m_shadowShader->use();
    m_shadowShader->setMat4("uLightSpaceMatrix", lightSpaceMatrix);

    traverseAndRender(
        rootNode,
        glm::mat4(1.0f),
        glm::mat4(1.0f),
        true,
        lightSpaceMatrix
    );

    glDisable(GL_POLYGON_OFFSET_FILL);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::renderView(
    Node* rootNode,
    const glm::mat4& view,
    const glm::mat4& proj,
    unsigned int framebuffer,
    int width,
    int height
) {
    if (width <= 0 || height <= 0) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    if (framebuffer == 0) {
        glDrawBuffer(GL_BACK);
    } else {
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderSkybox(view, proj);

    m_shader->use();

    m_shader->setMat4("uView", view);
    m_shader->setMat4("uProjection", proj);

    glm::vec3 lightDirection(0.0f, -1.0f, 0.0f);
    glm::vec3 lightPosition(0.0f, 0.0f, 0.0f);
    glm::vec3 lightColor(1.0f);

    float lightIntensity = 1.0f;
    float lightExposure = 0.0f;
    float lightRadius = 10.0f;

    int lightType = 0;

    glm::mat4 lightSpaceMatrix(1.0f);

    DirectionalLight* directional = getDirectionalLight();

    if (directional != nullptr) {
        lightDirection = directional->getDirection();
        lightColor = directional->getColor();
        lightIntensity = directional->getIntensity();
        lightExposure = directional->getExposure();
        lightRadius = directional->getRadius();
        lightType = 0;

        if (!m_hasLightSpaceMatrix) {
            const glm::vec3 focusPosition = computeShadowFocusPosition(view);
            m_lightSpaceMatrix = computeDirectionalLightSpaceMatrix(focusPosition, directional);
            m_hasLightSpaceMatrix = true;
        }

        lightSpaceMatrix = m_lightSpaceMatrix;
    } else if (!m_lights.empty()) {
        if (auto* point = dynamic_cast<PointLight*>(m_lights.front())) {
            lightPosition = point->getPosition();
            lightColor = point->getColor();
            lightIntensity = point->getIntensity();
            lightExposure = point->getExposure();
            lightRadius = point->getRadius();
            lightType = 1;
        }
    }

    m_shader->setVec3("uLightDirection", lightDirection);
    m_shader->setVec3("uLightPosition", lightPosition);
    m_shader->setVec3("uLightColor", lightColor);
    m_shader->setFloat("uLightIntensity", lightIntensity);
    m_shader->setFloat("uLightExposure", lightExposure);
    m_shader->setFloat("uLightRadius", lightRadius);
    m_shader->setInt("uLightType", lightType);
    m_shader->setMat4("uLightSpaceMatrix", lightSpaceMatrix);

    const bool useShadows =
        shadowsEnabled &&
        directional != nullptr &&
        m_shadowMap != 0 &&
        m_hasLightSpaceMatrix;

    m_shader->setInt("uShadowsEnabled", useShadows ? 1 : 0);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, useShadows ? m_shadowMap : 0);
    m_shader->setInt("uShadowMap", 4);

    m_shader->setVec3("uCameraPosition", glm::vec3(glm::inverse(view)[3]));

    traverseAndRender(
        rootNode,
        view,
        proj,
        false,
        lightSpaceMatrix
    );
    
    renderCollisionDebug(view, proj);
}

void RenderSystem::drawCameraGizmo(const glm::mat4& view, const glm::mat4& projection) {
    glm::mat4 vrHeadView = XRManager::getInstance().getHeadViewMatrix();
    
    // Use the VR head view if it's active, otherwise fallback to desktop camera
    glm::mat4 targetView = (vrHeadView != glm::mat4(1.0f)) ? vrHeadView : view;

    const glm::mat4 invView = glm::inverse(targetView);
    const glm::vec3 pos = glm::vec3(invView[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));
    const glm::vec3 up = glm::normalize(glm::vec3(invView[1]));
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));

    const float size = 0.2f;
    std::vector<glm::vec3> vertices;

    // Camera Pyramid
    vertices.push_back(pos);
    vertices.push_back(pos + (right * size * 0.5f) + (up * size * 0.5f) - (forward * size));
    vertices.push_back(pos);
    vertices.push_back(pos + (-right * size * 0.5f) + (up * size * 0.5f) - (forward * size));
    vertices.push_back(pos);
    vertices.push_back(pos + (right * size * 0.5f) + (-up * size * 0.5f) - (forward * size));
    vertices.push_back(pos);
    vertices.push_back(pos + (-right * size * 0.5f) + (-up * size * 0.5f) - (forward * size));
    
    // Base
    vertices.push_back(pos + (right * size * 0.5f) + (up * size * 0.5f) - (forward * size));
    vertices.push_back(pos + (-right * size * 0.5f) + (up * size * 0.5f) - (forward * size));
    vertices.push_back(pos + (-right * size * 0.5f) + (-up * size * 0.5f) - (forward * size));
    vertices.push_back(pos + (right * size * 0.5f) + (-up * size * 0.5f) - (forward * size));
    vertices.push_back(pos + (right * size * 0.5f) + (-up * size * 0.5f) - (forward * size));
    vertices.push_back(pos + (right * size * 0.5f) + (up * size * 0.5f) - (forward * size));

    if (vertices.empty()) return;

    m_debugShader->use();
    m_debugShader->setMat4("uView", view);
    m_debugShader->setMat4("uProjection", projection);

    glBindVertexArray(m_debugVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_debugVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);

    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
}


void RenderSystem::renderSkybox(const glm::mat4& view, const glm::mat4& projection) {
    if (m_skyboxTexture == 0 && !m_skyboxPath.empty()) {
        int width = 0;
        int height = 0;
        int channels = 0;

        unsigned char* pixels = stbi_load(
            m_skyboxPath.c_str(),
            &width,
            &height,
            &channels,
            3
        );

        if (pixels == nullptr) {
            throw std::runtime_error("Failed to load skybox image: " + m_skyboxPath);
        }

        glGenTextures(1, &m_skyboxTexture);
        glBindTexture(GL_TEXTURE_2D, m_skyboxTexture);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB8,
            width,
            height,
            0,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            pixels
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        stbi_image_free(pixels);
    }

    if (m_skyboxTexture == 0) {
        return;
    }

    if (m_skyboxShader == nullptr) {
        std::cerr << "[Render] Skybox shader is unavailable\n";
        return;
    }

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    m_skyboxShader->use();
    m_skyboxShader->setMat4("uView", view);
    m_skyboxShader->setMat4("uProjection", projection);
    m_skyboxShader->setInt("uEnvironment", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_skyboxTexture);

    const GLboolean cullingEnabled = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(m_skyboxVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    if (cullingEnabled) {
        glEnable(GL_CULL_FACE);
    }

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void RenderSystem::renderCollisionDebug(const glm::mat4& view, const glm::mat4& projection) {
#if defined(_CPPUNWIND)
    try {
#endif
        const std::vector<PhysicsDebugLine> lines = PhysicsSystem::getInstance().getDebugLines();

        if (lines.empty()) {
            return;
        }

        if (m_debugShader == nullptr || m_debugVao == 0 || m_debugVbo == 0) {
            std::cerr << "[Render] Collision debug resources are unavailable\n";
            return;
        }

        if (lines.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max() / 2)) {
            std::cerr << "[Render] Collision debug geometry is too large; skipping\n";
            return;
        }

        std::vector<glm::vec3> vertices;

#if defined(_CPPUNWIND)
        try {
#endif
            vertices.reserve(lines.size() * 2);

            for (const PhysicsDebugLine& line : lines) {
                vertices.push_back(line.from);
                vertices.push_back(line.to);
            }
#if defined(_CPPUNWIND)
        } catch (const std::exception& error) {
            std::cerr << "[Render] Collision debug vertex allocation failed: "
                      << error.what() << '\n';
            return;
        }
#endif

        m_debugShader->use();
        m_debugShader->setMat4("uView", view);
        m_debugShader->setMat4("uProjection", projection);

        glBindVertexArray(m_debugVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_debugVbo);

        glBufferData(
            GL_ARRAY_BUFFER,
            vertices.size() * sizeof(glm::vec3),
            vertices.data(),
            GL_DYNAMIC_DRAW
        );

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glEnableVertexAttribArray(0);

        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));

        glBindVertexArray(0);

        drawCameraGizmo(view, projection);
#if defined(_CPPUNWIND)
    } catch (const std::exception& error) {
        std::cerr << "[Render] Collision debug skipped after exception: "
                  << error.what() << '\n';
    } catch (...) {
        std::cerr << "[Render] Collision debug skipped after unknown exception\n";
    }
#endif
}

void RenderSystem::traverseAndRender(
    Node* node,
    const glm::mat4& view,
    const glm::mat4& proj,
    bool isShadowPass,
    const glm::mat4& lightSpaceMatrix
) {
    if (!node) {
        return;
    }

    if (MeshNode* meshNode = dynamic_cast<MeshNode*>(node)) {
        if (meshNode->getVAO() == 0 || meshNode->getIndexCount() <= 0) {
            for (const auto& child : node->getChildren()) {
                traverseAndRender(child.get(), view, proj, isShadowPass, lightSpaceMatrix);
            }
            return;
        }

        const glm::mat4 worldTransform = node->getWorldTransform();

        if (isShadowPass) {
            if (m_shadowShader == nullptr) {
                return;
            }

            m_shadowShader->use();
            m_shadowShader->setMat4("uModel", worldTransform);
            m_shadowShader->setMat4("uLightSpaceMatrix", lightSpaceMatrix);

            glBindVertexArray(meshNode->getVAO());
            glDrawElements(
                GL_TRIANGLES,
                meshNode->getIndexCount(),
                GL_UNSIGNED_INT,
                nullptr
            );
            glBindVertexArray(0);
        } else {
            if (frustumCullingEnabled) {
                const Frustum camFrustum = Frustum::fromMatrix(proj * view);

                if (!camFrustum.isBoxVisible(
                        meshNode->getBoundsMin(),
                        meshNode->getBoundsMax(),
                        worldTransform
                    )) {
                    for (const auto& child : node->getChildren()) {
                        traverseAndRender(child.get(), view, proj, isShadowPass, lightSpaceMatrix);
                    }

                    return;
                }
            }

            m_shader->use();
            m_shader->setMat4("uModel", worldTransform);

            const Material& material = meshNode->getMaterial();

            m_shader->setVec4("uBaseColorFactor", material.baseColorFactor);
            m_shader->setFloat("uMetallicFactor", material.metallicFactor);
            m_shader->setFloat("uRoughnessFactor", material.roughnessFactor);
            m_shader->setVec3("uEmissiveFactor", material.emissiveFactor);
            m_shader->setFloat("uNormalScale", material.normalScale);
            m_shader->setFloat("uOcclusionStrength", material.occlusionStrength);
            m_shader->setInt("uHasBaseColorTexture", material.hasBaseColorTexture() ? 1 : 0);
            m_shader->setInt("uHasMetallicRoughnessTexture", material.hasMetallicRoughnessTexture() ? 1 : 0);
            m_shader->setInt("uHasNormalTexture", material.hasNormalTexture() ? 1 : 0);
            m_shader->setInt("uHasEmissiveTexture", material.hasEmissiveTexture() ? 1 : 0);
            m_shader->setInt("uHasOcclusionTexture", material.hasOcclusionTexture() ? 1 : 0);

            auto bindMaterialTexture = [](unsigned int texture, int unit) {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, texture);
            };

            bindMaterialTexture(material.baseColorTexture, 0);
            bindMaterialTexture(material.metallicRoughnessTexture, 1);
            bindMaterialTexture(material.normalTexture, 2);
            bindMaterialTexture(material.emissiveTexture, 3);
            bindMaterialTexture(material.occlusionTexture, 5);

            m_shader->setInt("uBaseColorTexture", 0);
            m_shader->setInt("uMetallicRoughnessTexture", 1);
            m_shader->setInt("uNormalTexture", 2);
            m_shader->setInt("uEmissiveTexture", 3);
            m_shader->setInt("uShadowMap", 4);
            m_shader->setInt("uOcclusionTexture", 5);

            glBindVertexArray(meshNode->getVAO());
            glDrawElements(
                GL_TRIANGLES,
                meshNode->getIndexCount(),
                GL_UNSIGNED_INT,
                nullptr
            );
            glBindVertexArray(0);
        }
    }

    for (const auto& child : node->getChildren()) {
        traverseAndRender(child.get(), view, proj, isShadowPass, lightSpaceMatrix);
    }
}

void RenderSystem::ensureFXAAResources(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (m_fxaaFbo == 0) {
        glGenFramebuffers(1, &m_fxaaFbo);
        glGenTextures(1, &m_fxaaTexture);
    }

    if (m_fxaaWidth != width || m_fxaaHeight != height) {
        glBindTexture(GL_TEXTURE_2D, m_fxaaTexture);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fxaaFbo);

        glFramebufferTexture2D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D,
            m_fxaaTexture,
            0
        );

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[Render] FXAA framebuffer is incomplete\n";

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glDeleteFramebuffers(1, &m_fxaaFbo);
            glDeleteTextures(1, &m_fxaaTexture);

            m_fxaaFbo = 0;
            m_fxaaTexture = 0;

            m_fxaaWidth = 0;
            m_fxaaHeight = 0;

            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_fxaaWidth = width;
        m_fxaaHeight = height;
    }
}

void RenderSystem::applyFXAA(
    unsigned int targetFramebuffer,
    unsigned int sourceFramebuffer,
    int width,
    int height
) {
    if (!fxaaEnabled || width <= 0 || height <= 0) {
        return;
    }

    if (m_fxaaShader == nullptr || m_fxaaVao == 0) {
        return;
    }

    ensureFXAAResources(width, height);

    if (m_fxaaFbo == 0 || m_fxaaTexture == 0) {
        return;
    }

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    GLint prevProgram = 0;
    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevTextureBinding = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);

    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);

    if (sourceFramebuffer == 0) {
        glReadBuffer(GL_BACK);
    } else {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fxaaFbo);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBlitFramebuffer(
        0, 0, width, height,
        0, 0, width, height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFramebuffer);

    if (targetFramebuffer == 0) {
        glDrawBuffer(GL_BACK);
    } else {
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    glViewport(0, 0, width, height);

    m_fxaaShader->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fxaaTexture);

    m_fxaaShader->setInt("uTexture", 0);
    m_fxaaShader->setFloat("uTexelWidth", 1.0f / static_cast<float>(width));
    m_fxaaShader->setFloat("uTexelHeight", 1.0f / static_cast<float>(height));

    glBindVertexArray(m_fxaaVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);

    glUseProgram(prevProgram);

    glActiveTexture(prevActiveTexture);
    glBindTexture(GL_TEXTURE_2D, prevTextureBinding);

    if (prevDepthTest) {
        glEnable(GL_DEPTH_TEST);
    }

    if (prevBlend) {
        glEnable(GL_BLEND);
    }

    if (prevCullFace) {
        glEnable(GL_CULL_FACE);
    }

    glDepthMask(prevDepthMask);

    glViewport(
        prevViewport[0],
        prevViewport[1],
        prevViewport[2],
        prevViewport[3]
    );
}