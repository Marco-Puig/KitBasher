#include "SceneEditor.h"
#include "Window.h"
#include "core/Version.h"
#include "xr/XRManager.h"
#include "PhysicsSystem.h"
#include "scene/Node.h"
#include "scene/ArcRotateCamera.h"
#include "rendering/RenderSystem.h"
#include "rendering/Light.h"
#include "scene/MeshNode.h"
#include "scene/Animator.h"

#if defined(__has_include)
#  if __has_include("net/NetworkManager.h")
#    include "net/NetworkManager.h"
#    define CHISEL_SCENE_EDITOR_HAS_NETWORK 1
#  endif
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstring>
#include <cfloat>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <iostream>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#endif

SceneEditor& SceneEditor::getInstance() {
    static SceneEditor instance;
    return instance;
}

SceneEditor::~SceneEditor() {
    shutdown();
}

bool SceneEditor::isGizmoCapturingMouse() {
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || ImGuizmo::IsUsing() || ImGuizmo::IsOver();
}

void SceneEditor::init(Window& window) {
    if (m_initialized)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    const std::string fontPath = "resources/Roboto-Regular.ttf";
    ImGuiIO& io = ImGui::GetIO();
    std::ifstream fontFile(fontPath, std::ios::binary);
    unsigned char signature[4] = {};
    if (fontFile.read(reinterpret_cast<char*>(signature), sizeof(signature)) &&
        ((signature[0] == 0x00 && signature[1] == 0x01 &&
          signature[2] == 0x00 && signature[3] == 0x00) ||
         (signature[0] == 't' && signature[1] == 'r' &&
          signature[2] == 'u' && signature[3] == 'e'))) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 17.0f);
        if (font == nullptr)
            throw std::runtime_error("Failed to load Scene Editor font: " + fontPath);
    } else {
        std::cerr << "[ImGui] Invalid or missing font '" << fontPath
                  << "'; using the default font\n";
    }
    ImGui_ImplGlfw_InitForOpenGL(window.getHandle(), true);
    ImGui_ImplOpenGL3_Init("#version 450 core");
    querySystemInfo();
    m_initialized = true;
}

void SceneEditor::shutdown() {
    if (!m_initialized)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void SceneEditor::beginFrame() {
    if (!m_initialized)
        return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void SceneEditor::updateGizmo(Node* sceneRoot, ArcRotateCamera* camera) {
    m_gizmoCapturingMouse = false;
    selectLightAtCursor(sceneRoot, camera);
    Node* previousNode = m_manipulatedNode;
    if (m_manipulatedNode != nullptr && m_manipulatedNode != m_selectedNode) {
        PhysicsSystem::getInstance().endEditorManipulation(m_manipulatedNode);
        m_manipulatedNode->setEditorManipulated(false);
    }
    if (!m_visible || m_selectedNode == nullptr || camera == nullptr) {
        if (m_manipulatedNode != nullptr) {
            PhysicsSystem::getInstance().endEditorManipulation(m_manipulatedNode);
            m_manipulatedNode->setEditorManipulated(false);
        }
        m_manipulatedNode = nullptr;
        return;
    }
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGuizmo::SetRect(0.0f, 0.0f, displaySize.x, displaySize.y);
    glm::mat4 transform = m_selectedNode->getWorldTransform();
    float matrix[16];
    std::memcpy(matrix, glm::value_ptr(transform), sizeof(matrix));
    const glm::mat4 view = camera->getViewMatrix();
    const glm::mat4 projection = camera->getProjectionMatrix();
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                         m_gizmoOperation == 0 ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE,
                         ImGuizmo::WORLD, matrix);
    const bool usingGizmo = ImGuizmo::IsUsing();
    if (!usingGizmo && previousNode != nullptr)
        PhysicsSystem::getInstance().endEditorManipulation(previousNode);
    if (usingGizmo && previousNode != m_selectedNode)
        PhysicsSystem::getInstance().beginEditorManipulation(m_selectedNode);
    m_selectedNode->setEditorManipulated(usingGizmo);
    m_manipulatedNode = usingGizmo ? m_selectedNode : nullptr;
    if (usingGizmo) {
        glm::mat4 edited;
        std::memcpy(glm::value_ptr(edited), matrix, sizeof(matrix));
        if (m_selectedNode->getParent() != nullptr)
            edited = glm::inverse(m_selectedNode->getParent()->getWorldTransform()) * edited;
        glm::vec3 translation;
        glm::vec3 rotation;
        glm::vec3 scale;
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(edited), &translation.x,
                                               &rotation.x, &scale.x);
        m_selectedNode->setPosition(translation);
        m_selectedNode->setRotation(glm::quat(glm::radians(rotation)));
    }
    m_gizmoCapturingMouse = usingGizmo || ImGuizmo::IsOver();
}

void SceneEditor::selectLightAtCursor(Node* sceneRoot, ArcRotateCamera* camera) {
    if (!m_visible || sceneRoot == nullptr || camera == nullptr)
        return;
    const ImGuiIO& io = ImGui::GetIO();
    const glm::mat4 view = camera->getViewMatrix();
    const glm::mat4 projection = camera->getProjectionMatrix();
    Node* hit = nullptr;
    float bestDistance = 12.0f;
    std::function<void(Node*)> visit = [&](Node* node) {
        if (auto* light = dynamic_cast<Light*>(node)) {
            const glm::vec4 clip = projection * view *
                                   glm::vec4(glm::vec3(node->getWorldTransform()[3]), 1.0f);
            if (clip.w > 0.0f) {
                const glm::vec2 screen(
                    (clip.x / clip.w * 0.5f + 0.5f) * io.DisplaySize.x,
                    (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * io.DisplaySize.y);
                const float distance = glm::length(screen - glm::vec2(io.MousePos.x, io.MousePos.y));
                ImDrawList* drawList = ImGui::GetForegroundDrawList();
                const ImVec2 icon(screen.x, screen.y);
                drawList->AddCircleFilled(icon, 7.0f,
                    node == m_selectedNode ? IM_COL32(255, 220, 80, 255)
                                           : IM_COL32(255, 180, 40, 220));
                drawList->AddLine(ImVec2(screen.x - 11.0f, screen.y),
                                  ImVec2(screen.x + 11.0f, screen.y),
                                  IM_COL32(255, 230, 120, 220), 1.5f);
                drawList->AddLine(ImVec2(screen.x, screen.y - 11.0f),
                                  ImVec2(screen.x, screen.y + 11.0f),
                                  IM_COL32(255, 230, 120, 220), 1.5f);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    hit = light;
                }
            }
        }
        for (const auto& child : node->getChildren())
            visit(child.get());
    };
    visit(sceneRoot);
    if (hit != nullptr && io.MouseClicked[0] && !io.WantCaptureMouse)
        m_selectedNode = hit;
}

namespace {
void drawNodeList(Node* node, Node*& selected, const Animator* animator, bool forceOpen = false) {
    if (node == nullptr)
        return;
    const bool isSelected = selected == node;
    const std::vector<std::string> animations =
        animator != nullptr ? animator->getAnimationLabels(node) :
                              std::vector<std::string>();
    
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (node->getChildren().empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (isSelected)
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(node);
    if (forceOpen) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    bool open = ImGui::TreeNodeEx(node->getName().c_str(), flags);
    if (ImGui::IsItemClicked())
        selected = node;

    if (!animations.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[ANIM]");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Animations");
            for (const std::string& label : animations)
                ImGui::BulletText("%s", label.c_str());
            ImGui::EndTooltip();
        }
    }

    if (open) {
        for (const auto& child : node->getChildren())
            drawNodeList(child.get(), selected, animator, false);
        ImGui::TreePop();
    }
    ImGui::PopID();
}
}

void SceneEditor::render(Window& window, XRManager& xr, Node* sceneRoot,
                   ArcRotateCamera* camera, Animator* animator,
                   float deltaTime) {
    (void)camera;
    if (!m_initialized)
        return;

    if (!m_modeInitialized) {
        m_simulatedVR = xr.isSimulated();
        m_modeInitialized = true;
    }

    m_displayAccumulator += deltaTime;
    m_pendingFrameTimeMs = deltaTime * 1000.0f;
    m_intervalTime += deltaTime;
    ++m_intervalFrames;
    if (m_intervalTime > 0.0f)
        m_pendingFps = static_cast<float>(m_intervalFrames) / m_intervalTime;
    if (m_displayAccumulator >= 0.25f) {
        m_frameTimeMs = m_pendingFrameTimeMs;
        m_fps = m_pendingFps;
        m_displayAccumulator = 0.0f;
        m_intervalTime = 0.0f;
        m_intervalFrames = 0;
    }

    const bool f1Down = glfwGetKey(window.getHandle(), GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Down && !m_toggleKeyWasDown)
        m_visible = !m_visible;
    m_toggleKeyWasDown = f1Down;

    if (m_visible) {
        ImGui::SetNextWindowSize(ImVec2(380.0f, 550.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 220.0f),
                                             ImVec2(FLT_MAX, FLT_MAX));
        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("Scene Editor", &m_visible, windowFlags)) {
            ImGui::Text("Press F1 to toggle this window");
            ImGui::Separator();
            ImGui::Text("Engine version: %s", ChiselEngine::Version);
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", m_fps);
            ImGui::Text("Frame time: %.3f ms", m_frameTimeMs);
            ImGui::Separator();
            ImGui::Text("GPU: %s", m_gpuName.c_str());
            ImGui::Text("CPU: %s", m_cpuName.c_str());
            ImGui::Text("OS: %s", m_osName.c_str());
            ImGui::Separator();

            if (ImGui::Checkbox("Simulated VR", &m_simulatedVR))
                xr.setSimulationMode(m_simulatedVR, window);
            ImGui::SameLine();
            if (m_simulatedVR)
                ImGui::TextUnformatted("Desktop Simulation");
            else if (xr.isRunning())
                ImGui::TextUnformatted("OpenXR runtime");
            else
                ImGui::TextUnformatted("OpenXR unavailable; desktop fallback");
            if (ImGui::Checkbox("Show Collisions & Additional Gizmos", &m_showCollisionDebug))
                PhysicsSystem::getInstance().setDebugDrawEnabled(m_showCollisionDebug);
            if (ImGui::Checkbox("V-Sync", &m_vsync))
                window.setVSync(m_vsync);
            if (ImGui::Checkbox("Shadows", &RenderSystem::getInstance().shadowsEnabled)) {
                // Shadows enabled/disabled
            }
            if (ImGui::Checkbox("Culling", &RenderSystem::getInstance().frustumCullingEnabled)) {
                // Culling enabled/disabled
            }
            if (ImGui::Checkbox("FXAA", &RenderSystem::getInstance().fxaaEnabled)) {
                // FXAA enabled/disabled
            }

#ifdef CHISEL_SCENE_EDITOR_HAS_NETWORK
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Multiplayer", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& network = net::NetworkManager::getInstance();
                ImGui::Text("Initialized: %s", network.isInitialized() ? "Yes" : "No");
                ImGui::Text("Connected: %s", network.isConnected() ? "Yes" : "No");
                ImGui::Text("Role: %s",
                    network.isHost() ? "Host" :
                    network.isConnected() ? "Client" : "Disconnected"
                );
                ImGui::Text("Player Count: %u", network.getPlayerCount());

                if (!network.isInitialized()) {
                    if (ImGui::Button("Initialize Networking")) {
                        network.init();
                    }
                } else if (!network.isConnected()) {
                    if (ImGui::Button("Host Private Lobby")) {
                        std::string code;
                        if (network.hostPrivateLobby(code)) {
                            std::strncpy(m_lobbyCode, code.c_str(), sizeof(m_lobbyCode) - 1);
                            m_lobbyCode[sizeof(m_lobbyCode) - 1] = '\0';
                        }
                    }

                    ImGui::InputText("Lobby Code", m_lobbyCode, IM_ARRAYSIZE(m_lobbyCode));

                    if (ImGui::Button("Join Private Lobby")) {
                        network.joinPrivateLobby(m_lobbyCode);
                    }
                } else {
                    ImGui::Text("Lobby Code: %s", m_lobbyCode);
                    
                    // --- ADD THIS BUTTON ---
                    if (network.isHost()) {
                        if (ImGui::Button("Invite Friends (Steam)")) {
                            network.showInviteDialog();
                        }
                    }
                    // -----------------------

                    if (ImGui::Button("Disconnect")) {
                        network.shutdown();
                    }
                }
            }
#endif

            if (DirectionalLight* light = dynamic_cast<DirectionalLight*>(m_selectedNode)) {
                float intensity = light->getIntensity();
                float exposure = light->getExposure();
                if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 8.0f, "%.2f"))
                    light->setIntensity(intensity);
                if (ImGui::SliderFloat("Exposure", &exposure, -4.0f, 4.0f, "%.2f EV"))
                    light->setExposure(exposure);
            } else if (PointLight* pLight = dynamic_cast<PointLight*>(m_selectedNode)) {
                float intensity = pLight->getIntensity();
                float exposure = pLight->getExposure();
                float radius = pLight->getRadius();
                if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 8.0f, "%.2f"))
                    pLight->setIntensity(intensity);
                if (ImGui::SliderFloat("Exposure", &exposure, -4.0f, 4.0f, "%.2f EV"))
                    pLight->setExposure(exposure);
                if (ImGui::SliderFloat("Radius", &radius, 0.1f, 100.0f, "%.2f"))
                    pLight->setRadius(radius);
            }
            ImGui::Separator();
            if (ImGui::RadioButton("Move", m_gizmoOperation == 0))
                m_gizmoOperation = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", m_gizmoOperation == 1))
                m_gizmoOperation = 1;
            ImGui::TextUnformatted("Scene nodes");
            ImGui::BeginChild("SceneEditor.NodeList", ImVec2(0.0f, 0.0f), true);
            drawNodeList(sceneRoot, m_selectedNode, animator, true);
            ImGui::EndChild();
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void SceneEditor::querySystemInfo() {
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    m_gpuName = renderer != nullptr ? renderer : "Unknown GPU";
    m_gpuMemory = "Unavailable";

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const std::string extensionList = extensions != nullptr ? extensions : "";
    if (extensionList.find("GL_NVX_gpu_memory_info") != std::string::npos) {
        GLint dedicatedKb = 0;
        glGetIntegerv(0x9047, &dedicatedKb);
        if (dedicatedKb > 0)
            m_gpuMemory = std::to_string(dedicatedKb / 1024) + " MB";
    } else if (extensionList.find("GL_ATI_meminfo") != std::string::npos) {
        GLint dedicatedKb = 0;
        glGetIntegerv(0x87FC, &dedicatedKb);
        if (dedicatedKb > 0)
            m_gpuMemory = std::to_string(dedicatedKb) + " MB";
    }

#ifdef _WIN32
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    std::ostringstream cpu;
    cpu << systemInfo.dwNumberOfProcessors << " logical processors";
    m_cpuName = cpu.str();
    m_osName = "Windows";
#else
    m_cpuName = "Unknown CPU";
    m_osName = "Unknown OS";
#endif
}