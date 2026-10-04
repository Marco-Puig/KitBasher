#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstdint>
#include <vector>

#ifdef KITBASHER_ENABLE_OPENXR
#include <openxr/openxr.h>
#endif

class Window;

struct XRControllerState {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    float trigger = 0.0f;
    float grip = 0.0f;
    bool select = false;
    bool menu = false;
};

class XRManager {
public:
    static XRManager& getInstance();

    bool init(Window& window);
    void shutdown();
    bool isRunning() const { return m_running; }
    bool isSimulated() const { return m_simulationRequested || !m_running; }
    bool setSimulationMode(bool enabled, Window& window);

    bool beginFrame();
    void pollEvents();
    bool acquireView(uint32_t eye, glm::mat4& view, glm::mat4& projection);
    void releaseView(uint32_t eye);
    uint32_t getViewTexture(uint32_t eye) const;
    glm::mat4 getHeadViewMatrix() const;
    uint32_t getViewWidth(uint32_t eye) const;
    uint32_t getViewHeight(uint32_t eye) const;
    void endFrame();
    void syncActions();

    glm::mat4 getViewMatrix() const { return m_simulationView; }
    glm::mat4 getProjectionMatrix() const { return m_simulationProjection; }
    bool controllerButtonPressed(uint32_t controller, uint32_t button) const;
    XRControllerState getControllerState(uint32_t controller) const;
    glm::vec2 getThumbstick(uint32_t controller) const;

private:
    XRManager() = default;
    ~XRManager();
    XRManager(const XRManager&) = delete;
    XRManager& operator=(const XRManager&) = delete;

#ifdef KITBASHER_ENABLE_OPENXR
    bool createInstance();
    bool createSession(Window& window);
    bool createActions();
    bool createSwapchains();
    void destroySessionResources();
    bool check(XrResult result, const char* operation) const;

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_system = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_stageSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;
    XrSpace m_leftHandSpace = XR_NULL_HANDLE;
    XrSpace m_rightHandSpace = XR_NULL_HANDLE;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
    XrActionSet m_actionSet = XR_NULL_HANDLE;
    XrAction m_leftPoseAction = XR_NULL_HANDLE;
    XrAction m_rightPoseAction = XR_NULL_HANDLE;
    XrAction m_leftSelectAction = XR_NULL_HANDLE;
    XrAction m_rightSelectAction = XR_NULL_HANDLE;
    XrAction m_handPoseAction = XR_NULL_HANDLE;
    XrPath m_leftHandPath = XR_NULL_PATH;
    XrPath m_rightHandPath = XR_NULL_PATH;
    std::array<XrSwapchain, 2> m_swapchains{XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::array<std::vector<uint32_t>, 2> m_swapchainImages;
    std::array<XrView, 2> m_views{XrView{XR_TYPE_VIEW}, XrView{XR_TYPE_VIEW}};
    XrViewConfigurationType m_viewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    bool m_sessionReady = false;
    bool m_frameBegun = false;
    std::array<bool, 2> m_acquired{false, false};
    std::array<uint32_t, 2> m_acquiredImage{0, 0};
    XrAction m_leftThumbstickAction = XR_NULL_HANDLE;
    XrAction m_rightThumbstickAction = XR_NULL_HANDLE;
#endif

    std::array<uint32_t, 2> m_viewWidths{0, 0};
    std::array<uint32_t, 2> m_viewHeights{0, 0};
    bool m_running = false;
    bool m_simulationRequested = false;
    Window* m_simulationWindow = nullptr;
    std::array<XRControllerState, 2> m_simulatedControllers{};
    glm::mat4 m_simulationView{1.0f};
    glm::mat4 m_simulationProjection{1.0f};
};
