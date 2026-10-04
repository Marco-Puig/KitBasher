#include "Game.h"
#include "xr/InteractionManager.h"

#include "net/NetworkManager.h"
#include "net/ReplicationManager.h"

#include <stdexcept>

Game::Game()
    : scene(std::make_unique<Scene>()),
      animator(std::make_unique<Animator>()),
      luaRuntime(std::make_unique<LuaRuntime>()),
      camera(std::make_unique<ArcRotateCamera>("DesktopCamera")) {}

void Game::start() {
    if (!luaRuntime->initialize()) {
        throw std::runtime_error("Failed to initialize Lua runtime");
    }

    // Initialize multiplayer replication against this game's scene.
    //
    // This must happen before Lua creates the scene if possible,
    // but definitely before replication updates begin.
    net::ReplicationManager::getInstance().initialize(scene.get());

    if (!luaRuntime->loadGameScript("Game/game.lua")) {
        throw std::runtime_error("Failed to load game script");
    }

    luaRuntime->startGame(*scene, *animator);
    InteractionManager::getInstance().init(scene.get());
}

void Game::update(float deltaTime) {
    // Pump network messages.
    net::NetworkManager::getInstance().update(deltaTime);

    // Apply remote transforms on clients.
    // Send host transforms on host.
    net::ReplicationManager::getInstance().update(deltaTime);

    // Normal game update.
    luaRuntime->updateGame(deltaTime);
    animator->update(deltaTime);
    camera->update(deltaTime);
}

