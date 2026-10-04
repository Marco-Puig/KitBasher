#include "Engine.h"
#include "rendering/RenderSystem.h"
#include "audio/Audio.h"
#include "scene/Animator.h"
#include "scene/ParticleSystem.h"
#include "platform/SceneEditor.h"
#include "xr/XRManager.h"
#include "platform/PhysicsSystem.h"
#include "scene/ArcRotateCamera.h"
#include "xr/VRPlayerRig.h"
#include "xr/VRInput.h"
#include "xr/InteractionManager.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <cstdio>

#include "net/NetworkManager.h"
#include "net/ReplicationManager.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
LONG WINAPI engineUnhandledException(EXCEPTION_POINTERS* info) noexcept {
    const DWORD code = info != nullptr && info->ExceptionRecord != nullptr
        ? info->ExceptionRecord->ExceptionCode
        : 0;

    char message[128] = {};
    std::snprintf(
        message,
        sizeof(message),
        "[Engine] unhandled SEH exception: 0x%08lX\n",
        static_cast<unsigned long>(code)
    );

    std::cerr << message;
    std::cerr.flush();

    OutputDebugStringA(message);

    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

[[noreturn]] void engineTerminate() noexcept {
    std::cerr << "[Engine] std::terminate called\n";
    std::cerr.flush();

#ifdef _WIN32
    OutputDebugStringA("[Engine] std::terminate called\n");
#endif

    std::abort();
}

}

void Engine::init(int argc, char** argv) {
    std::set_terminate(engineTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(engineUnhandledException);
#endif
    m_window = std::make_unique<Window>(1600, 900, "Kit Basher");

    // Initialize Steammmm FIRST (Important)
    net::NetworkManager::getInstance().init();
    net::NetworkManager::getInstance().checkForPendingInvite(argc, argv);

    // Initialize Rendering and OpenXR
    RenderSystem::getInstance().init();
    XRManager::getInstance().init(*m_window);
    
    // Initialize Physics and Debug Scene Editor
    PhysicsSystem::getInstance().init();
    SceneEditor::getInstance().init(*m_window);

    if (!AudioSystem::getInstance().init()) {
        std::cerr << "[Engine] Audio unavailable; continuing without sound\n";
    }
}

void Engine::run(IGame* game) {
    game->start();
    net::ReplicationManager::getInstance().initialize(game->getScene());
    XRManager& xr = XRManager::getInstance();

    if (game->getCamera() != nullptr) {
        game->getCamera()->attach(*m_window);
        m_window->setResizeTarget(game->getCamera());
        RenderSystem::getInstance().setDesktopCamera(game->getCamera());
    }

    auto lastTime = std::chrono::high_resolution_clock::now();

    GLuint eyeFbo[2] = {0, 0};
    GLuint eyeDepth[2] = {0, 0};

    glGenFramebuffers(2, eyeFbo);
    glGenRenderbuffers(2, eyeDepth);

    VRPlayerRig& vrRig = VRPlayerRig::getInstance();

    vrRig.useSnapTurn = true;
    vrRig.moveSpeed = 2.0f;
    vrRig.smoothTurnSpeed = 1.8f;

    while (!m_window->shouldClose()) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        m_window->pollEvents();
        AudioSystem::getInstance().update();
        SceneEditor::getInstance().beginFrame();
        SceneEditor::getInstance().updateGizmo(game->getSceneRoot(), game->getCamera());

        game->update(dt);

        if (game->getAnimator() != nullptr) {
            game->getAnimator()->update(dt);
        }

        PhysicsSystem::getInstance().syncAnimationDrivenNodes();
        PhysicsSystem::getInstance().update(dt);
        net::NetworkManager::getInstance().update(dt);
        net::ReplicationManager::getInstance().update(dt);
        updateParticleSystems(game->getSceneRoot(), dt);
        xr.syncActions();

        if (xr.beginFrame()) {
            bool shadowMapPrepared = false;
            bool rigUpdated = false;

            for (uint32_t eye = 0; eye < 2; ++eye) {
                glm::mat4 rawView;
                glm::mat4 projection;

                if (!xr.acquireView(eye, rawView, projection)) {
                    continue;
                }

                if (!rigUpdated) {
                    VRInputFrame input = gatherVRInput(xr);
                    vrRig.update(dt, input, rawView);
                    rigUpdated = true;

                    InteractionManager::getInstance().update(input, vrRig.getRigToWorld());
                }

                const glm::mat4 finalView = vrRig.applyToView(rawView);

                if (!shadowMapPrepared) {
                    RenderSystem::getInstance().updateShadowMap(
                        game->getSceneRoot(),
                        finalView
                    );

                    shadowMapPrepared = true;
                }

                glBindFramebuffer(GL_FRAMEBUFFER, eyeFbo[eye]);

                glFramebufferTexture2D(
                    GL_FRAMEBUFFER,
                    GL_COLOR_ATTACHMENT0,
                    GL_TEXTURE_2D,
                    xr.getViewTexture(eye),
                    0
                );

                glBindRenderbuffer(GL_RENDERBUFFER, eyeDepth[eye]);

                GLint currentWidth = 0;
                glGetRenderbufferParameteriv(
                    GL_RENDERBUFFER,
                    GL_RENDERBUFFER_WIDTH,
                    &currentWidth
                );

                if (currentWidth == 0) {
                    glRenderbufferStorage(
                        GL_RENDERBUFFER,
                        GL_DEPTH_COMPONENT24,
                        static_cast<GLsizei>(xr.getViewWidth(eye)),
                        static_cast<GLsizei>(xr.getViewHeight(eye))
                    );
                }

                glFramebufferRenderbuffer(
                    GL_FRAMEBUFFER,
                    GL_DEPTH_ATTACHMENT,
                    GL_RENDERBUFFER,
                    eyeDepth[eye]
                );

                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    std::cerr << "[Engine] OpenXR framebuffer incomplete for eye "
                              << eye << "\n";

                    xr.releaseView(eye);
                    continue;
                }

                RenderSystem::getInstance().renderView(
                    game->getSceneRoot(),
                    finalView,
                    projection,
                    eyeFbo[eye],
                    static_cast<int>(xr.getViewWidth(eye)),
                    static_cast<int>(xr.getViewHeight(eye))
                );
                
                renderParticleSystems(game->getSceneRoot(), finalView, projection);

                if (RenderSystem::getInstance().fxaaEnabled) {
                    RenderSystem::getInstance().applyFXAA(
                        eyeFbo[eye],
                        eyeFbo[eye],
                        static_cast<int>(xr.getViewWidth(eye)),
                        static_cast<int>(xr.getViewHeight(eye))
                    );
                }
                xr.releaseView(eye);
            }

            xr.endFrame();
        }

        RenderSystem::getInstance().render(game->getSceneRoot());

        if (game->getCamera() != nullptr) {
            renderParticleSystems(
                game->getSceneRoot(),
                game->getCamera()->getViewMatrix(),
                game->getCamera()->getProjectionMatrix()
            );
        }

        if (RenderSystem::getInstance().fxaaEnabled) {
            int framebufferWidth = 0;
            int framebufferHeight = 0;

            glfwGetFramebufferSize(
                m_window->getHandle(),
                &framebufferWidth,
                &framebufferHeight
            );

            if (framebufferWidth > 0 && framebufferHeight > 0) {
                RenderSystem::getInstance().applyFXAA(
                    0,
                    0,
                    framebufferWidth,
                    framebufferHeight
                );
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        SceneEditor::getInstance().render(
            *m_window,
            xr,
            game->getSceneRoot(),
            game->getCamera(),
            game->getAnimator(),
            dt
        );

        m_window->swapBuffers();
    }

    glDeleteFramebuffers(2, eyeFbo);
    glDeleteRenderbuffers(2, eyeDepth);

    SceneEditor::getInstance().shutdown();
    PhysicsSystem::getInstance().shutdown();
    AudioSystem::getInstance().shutdown();
    net::NetworkManager::getInstance().shutdown();
    xr.shutdown();
}