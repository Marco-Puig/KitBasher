#include "XRManager.h"
#include "platform/Window.h"
#include "xr/VRPlayerRig.h"
#include "scene/Node.h"
#include <glm/gtc/matrix_transform.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#include <GLFW/glfw3.h>

#ifdef KITBASHER_ENABLE_OPENXR

#ifdef _WIN32
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <unknwn.h>
#endif

#include <openxr/openxr_platform.h>
#include <glad/glad.h>

#endif

XRManager& XRManager::getInstance() {
    static XRManager instance;
    return instance;
}

XRManager::~XRManager() {
    shutdown();
}

bool XRManager::setSimulationMode(bool enabled, Window& window) {
    if (enabled == m_simulationRequested && (enabled || m_running))
        return true;

    shutdown();

    m_simulationRequested = enabled;
    m_simulationWindow = &window;

    if (enabled)
        return true;

    return init(window);
}

bool XRManager::init(Window& window) {
    m_simulationWindow = &window;

    if (m_simulationRequested)
        return false;

#ifdef KITBASHER_ENABLE_OPENXR
    m_running = false;
    m_sessionReady = false;
    m_frameBegun = false;

    m_acquired[0] = false;
    m_acquired[1] = false;

    m_acquiredImage[0] = 0;
    m_acquiredImage[1] = 0;

    if (createInstance() && createSession(window) && createActions() && createSwapchains()) {
        m_running = true;
        m_sessionReady = false;
        return true;
    }

    destroySessionResources();
#endif

    std::cerr << "OpenXR unavailable; using desktop rendering.\n";
    return false;
}

void XRManager::shutdown() {
#ifdef KITBASHER_ENABLE_OPENXR
    if (m_frameBegun && m_session != XR_NULL_HANDLE && m_sessionReady) {
        endFrame();
    }

    destroySessionResources();
#endif

    m_running = false;
}

bool XRManager::beginFrame() {
#ifdef KITBASHER_ENABLE_OPENXR
    pollEvents();

    if (!m_running || !m_sessionReady)
        return false;

    if (m_frameBegun) {
        endFrame();
    }

    m_acquired[0] = false;
    m_acquired[1] = false;

    m_acquiredImage[0] = 0;
    m_acquiredImage[1] = 0;

    m_frameState = XrFrameState{XR_TYPE_FRAME_STATE};

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    if (!check(xrWaitFrame(m_session, &waitInfo, &m_frameState), "xrWaitFrame"))
        return false;

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!check(xrBeginFrame(m_session, &beginInfo), "xrBeginFrame"))
        return false;

    m_frameBegun = true;

    auto abortBegunFrame = [this]() {
        if (m_session == XR_NULL_HANDLE) {
            m_frameBegun = false;
            return;
        }

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = m_frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = 0;
        endInfo.layers = nullptr;

        check(xrEndFrame(m_session, &endInfo), "xrEndFrame(abortBegunFrame)");
        m_frameBegun = false;
    };

    for (uint32_t eye = 0; eye < 2; ++eye) {
        m_views[eye].type = XR_TYPE_VIEW;
        m_views[eye].next = nullptr;
    }

    XrViewState viewState{XR_TYPE_VIEW_STATE};
    viewState.next = nullptr;

    XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.next = nullptr;
    locateInfo.viewConfigurationType = m_viewConfiguration;
    locateInfo.displayTime = m_frameState.predictedDisplayTime;
    locateInfo.space = m_stageSpace;

    uint32_t count = 0;

    if (!check(
            xrLocateViews(
                m_session,
                &locateInfo,
                &viewState,
                2,
                &count,
                m_views.data()
            ),
            "xrLocateViews"
        )) {
        abortBegunFrame();
        return false;
    }

    if (count != 2) {
        abortBegunFrame();
        return false;
    }

    return true;
#else
    return false;
#endif
}

void XRManager::pollEvents() {
#ifdef KITBASHER_ENABLE_OPENXR
    if (m_instance == XR_NULL_HANDLE)
        return;

    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};

    while (xrPollEvent(m_instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& changed =
                *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);

            m_sessionState = changed.state;

            if (m_sessionState == XR_SESSION_STATE_READY && !m_sessionReady) {
                XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = m_viewConfiguration;

                m_sessionReady = check(
                    xrBeginSession(m_session, &beginInfo),
                    "xrBeginSession"
                );
            } else if (m_sessionState == XR_SESSION_STATE_STOPPING && m_sessionReady) {
                check(xrEndSession(m_session), "xrEndSession");
                m_sessionReady = false;
            } else if (
                m_sessionState == XR_SESSION_STATE_EXITING ||
                m_sessionState == XR_SESSION_STATE_LOSS_PENDING
            ) {
                m_running = false;
                m_sessionReady = false;
                m_frameBegun = false;
            }
        }

        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        event.next = nullptr;
    }
#endif
}

bool XRManager::acquireView(uint32_t eye, glm::mat4& view, glm::mat4& projection) {
#ifdef KITBASHER_ENABLE_OPENXR
    if (!m_frameBegun || eye >= 2 || m_session == XR_NULL_HANDLE)
        return false;

    const XrSwapchain swapchain = m_swapchains[eye];

    if (swapchain == XR_NULL_HANDLE)
        return false;

    uint32_t imageIndex = 0;

    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!check(
            xrAcquireSwapchainImage(swapchain, &acquireInfo, &imageIndex),
            "xrAcquireSwapchainImage"
        )) {
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;

    if (!check(
            xrWaitSwapchainImage(swapchain, &waitInfo),
            "xrWaitSwapchainImage"
        )) {
        return false;
    }

    m_acquired[eye] = true;
    m_acquiredImage[eye] = imageIndex;

    const XrPosef& pose = m_views[eye].pose;

    const glm::quat orientation(
        pose.orientation.w,
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z
    );

    const glm::vec3 position(
        pose.position.x,
        pose.position.y,
        pose.position.z
    );

    const glm::mat4 rotation = glm::mat4_cast(glm::conjugate(orientation));

    view = rotation * glm::translate(glm::mat4(1.0f), -position);

    const XrFovf& fov = m_views[eye].fov;

    const float nearClip = 0.05f;
    const float farClip = 100.0f;

    const float left = nearClip * std::tan(fov.angleLeft);
    const float right = nearClip * std::tan(fov.angleRight);
    const float down = nearClip * std::tan(fov.angleDown);
    const float up = nearClip * std::tan(fov.angleUp);

    projection = glm::frustum(left, right, down, up, nearClip, farClip);

    return true;
#else
    (void)eye;
    (void)view;
    (void)projection;
    return false;
#endif
}

void XRManager::releaseView(uint32_t eye) {
#ifdef KITBASHER_ENABLE_OPENXR
    if (eye >= 2 || !m_acquired[eye])
        return;

    if (m_swapchains[eye] == XR_NULL_HANDLE)
        return;

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

    check(
        xrReleaseSwapchainImage(m_swapchains[eye], &releaseInfo),
        "xrReleaseSwapchainImage"
    );

    m_acquired[eye] = false;
#else
    (void)eye;
#endif
}

uint32_t XRManager::getViewTexture(uint32_t eye) const {
#ifdef KITBASHER_ENABLE_OPENXR
    if (eye >= 2 || !m_acquired[eye])
        return 0;

    const uint32_t imageIndex = m_acquiredImage[eye];

    if (imageIndex >= m_swapchainImages[eye].size())
        return 0;

    return m_swapchainImages[eye][imageIndex];
#else
    (void)eye;
    return 0;
#endif
}

uint32_t XRManager::getViewWidth(uint32_t eye) const {
    return eye < 2 ? m_viewWidths[eye] : 0;
}

uint32_t XRManager::getViewHeight(uint32_t eye) const {
    return eye < 2 ? m_viewHeights[eye] : 0;
}

void XRManager::endFrame() {
#ifdef KITBASHER_ENABLE_OPENXR
    if (!m_frameBegun)
        return;

    if (m_session == XR_NULL_HANDLE) {
        m_frameBegun = false;
        return;
    }

    for (uint32_t eye = 0; eye < 2; ++eye) {
        releaseView(eye);
    }

    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};

    for (uint32_t eye = 0; eye < 2; ++eye) {
        projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projectionViews[eye].next = nullptr;

        projectionViews[eye].pose = m_views[eye].pose;
        projectionViews[eye].fov = m_views[eye].fov;

        projectionViews[eye].subImage.swapchain = m_swapchains[eye];

        projectionViews[eye].subImage.imageRect.offset.x = 0;
        projectionViews[eye].subImage.imageRect.offset.y = 0;

        projectionViews[eye].subImage.imageRect.extent.width =
            static_cast<int32_t>(m_viewWidths[eye]);

        projectionViews[eye].subImage.imageRect.extent.height =
            static_cast<int32_t>(m_viewHeights[eye]);

        projectionViews[eye].subImage.imageArrayIndex = 0;
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.next = nullptr;
    layer.layerFlags = 0;
    layer.space = m_stageSpace;
    layer.viewCount = 2;
    layer.views = projectionViews.data();

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
    };

    endInfo.layers = layers;

    check(xrEndFrame(m_session, &endInfo), "xrEndFrame");

    m_frameBegun = false;
#endif
}


glm::mat4 XRManager::getHeadViewMatrix() const {
#ifdef KITBASHER_ENABLE_OPENXR
    if (!m_sessionReady || m_views[0].type == 0) {
        return glm::mat4(1.0f);
    }

    // The head pose is relative to the stage space.
    // We combine it with the VRPlayerRig's world transform.
    const XrPosef& pose = m_views[0].pose;
    const glm::quat orientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
    const glm::vec3 position(pose.position.x, pose.position.y, pose.position.z);
    
    glm::mat4 localView = glm::translate(glm::mat4(1.0f), -position) * glm::mat4_cast(glm::conjugate(orientation));
    return VRPlayerRig::getInstance().getWorldToRig() * localView;
#else
    return glm::mat4(1.0f);
#endif
}

void XRManager::syncActions() {
    if (isSimulated() && m_simulationWindow != nullptr) {
        GLFWwindow* window = m_simulationWindow->getHandle();

        auto key = [window](int code) {
            return glfwGetKey(window, code) == GLFW_PRESS;
        };

        auto& left = m_simulatedControllers[0];
        auto& right = m_simulatedControllers[1];

        left.position = glm::vec3(
            static_cast<float>(key(GLFW_KEY_D) - key(GLFW_KEY_A)) * 0.03f,
            static_cast<float>(key(GLFW_KEY_E) - key(GLFW_KEY_Q)) * 0.03f,
            static_cast<float>(key(GLFW_KEY_S) - key(GLFW_KEY_W)) * 0.03f
        );

        right.position = glm::vec3(
            static_cast<float>(key(GLFW_KEY_RIGHT) - key(GLFW_KEY_LEFT)) * 0.03f,
            static_cast<float>(key(GLFW_KEY_PAGE_UP) - key(GLFW_KEY_PAGE_DOWN)) * 0.03f,
            static_cast<float>(key(GLFW_KEY_DOWN) - key(GLFW_KEY_UP)) * 0.03f
        );

        left.select = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        right.select = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        left.trigger = left.select ? 1.0f : 0.0f;
        right.trigger = right.select ? 1.0f : 0.0f;

        left.grip = key(GLFW_KEY_LEFT_SHIFT) ? 1.0f : 0.0f;
        right.grip = key(GLFW_KEY_RIGHT_SHIFT) ? 1.0f : 0.0f;

        left.menu = key(GLFW_KEY_TAB);
        right.menu = key(GLFW_KEY_ENTER);

        return;
    }

#ifdef KITBASHER_ENABLE_OPENXR
    if (m_session == XR_NULL_HANDLE)
        return;

    XrActiveActionSet activeSet{m_actionSet, XR_NULL_PATH};

    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;

    check(xrSyncActions(m_session, &syncInfo), "xrSyncActions");
#endif
}

bool XRManager::controllerButtonPressed(uint32_t controller, uint32_t button) const {
    if (isSimulated())
        return controller < 2 && button == 0 && m_simulatedControllers[controller].select;

#ifdef KITBASHER_ENABLE_OPENXR
    if (button != 0 || controller > 1)
        return false;

    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = controller == 0 ? m_leftSelectAction : m_rightSelectAction;
    getInfo.subactionPath = controller == 0 ? m_leftHandPath : m_rightHandPath;

    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};

    return xrGetActionStateBoolean(m_session, &getInfo, &state) == XR_SUCCESS &&
           state.isActive &&
           state.currentState;
#else
    (void)controller;
    (void)button;
    return false;
#endif
}

XRControllerState XRManager::getControllerState(uint32_t controller) const {
    if (controller > 1)
        return {};

    if (isSimulated())
        return m_simulatedControllers[controller];

#ifdef KITBASHER_ENABLE_OPENXR
    XRControllerState state{};
    state.select = controllerButtonPressed(controller, 0);

    XrSpace handSpace = (controller == 0) ? m_leftHandSpace : m_rightHandSpace;
    if (handSpace != XR_NULL_HANDLE) {
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        
        // Use xrLocateSpace. Correct signature for OpenXR 1.0:
        // XrResult xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location);
        
        if (xrLocateSpace(handSpace, m_stageSpace, 0, &location) == XR_SUCCESS && location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
            state.position = glm::vec3(location.pose.position.x, location.pose.position.y, location.pose.position.z);
            state.orientation = glm::quat(location.pose.orientation.w, location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z);
        }
    }

    state.trigger = state.select ? 1.0f : 0.0f;
    
    return state;
#else
    return {};
#endif
}

glm::vec2 XRManager::getThumbstick(uint32_t controller) const {
    if (controller > 1)
        return glm::vec2(0.0f);

    if (isSimulated() && m_simulationWindow != nullptr) {
        GLFWwindow* window = m_simulationWindow->getHandle();

        auto key = [window](int code) {
            return glfwGetKey(window, code) == GLFW_PRESS;
        };

        if (controller == 0) {
            return glm::vec2(
                static_cast<float>(key(GLFW_KEY_D) - key(GLFW_KEY_A)),
                static_cast<float>(key(GLFW_KEY_W) - key(GLFW_KEY_S))
            );
        }

        return glm::vec2(
            static_cast<float>(key(GLFW_KEY_RIGHT) - key(GLFW_KEY_LEFT)),
            static_cast<float>(key(GLFW_KEY_UP) - key(GLFW_KEY_DOWN))
        );
    }

#ifdef KITBASHER_ENABLE_OPENXR
    if (m_session == XR_NULL_HANDLE)
        return glm::vec2(0.0f);

    const XrAction action =
        controller == 0 ? m_leftThumbstickAction : m_rightThumbstickAction;

    if (action == XR_NULL_HANDLE)
        return glm::vec2(0.0f);

    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;

    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};

    if (xrGetActionStateVector2f(m_session, &getInfo, &state) == XR_SUCCESS &&
        state.isActive) {
        return glm::vec2(state.currentState.x, state.currentState.y);
    }
#endif

    return glm::vec2(0.0f);
}

#ifdef KITBASHER_ENABLE_OPENXR

bool XRManager::check(XrResult result, const char* operation) const {
    if (XR_FAILED(result)) {
        std::cerr << operation << " failed: " << result << '\n';
    }

    return XR_SUCCEEDED(result);
}

bool XRManager::createInstance() {
    uint32_t extensionCount = 0;

    if (!check(
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
            "xrEnumerateInstanceExtensionProperties"
        )) {
        return false;
    }

    if (extensionCount == 0)
        return false;

    std::vector<XrExtensionProperties> extensions(
        extensionCount,
        {XR_TYPE_EXTENSION_PROPERTIES}
    );

    if (!check(
            xrEnumerateInstanceExtensionProperties(
                nullptr,
                extensionCount,
                &extensionCount,
                extensions.data()
            ),
            "xrEnumerateInstanceExtensionProperties"
        )) {
        return false;
    }

    bool opengl = false;

    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0) {
            opengl = true;
            break;
        }
    }

    if (!opengl)
        return false;

    const char* names[] = {
        XR_KHR_OPENGL_ENABLE_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = nullptr;

    std::strncpy(
        createInfo.applicationInfo.applicationName,
        "KitBasher",
        XR_MAX_APPLICATION_NAME_SIZE - 1
    );

    std::strncpy(
        createInfo.applicationInfo.engineName,
        "KitBasher",
        XR_MAX_ENGINE_NAME_SIZE - 1
    );

    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = names;

    if (!check(xrCreateInstance(&createInfo, &m_instance), "xrCreateInstance"))
        return false;

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    return check(xrGetSystem(m_instance, &systemInfo, &m_system), "xrGetSystem");
}

bool XRManager::createSession(Window& window) {
    PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements = nullptr;

    if (!check(
            xrGetInstanceProcAddr(
                m_instance,
                "xrGetOpenGLGraphicsRequirementsKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)
            ),
            "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)"
        )) {
        return false;
    }

    XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};

    if (!check(
            getRequirements(m_instance, m_system, &requirements),
            "xrGetOpenGLGraphicsRequirementsKHR"
        )) {
        return false;
    }

#ifdef _WIN32
    glfwMakeContextCurrent(window.getHandle());

    HDC hdc = wglGetCurrentDC();
    HGLRC hglrc = wglGetCurrentContext();

    if (hdc == nullptr || hglrc == nullptr) {
        std::cerr << "No current OpenGL context available for OpenXR session creation.\n";
        return false;
    }

    XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    binding.hDC = hdc;
    binding.hGLRC = hglrc;

    XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
    createInfo.next = &binding;

    if (!check(xrCreateSession(m_instance, &createInfo, &m_session), "xrCreateSession"))
        return false;
#else
    (void)window;
    return false;
#endif

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    if (!check(
            xrCreateReferenceSpace(m_session, &spaceInfo, &m_stageSpace),
            "xrCreateReferenceSpace(stage)"
        )) {
        return false;
    }

    XrReferenceSpaceCreateInfo viewSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    viewSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    viewSpaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    if (!check(
            xrCreateReferenceSpace(m_session, &viewSpaceInfo, &m_viewSpace),
            "xrCreateReferenceSpace(view)"
        )) {
        return false;
    }

    // Create action spaces for hand tracking
    XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};

    actionSpaceInfo.action = m_handPoseAction;
    actionSpaceInfo.subactionPath = m_leftHandPath;
    if (!check(xrCreateActionSpace(m_session, &actionSpaceInfo, &m_leftHandSpace), "xrCreateActionSpace(left)")) {
        return false;
    }

    actionSpaceInfo.subactionPath = m_rightHandPath;
    if (!check(xrCreateActionSpace(m_session, &actionSpaceInfo, &m_rightHandSpace), "xrCreateActionSpace(right)")) {
        return false;
    }

    return true;
}

namespace {

XrPath chiselXrMakePath(XrInstance instance, const char* pathString) {
    XrPath path = XR_NULL_PATH;

    if (XR_FAILED(xrStringToPath(instance, pathString, &path))) {
        return XR_NULL_PATH;
    }

    return path;
}

bool chiselXrAddBinding(
    XrActionSuggestedBinding* bindings,
    uint32_t& count,
    uint32_t capacity,
    XrAction action,
    XrPath path
) {
    if (bindings == nullptr) {
        return false;
    }

    if (count >= capacity) {
        return false;
    }

    if (action == XR_NULL_HANDLE || path == XR_NULL_PATH) {
        return false;
    }

    bindings[count].action = action;
    bindings[count].binding = path;
    ++count;

    return true;
}

bool chiselXrSuggestBindings(
    XrInstance instance,
    const char* profileString,
    const XrActionSuggestedBinding* bindings,
    uint32_t count
) {
    const XrPath profile = chiselXrMakePath(instance, profileString);

    if (profile == XR_NULL_PATH || count == 0 || bindings == nullptr) {
        return false;
    }

    XrInteractionProfileSuggestedBinding suggested{
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
    };

    suggested.interactionProfile = profile;
    suggested.countSuggestedBindings = count;
    suggested.suggestedBindings = bindings;

    return XR_SUCCEEDED(
        xrSuggestInteractionProfileBindings(instance, &suggested)
    );
}

}

bool XRManager::createActions() {
    m_leftHandPath = chiselXrMakePath(m_instance, "/user/hand/left");
    m_rightHandPath = chiselXrMakePath(m_instance, "/user/hand/right");

    if (m_leftHandPath == XR_NULL_PATH || m_rightHandPath == XR_NULL_PATH) {
        return false;
    }

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(setInfo.actionSetName, "gameplay");
    std::strcpy(setInfo.localizedActionSetName, "Gameplay");
    setInfo.priority = 0;

    if (!check(xrCreateActionSet(m_instance, &setInfo, &m_actionSet), "xrCreateActionSet")) {
        return false;
    }

    const XrPath subactionPaths[] = {
        m_leftHandPath,
        m_rightHandPath
    };

    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    actionInfo.countSubactionPaths = 2;
    actionInfo.subactionPaths = subactionPaths;

    std::strcpy(actionInfo.actionName, "select");
    std::strcpy(actionInfo.localizedActionName, "Select");

    if (!check(
            xrCreateAction(m_actionSet, &actionInfo, &m_leftSelectAction),
            "xrCreateAction(select)"
        )) {
        return false;
    }

    std::strcpy(actionInfo.actionName, "right_select");
    std::strcpy(actionInfo.localizedActionName, "Right Select");

    if (!check(
            xrCreateAction(m_actionSet, &actionInfo, &m_rightSelectAction),
            "xrCreateAction(right_select)"
        )) {
        return false;
    }

    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;

    std::strcpy(actionInfo.actionName, "hand_pose");
    std::strcpy(actionInfo.localizedActionName, "Hand Pose");

    if (!check(
            xrCreateAction(m_actionSet, &actionInfo, &m_handPoseAction),
            "xrCreateAction(hand_pose)"
        )) {
        return false;
    }

    XrActionCreateInfo thumbstickInfo{XR_TYPE_ACTION_CREATE_INFO};
    thumbstickInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    thumbstickInfo.countSubactionPaths = 0;
    thumbstickInfo.subactionPaths = nullptr;

    std::strcpy(thumbstickInfo.actionName, "left_thumbstick");
    std::strcpy(thumbstickInfo.localizedActionName, "Left Thumbstick");

    if (!check(
            xrCreateAction(m_actionSet, &thumbstickInfo, &m_leftThumbstickAction),
            "xrCreateAction(left_thumbstick)"
        )) {
        return false;
    }

    std::strcpy(thumbstickInfo.actionName, "right_thumbstick");
    std::strcpy(thumbstickInfo.localizedActionName, "Right Thumbstick");

    if (!check(
            xrCreateAction(m_actionSet, &thumbstickInfo, &m_rightThumbstickAction),
            "xrCreateAction(right_thumbstick)"
        )) {
        return false;
    }

    constexpr uint32_t MAX_BINDINGS = 8;
    XrActionSuggestedBinding bindings[MAX_BINDINGS];
    uint32_t count = 0;

    count = 0;
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftSelectAction, chiselXrMakePath(m_instance, "/user/hand/left/input/select/click"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightSelectAction, chiselXrMakePath(m_instance, "/user/hand/right/input/select/click"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/left/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/right/input/grip/pose"));
    if (!chiselXrSuggestBindings(m_instance, "/interaction_profiles/khr/simple_controller", bindings, count)) {
        std::cerr << "Failed to suggest simple controller bindings.\n";
        return false;
    }

    // Oculus Touch
    count = 0;
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftSelectAction, chiselXrMakePath(m_instance, "/user/hand/left/input/trigger/value"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightSelectAction, chiselXrMakePath(m_instance, "/user/hand/right/input/trigger/value"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/left/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/right/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/left/input/thumbstick"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/right/input/thumbstick"));
    chiselXrSuggestBindings(m_instance, "/interaction_profiles/oculus/touch_controller", bindings, count);

    // Valve Index
    count = 0;
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftSelectAction, chiselXrMakePath(m_instance, "/user/hand/left/input/trigger/value"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightSelectAction, chiselXrMakePath(m_instance, "/user/hand/right/input/trigger/value"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/left/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/right/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/left/input/thumbstick"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/right/input/thumbstick"));
    chiselXrSuggestBindings(m_instance, "/interaction_profiles/valve/index_controller", bindings, count);

    // Windows Mixed Reality motion controllers
    count = 0;
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftSelectAction, chiselXrMakePath(m_instance, "/user/hand/left/input/trigger/value"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightSelectAction, chiselXrMakePath(m_instance, "/user/hand/right/input/trigger/value"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/left/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/right/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/left/input/thumbstick"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/right/input/thumbstick"));
    chiselXrSuggestBindings(m_instance, "/interaction_profiles/microsoft/motion_controller", bindings, count);

    // HTC Vive controllers use trackpads instead of thumbsticks
    count = 0;
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftSelectAction, chiselXrMakePath(m_instance, "/user/hand/left/input/trigger/click"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightSelectAction, chiselXrMakePath(m_instance, "/user/hand/right/input/trigger/click"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/left/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_handPoseAction, chiselXrMakePath(m_instance, "/user/hand/right/input/grip/pose"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_leftThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/left/input/trackpad"));
    chiselXrAddBinding(bindings, count, MAX_BINDINGS, m_rightThumbstickAction, chiselXrMakePath(m_instance, "/user/hand/right/input/trackpad"));
    chiselXrSuggestBindings(m_instance, "/interaction_profiles/htc/vive_controller", bindings, count);

    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_actionSet;

    return check(
        xrAttachSessionActionSets(m_session, &attachInfo),
        "xrAttachSessionActionSets"
    );
}

bool XRManager::createSwapchains() {
    uint32_t count = 0;

    if (!check(
            xrEnumerateViewConfigurationViews(
                m_instance,
                m_system,
                m_viewConfiguration,
                0,
                &count,
                nullptr
            ),
            "xrEnumerateViewConfigurationViews(count)"
        ) || count != 2) {
        return false;
    }

    std::vector<XrViewConfigurationView> views(
        count,
        {XR_TYPE_VIEW_CONFIGURATION_VIEW}
    );

    if (!check(
            xrEnumerateViewConfigurationViews(
                m_instance,
                m_system,
                m_viewConfiguration,
                count,
                &count,
                views.data()
            ),
            "xrEnumerateViewConfigurationViews(data)"
        ) || count != 2) {
        return false;
    }

    for (uint32_t eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};

        info.usageFlags =
            XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t formatCount = 0;

        if (!check(
                xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr),
                "xrEnumerateSwapchainFormats(count)"
            )) {
            return false;
        }

        if (formatCount == 0)
            return false;

        std::vector<int64_t> formats(formatCount);

        if (!check(
                xrEnumerateSwapchainFormats(
                    m_session,
                    formatCount,
                    &formatCount,
                    formats.data()
                ),
                "xrEnumerateSwapchainFormats(data)"
            )) {
            return false;
        }

        if (formats.empty())
            return false;

        GLint selectedFormat = static_cast<GLint>(formats.front());

        const auto srgbIt = std::find(
            formats.begin(),
            formats.end(),
            static_cast<int64_t>(GL_SRGB8_ALPHA8)
        );

        const auto rgbaIt = std::find(
            formats.begin(),
            formats.end(),
            static_cast<int64_t>(GL_RGBA8)
        );

        if (srgbIt != formats.end()) {
            selectedFormat = GL_SRGB8_ALPHA8;
        } else if (rgbaIt != formats.end()) {
            selectedFormat = GL_RGBA8;
        }

        info.format = selectedFormat;
        info.sampleCount = 1;
        info.width = views[eye].recommendedImageRectWidth;
        info.height = views[eye].recommendedImageRectHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;

        m_viewWidths[eye] = info.width;
        m_viewHeights[eye] = info.height;

        if (info.width == 0 || info.height == 0)
            return false;

        if (!check(
                xrCreateSwapchain(m_session, &info, &m_swapchains[eye]),
                "xrCreateSwapchain"
            )) {
            return false;
        }

        uint32_t imageCount = 0;

        if (!check(
                xrEnumerateSwapchainImages(
                    m_swapchains[eye],
                    0,
                    &imageCount,
                    nullptr
                ),
                "xrEnumerateSwapchainImages(count)"
            )) {
            return false;
        }

        if (imageCount == 0)
            return false;

        std::vector<XrSwapchainImageOpenGLKHR> images(
            imageCount,
            {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR}
        );

        if (!check(
                xrEnumerateSwapchainImages(
                    m_swapchains[eye],
                    imageCount,
                    &imageCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())
                ),
                "xrEnumerateSwapchainImages(data)"
            )) {
            return false;
        }

        m_swapchainImages[eye].resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i) {
            m_swapchainImages[eye][i] = images[i].image;
        }
    }

    return true;
}

void XRManager::destroySessionResources() {
    if (m_sessionReady && m_session != XR_NULL_HANDLE) {
        xrEndSession(m_session);
    }

    if (m_leftHandSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_leftHandSpace);
        m_leftHandSpace = XR_NULL_HANDLE;
    }

    if (m_rightHandSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_rightHandSpace);
        m_rightHandSpace = XR_NULL_HANDLE;
    }

    for (uint32_t eye = 0; eye < 2; ++eye) {
        if (m_swapchains[eye] != XR_NULL_HANDLE) {
            xrDestroySwapchain(m_swapchains[eye]);
            m_swapchains[eye] = XR_NULL_HANDLE;
        }
    }

    if (m_stageSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_stageSpace);
        m_stageSpace = XR_NULL_HANDLE;
    }

    if (m_viewSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_viewSpace);
        m_viewSpace = XR_NULL_HANDLE;
    }

    if (m_session != XR_NULL_HANDLE) {
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }

    if (m_leftSelectAction != XR_NULL_HANDLE) {
        xrDestroyAction(m_leftSelectAction);
        m_leftSelectAction = XR_NULL_HANDLE;
    }

    if (m_rightSelectAction != XR_NULL_HANDLE) {
        xrDestroyAction(m_rightSelectAction);
        m_rightSelectAction = XR_NULL_HANDLE;
    }

    if (m_handPoseAction != XR_NULL_HANDLE) {
        xrDestroyAction(m_handPoseAction);
        m_handPoseAction = XR_NULL_HANDLE;
    }

    if (m_leftThumbstickAction != XR_NULL_HANDLE) {
        xrDestroyAction(m_leftThumbstickAction);
        m_leftThumbstickAction = XR_NULL_HANDLE;
    }

    if (m_rightThumbstickAction != XR_NULL_HANDLE) {
        xrDestroyAction(m_rightThumbstickAction);
        m_rightThumbstickAction = XR_NULL_HANDLE;
    }

    if (m_actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(m_actionSet);
        m_actionSet = XR_NULL_HANDLE;
    }

    if (m_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }

    m_system = XR_NULL_SYSTEM_ID;

    m_sessionReady = false;
    m_frameBegun = false;

    m_acquired[0] = false;
    m_acquired[1] = false;

    m_acquiredImage[0] = 0;
    m_acquiredImage[1] = 0;
}

#endif