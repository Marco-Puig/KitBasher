#include "LuaRuntime.h"

#include "scene/Animator.h"
#include "scene/Node.h"
#include "scene/Scene.h"
#include "xr/ButtonNode.h"
#include "xr/JoystickNode.h"
#include "xr/InteractionManager.h"

#include "rendering/GLBLoader.h"
#include "rendering/Light.h"
#include "rendering/RenderSystem.h"

#include "scene/MeshNode.h"
#include "scene/DestructibleBuildingNode.h"
#include "scene/PlayerAvatarNode.h"

#include "scene/ParticleSystem.h"
#include "scene/FireParticleSystem.h"
#include "scene/SmokeParticleSystem.h"

#include "audio/Audio.h"
#include "platform/PhysicsSystem.h"
#include "xr/VRPlayerRig.h"

#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>

#include <glm/glm.hpp>

#include <iostream>
#include <memory>

#if defined(__has_include)
#  if __has_include("net/NetworkManager.h")
#    include "net/NetworkManager.h"
#    define KITBASHER_HAS_NETWORK_MANAGER 1
#  endif

#  if __has_include("net/ReplicationManager.h")
#    include "net/ReplicationManager.h"
#    define KITBASHER_HAS_REPLICATION_MANAGER 1
#  endif

#  if __has_include("scene/PlayerAvatarNode.h")
#    include "scene/PlayerAvatarNode.h"
#    define KITBASHER_HAS_PLAYER_AVATAR 1
#  endif
#endif

void luaAddInteractionButton(ButtonNode* btn) {
    InteractionManager::getInstance().addButton(btn);
}

void luaRemoveInteractionButton(ButtonNode* btn) {
    InteractionManager::getInstance().removeButton(btn);
}

namespace {

Node* createPlayerAvatar(
    Scene* scene,
    const std::string& name,
    float x, float y, float z,
    float r, float g, float b
) {
    if (scene == nullptr) {
        return nullptr;
    }

    auto avatar = std::make_unique<PlayerAvatarNode>(
        name,
        glm::vec3(r, g, b)
    );

    avatar->setPosition(glm::vec3(x, y, z));

    Node* result = avatar.get();
    scene->adopt(std::unique_ptr<MeshNode>(avatar.release()));

    return result;
}

void setPosition(Node* node, float x, float y, float z) {
    if (node != nullptr) {
        node->setPosition(glm::vec3(x, y, z));
        PhysicsSystem::getInstance().setBodyPosition(node, glm::vec3(x, y, z));
    }
}

void addForce(Node* node, float x, float y, float z) {
    if (node != nullptr) {
        PhysicsSystem::getInstance().addForce(node, glm::vec3(x, y, z));
    }
}

void setLinearVelocity(Node* node, float x, float y, float z) {
    if (node != nullptr) {
        PhysicsSystem::getInstance().setLinearVelocity(node, glm::vec3(x, y, z));
    }
}

void setScale(Node* node, float x, float y, float z) {
    if (node != nullptr) {
        node->setScale(glm::vec3(x, y, z));
    }
}

float getPositionX(Node* node) {
    return node != nullptr ? node->getPosition().x : 0.0f;
}

float getPositionY(Node* node) {
    return node != nullptr ? node->getPosition().y : 0.0f;
}

float getPositionZ(Node* node) {
    return node != nullptr ? node->getPosition().z : 0.0f;
}

void setLightPosition(DirectionalLight* light, float x, float y, float z) {
    if (light != nullptr) {
        light->setPosition(glm::vec3(x, y, z));
    }
}

void setLightColorBase(Light* light, float r, float g, float b) {
    if (light != nullptr) {
        light->setColor(glm::vec3(r, g, b));
    }
}

void setPointLightPosition(PointLight* light, float x, float y, float z) {
    if (light != nullptr) {
        light->setPosition(glm::vec3(x, y, z));
    }
}

void setDirLightColor(DirectionalLight* light, float r, float g, float b) {
    setLightColorBase(light, r, g, b);
}

void setPointLightColor(PointLight* light, float r, float g, float b) {
    setLightColorBase(light, r, g, b);
}

void setDirIntensity(DirectionalLight* light, float intensity) {
    if (light != nullptr) {
        light->setIntensity(intensity);
    }
}

void setDirExposure(DirectionalLight* light, float exposure) {
    if (light != nullptr) {
        light->setExposure(exposure);
    }
}

void setPointIntensity(PointLight* light, float intensity) {
    if (light != nullptr) {
        light->setIntensity(intensity);
    }
}

void setPointExposure(PointLight* light, float exposure) {
    if (light != nullptr) {
        light->setExposure(exposure);
    }
}

void setPointRadius(PointLight* light, float radius) {
    if (light != nullptr) {
        light->setRadius(radius);
    }
}

Node* loadMesh(
    Scene* scene,
    const std::string& path,
    const std::string& name
) {
    if (scene == nullptr) {
        return nullptr;
    }

    MeshNode* root = GLBLoader::loadGLB(path);

    if (root == nullptr) {
        return nullptr;
    }

    root->setName(name);

    return scene->adopt(std::unique_ptr<MeshNode>(root));
}

DirectionalLight* createDirectionalLight(
    Scene* scene,
    const std::string& name
) {
    if (scene == nullptr) {
        return nullptr;
    }

    auto light = std::make_unique<DirectionalLight>(
        name,
        glm::vec3(-0.2f, -1.0f, -0.3f)
    );

    DirectionalLight* result = light.get();

    scene->adopt(std::move(light));
    RenderSystem::getInstance().addLight(result);

    return result;
}

PointLight* createPointLight(
    Scene* scene,
    const std::string& name,
    float x,
    float y,
    float z
) {
    if (scene == nullptr) {
        return nullptr;
    }

    auto light = std::make_unique<PointLight>(
        name,
        glm::vec3(x, y, z)
    );

    PointLight* result = light.get();

    scene->adopt(std::move(light));
    RenderSystem::getInstance().addLight(result);

    return result;
}

bool addRigidBody(
    Node* node,
    const std::string& type,
    const std::string& collider,
    float friction,
    float restitution
) {
    if (node == nullptr) {
        return false;
    }

    auto* mesh = dynamic_cast<MeshNode*>(node);

    if (mesh == nullptr) {
        return false;
    }

    BodyType bodyType = BodyType::Static;

    if (type == "dynamic") {
        bodyType = BodyType::Dynamic;
    } else if (type == "kinematic") {
        bodyType = BodyType::Kinematic;
    }

    ColliderType colliderType = ColliderType::Box;

    if (collider == "convex") {
        colliderType = ColliderType::Convex;
    }

    return PhysicsSystem::getInstance().createRigidBody(
        node,
        bodyType,
        glm::max(mesh->getBoundsSize(), glm::vec3(0.01f)),
        colliderType,
        friction,
        restitution
    ) != nullptr;
}

void setSkybox(const std::string& path) {
    RenderSystem::getInstance().setSkyboxPath(path);
}

void setVRPlayerPosition(float x, float y, float z) {
    VRPlayerRig::getInstance().setPosition(x, y, z);
}

float getVRPlayerPositionX() {
    return VRPlayerRig::getInstance().getPositionX();
}

float getVRPlayerPositionY() {
    return VRPlayerRig::getInstance().getPositionY();
}

float getVRPlayerPositionZ() {
    return VRPlayerRig::getInstance().getPositionZ();
}

void setVRPlayerYaw(float yaw) {
    VRPlayerRig::getInstance().setYaw(yaw);
}

float getVRPlayerYaw() {
    return VRPlayerRig::getInstance().getYaw();
}

void setVRMoveSpeed(float speed) {
    VRPlayerRig::getInstance().setMoveSpeed(speed);
}

void setVRSnapTurn(bool enabled) {
    VRPlayerRig::getInstance().setSnapTurn(enabled);
}

Node* createDestructibleBuilding(
    Scene* scene,
    const std::string& name,
    float x,
    float y,
    float z,
    float height,
    float health
) {
    if (scene == nullptr) {
        return nullptr;
    }

    const glm::vec3 size(
        1.0f,
        glm::max(0.1f, height),
        1.0f
    );

    auto building = std::make_unique<DestructibleBuildingNode>(
        name,
        scene,
        size,
        health
    );

    building->setPosition(glm::vec3(x, y, z));

    DestructibleBuildingNode* result = building.get();

    std::unique_ptr<MeshNode> asMesh(building.release());
    scene->adopt(std::move(asMesh));

    return result;
}

void nodeApplyDamage(Node* node, float amount) {
    if (node == nullptr) {
        return;
    }

    if (auto* building = dynamic_cast<DestructibleBuildingNode*>(node)) {
        building->applyDamage(amount);
    }
}

void nodeDestroy(Node* node) {
    if (node == nullptr) {
        return;
    }

    if (auto* building = dynamic_cast<DestructibleBuildingNode*>(node)) {
        building->destroy();
    }
}

float nodeGetHealth(Node* node) {
    if (node == nullptr) {
        return 0.0f;
    }

    if (auto* building = dynamic_cast<DestructibleBuildingNode*>(node)) {
        return building->getHealth();
    }

    return 0.0f;
}

float nodeGetMaxHealth(Node* node) {
    if (node == nullptr) {
        return 0.0f;
    }

    if (auto* building = dynamic_cast<DestructibleBuildingNode*>(node)) {
        return building->getMaxHealth();
    }

    return 0.0f;
}

bool nodeIsDestroyed(Node* node) {
    if (node == nullptr) {
        return false;
    }

    if (auto* building = dynamic_cast<DestructibleBuildingNode*>(node)) {
        return building->isDestroyed();
    }

    return false;
}

void audioPlaySound(const std::string& path) {
    AudioSystem::getInstance().playSound(path, false, 1.0f);
}

void audioPlaySoundLoop(const std::string& path) {
    AudioSystem::getInstance().playSound(path, true, 1.0f);
}

void audioPlayMusic(const std::string& path) {
    AudioSystem::getInstance().playMusic(path, true, 0.8f);
}

void audioStopMusic() {
    AudioSystem::getInstance().stopMusic();
}

void audioStopAllSounds() {
    AudioSystem::getInstance().stopAllSounds();
}

Node* createFire(
    Scene* scene,
    const std::string& name,
    float x,
    float y,
    float z
) {
    if (scene == nullptr) {
        return nullptr;
    }

    auto fire = std::make_unique<FireParticleSystem>(name);
    fire->setPosition(glm::vec3(x, y, z));

    Node* result = fire.get();

    scene->adopt(std::move(fire));

    return result;
}

Node* createSmoke(
    Scene* scene,
    const std::string& name,
    float x,
    float y,
    float z
) {
    if (scene == nullptr) {
        return nullptr;
    }

    auto smoke = std::make_unique<SmokeParticleSystem>(name);
    smoke->setPosition(glm::vec3(x, y, z));

    Node* result = smoke.get();

    scene->adopt(std::move(smoke));

    return result;
}

void nodePlayParticles(Node* node) {
    if (node == nullptr) {
        return;
    }

    if (auto* particleSystem = dynamic_cast<ParticleSystem*>(node)) {
        particleSystem->play();
    }
}

void nodeStopParticles(Node* node) {
    if (node == nullptr) {
        return;
    }

    if (auto* particleSystem = dynamic_cast<ParticleSystem*>(node)) {
        particleSystem->stop();
    }
}

void nodeClearParticles(Node* node) {
    if (node == nullptr) {
        return;
    }

    if (auto* particleSystem = dynamic_cast<ParticleSystem*>(node)) {
        particleSystem->clear();
    }
}

void nodeSetParticleEmissionRate(Node* node, float rate) {
    if (node == nullptr) {
        return;
    }

    if (auto* particleSystem = dynamic_cast<ParticleSystem*>(node)) {
        particleSystem->setEmissionRate(rate);
    }
}

bool nodeIsParticleSystemPlaying(Node* node) {
    if (node == nullptr) {
        return false;
    }

    if (auto* particleSystem = dynamic_cast<ParticleSystem*>(node)) {
        return particleSystem->isPlaying();
    }

    return false;
}

#ifdef KITBASHER_HAS_NETWORK_MANAGER

bool netIsHost() {
    return net::NetworkManager::getInstance().isHost();
}

bool netIsConnected() {
    return net::NetworkManager::getInstance().isConnected();
}

int netGetLocalPlayerSlot() {
    return static_cast<int>(
        net::NetworkManager::getInstance().getLocalPlayerSlot()
    );
}

int netGetPlayerCount() {
    return static_cast<int>(
        net::NetworkManager::getInstance().getPlayerCount()
    );
}

void netShowInviteDialog() {
    net::NetworkManager::getInstance().showInviteDialog();
}

#endif

#ifdef KITBASHER_HAS_REPLICATION_MANAGER

void netReplicateNode(Node* node) {
    if (node != nullptr) {
        net::ReplicationManager::getInstance().spawnNodeOnClients(node);
    }
}

void netDespawnNode(Node* node) {
    if (node != nullptr) {
        net::ReplicationManager::getInstance().despawnNodeOnClients(node);
    }
}

#endif

}

#if defined(KITBASHER_HAS_NETWORK_MANAGER) && defined(KITBASHER_HAS_REPLICATION_MANAGER)

void netSendAvatarTransform(float x, float y, float z) {
    auto& network = net::NetworkManager::getInstance();

    if (!network.isConnected()) {
        return;
    }

    if (network.isHost()) {
        return;
    }

    net::ReplicationManager::getInstance().sendAvatarTransform(
        network.getLocalPlayerSlot(),
        glm::vec3(x, y, z)
    );
}

#endif

LuaRuntime::LuaRuntime() = default;

LuaRuntime::~LuaRuntime() {
    shutdown();
}

bool LuaRuntime::initialize() {
    if (m_state != nullptr) {
        return true;
    }

    m_state = luaL_newstate();

    if (m_state == nullptr) {
        std::cerr << "[Lua] Failed to create Lua state\n";
        return false;
    }

    luaL_openlibs(m_state);

    bindEngineApi();

    return true;
}

void LuaRuntime::shutdown() {
    if (m_state != nullptr) {
        m_gameScript.reset();
        lua_close(m_state);
        m_state = nullptr;
    }

    m_scene = nullptr;
    m_animator = nullptr;
    m_started = false;
}

void LuaRuntime::bindEngineApi() {
    luabridge::getGlobalNamespace(m_state)
        .beginClass<Animator>("Animator")
            .addFunction("animateAxis", &Animator::animateAxis)
            .addFunction("procedural", &Animator::proceduralFromLua)
            .addFunction("playProcedural", &Animator::playProcedural)
            .addFunction("stopProcedural", &Animator::stopProcedural)
            .addFunction("playAnimation", &Animator::playAnimation)
            .addFunction("stopAnimation", &Animator::stopAnimation)
        .endClass()

    .beginClass<Scene>("Scene")
        .addFunction("findNode", &Scene::findByName)
        .addFunction("loadMesh", &loadMesh)
        .addFunction("createDirectionalLight", &createDirectionalLight)
        .addFunction("createPointLight", &createPointLight)
        .addFunction("createDestructibleBuilding", &createDestructibleBuilding)
        .addFunction("createFire", &createFire)
        .addFunction("createSmoke", &createSmoke)
        .addFunction("createPlayerAvatar", &createPlayerAvatar)
    .endClass();

    luabridge::getGlobalNamespace(m_state)
        .beginClass<Node>("Node")
            .addFunction("getName", &Node::getName)
            .addFunction("setName", &Node::setName)
            .addFunction("setPosition", &setPosition)
            .addFunction("getPositionX", &getPositionX)
            .addFunction("getPositionY", &getPositionY)
            .addFunction("getPositionZ", &getPositionZ)
            .addFunction("setScale", &setScale)
            .addFunction("addForce", &addForce)
            .addFunction("setLinearVelocity", &setLinearVelocity)
            .addFunction("applyDamage", &nodeApplyDamage)
            .addFunction("destroy", &nodeDestroy)
            .addFunction("getHealth", &nodeGetHealth)
            .addFunction("getMaxHealth", &nodeGetMaxHealth)
            .addFunction("isDestroyed", &nodeIsDestroyed)
            .addFunction("playParticles", &nodePlayParticles)
            .addFunction("stopParticles", &nodeStopParticles)
            .addFunction("clearParticles", &nodeClearParticles)
            .addFunction("setParticleEmissionRate", &nodeSetParticleEmissionRate)
            .addFunction("isParticleSystemPlaying", &nodeIsParticleSystemPlaying)
        .endClass()

        .beginClass<Light>("Light")
            .addFunction("setColor", &setLightColorBase)
        .endClass()

        .beginClass<DirectionalLight>("DirectionalLight")
            .addFunction("setPosition", &setLightPosition)
            .addFunction("setColor", &setDirLightColor)
            .addFunction("setIntensity", &setDirIntensity)
            .addFunction("setExposure", &setDirExposure)
        .endClass()

        .beginClass<PointLight>("PointLight")
            .addFunction("setPosition", &setPointLightPosition)
            .addFunction("setColor", &setPointLightColor)
            .addFunction("setIntensity", &setPointIntensity)
            .addFunction("setExposure", &setPointExposure)
            .addFunction("setRadius", &setPointRadius)
        .endClass();

    luabridge::getGlobalNamespace(m_state)
        .beginNamespace("Engine")
            .addFunction("setSkybox", &setSkybox)

            .addFunction("setVRPlayerPosition", &setVRPlayerPosition)
            .addFunction("getVRPlayerPositionX", &getVRPlayerPositionX)
            .addFunction("getVRPlayerPositionY", &getVRPlayerPositionY)
            .addFunction("getVRPlayerPositionZ", &getVRPlayerPositionZ)

            .addFunction("setVRPlayerYaw", &setVRPlayerYaw)
            .addFunction("getVRPlayerYaw", &getVRPlayerYaw)

            .addFunction("setVRMoveSpeed", &setVRMoveSpeed)
            .addFunction("setVRSnapTurn", &setVRSnapTurn)
        .endNamespace();

    luabridge::getGlobalNamespace(m_state)
        .beginNamespace("Audio")
            .addFunction("playSound", &audioPlaySound)
            .addFunction("playSoundLoop", &audioPlaySoundLoop)
            .addFunction("playMusic", &audioPlayMusic)
            .addFunction("stopMusic", &audioStopMusic)
            .addFunction("stopAllSounds", &audioStopAllSounds)
        .endNamespace();

    luabridge::getGlobalNamespace(m_state)
        .beginNamespace("Physics")
            .addFunction("addRigidBody", &addRigidBody)
        .endNamespace();

    luabridge::getGlobalNamespace(m_state)
        .beginNamespace("Interaction")
            .addFunction("addButton", &luaAddInteractionButton)
            .addFunction("removeButton", &luaRemoveInteractionButton)
        .endNamespace();


    luabridge::getGlobalNamespace(m_state)
        .beginClass<ButtonNode>("ButtonNode")
            .addFunction("isPressed", &ButtonNode::isPressed)
            .addFunction("press", &ButtonNode::press)
            .addFunction("release", &ButtonNode::release)
        .endClass()
        .beginClass<JoystickNode>("JoystickNode")
            .addFunction("isHeld", &JoystickNode::isHeld)
            .addFunction("setHomePosition", &JoystickNode::setHomePosition)
        .endClass();

#if defined(KITBASHER_HAS_NETWORK_MANAGER) || defined(KITBASHER_HAS_REPLICATION_MANAGER)
    luabridge::getGlobalNamespace(m_state)
        .beginNamespace("Net")

#ifdef KITBASHER_HAS_NETWORK_MANAGER
            .addFunction("isHost", &netIsHost)
            .addFunction("isConnected", &netIsConnected)
            .addFunction("getLocalPlayerSlot", &netGetLocalPlayerSlot)
            .addFunction("getPlayerCount", &netGetPlayerCount)
            .addFunction("showInviteDialog", &netShowInviteDialog) 
#endif

#ifdef KITBASHER_HAS_REPLICATION_MANAGER
            .addFunction("replicateNode", &netReplicateNode)
            .addFunction("despawnNode", &netDespawnNode)
            .addFunction("sendAvatarTransform", &netSendAvatarTransform)
#endif

        .endNamespace();
#endif
}

bool LuaRuntime::loadGameScript(const std::string& path) {
    if (m_state == nullptr) {
        std::cerr << "[Lua] Cannot load '" << path
                  << "': runtime is not initialized\n";
        return false;
    }

    if (luaL_loadfile(m_state, path.c_str()) != LUA_OK) {
        std::cerr << "[Lua] Load error in '" << path << "': "
                  << lua_tostring(m_state, -1) << '\n';
        lua_pop(m_state, 1);
        return false;
    }

    if (lua_pcall(m_state, 0, 1, 0) != LUA_OK) {
        std::cerr << "[Lua] Startup error in '" << path << "': "
                  << lua_tostring(m_state, -1) << '\n';
        lua_pop(m_state, 1);
        return false;
    }

    if (!lua_istable(m_state, -1)) {
        std::cerr << "[Lua] Script '" << path
                  << "' must return a table\n";
        lua_pop(m_state, 1);
        return false;
    }

    m_gameScript = std::make_unique<luabridge::LuaRef>(
        luabridge::LuaRef::fromStack(m_state, -1)
    );

    lua_pop(m_state, 1);

    m_scriptPath = path;
    m_started = false;

    return true;
}

void LuaRuntime::startGame(Scene& scene, Animator& animator) {
    if (m_started || !m_gameScript) {
        return;
    }

    m_scene = &scene;
    m_animator = &animator;

    try {
        luabridge::LuaRef callback = (*m_gameScript)["onStart"];

        if (callback.isFunction()) {
            callback(&scene, &animator);
        }

        m_started = true;
    } catch (const std::exception& error) {
        std::cerr << "[Lua] onStart error in '" << m_scriptPath << "': "
                  << error.what() << '\n';
    }
}

void LuaRuntime::updateGame(float deltaTime) {
    if (!m_started || !m_gameScript || m_scene == nullptr ||
        m_animator == nullptr) {
        return;
    }

    try {
        luabridge::LuaRef callback = (*m_gameScript)["onUpdate"];

        if (callback.isFunction()) {
            callback(deltaTime, m_scene, m_animator);
        }
    } catch (const std::exception& error) {
        std::cerr << "[Lua] onUpdate error in '" << m_scriptPath << "': "
                  << error.what() << '\n';
    }
}