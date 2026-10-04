#include "InteractionManager.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

InteractionManager& InteractionManager::getInstance() {
    static InteractionManager instance;
    return instance;
}

void InteractionManager::init(Scene* scene) {
    m_scene = scene;
    if (!m_scene) return;

    // Create hands
    auto leftHand = std::make_unique<HandController>("LeftHand", true);
    auto rightHand = std::make_unique<HandController>("RightHand", false);
    m_hands.push_back(leftHand.get());
    m_hands.push_back(rightHand.get());
    m_scene->adopt(std::move(leftHand));
    m_scene->adopt(std::move(rightHand));

    // Create joysticks
    auto leftJoy = std::make_unique<JoystickNode>("LeftJoystick", true);
    auto rightJoy = std::make_unique<JoystickNode>("RightJoystick", false);
    leftJoy->setHomePosition(glm::vec3(-0.2f, 0.0f, -0.3f));
    rightJoy->setHomePosition(glm::vec3(0.2f, 0.0f, -0.3f));
    m_joysticks.push_back(leftJoy.get());
    m_joysticks.push_back(rightJoy.get());
    m_scene->adopt(std::move(leftJoy));
    m_scene->adopt(std::move(rightJoy));
}


void InteractionManager::update(const VRInputFrame& input, const glm::mat4& rigToWorld) {
    for (int i = 0; i < 2; ++i) {
        HandController* hand = m_hands[i];
        const HandState& state = input.hands[i];
        
        // Transform pose by rig
        glm::vec3 worldPos = glm::vec3(rigToWorld * glm::vec4(state.position, 1.0f));
        glm::quat worldRot = glm::quat_cast(rigToWorld) * state.orientation;
        
        hand->updateHand(worldPos, worldRot, state.gripPressed, state.triggerPressed, state.gripValue, state.triggerValue);
        
        // Joystick interaction
        JoystickNode* joy = m_joysticks[i];
        float dist = glm::distance(worldPos, joy->getPosition());
        
        if (state.gripPressed && dist < joy->getGrabRadius()) {
            hand->attachJoystick(joy);
            joy->setHeld(true);
        } else if (!state.gripPressed && hand->getHeldJoystick() == joy) {
            hand->detachJoystick();
            joy->setHeld(false);
            joy->setPosition(joy->getHomePosition());
        }
        
        if (joy->isHeld()) {
            joy->followHand(worldPos, worldRot);
        }
        
        // Button interaction
        for (auto* btn : m_buttons) {
            float btnDist = glm::distance(worldPos, btn->getPosition());
            if (state.triggerPressed && btnDist < btn->getInteractionRadius()) {
                btn->press();
            } else {
                btn->release();
            }
        }
    }
}

void InteractionManager::addButton(ButtonNode* button) {
    if (button) {
        m_buttons.push_back(button);
    }
}

void InteractionManager::removeButton(ButtonNode* button) {
    m_buttons.erase(
        std::remove(m_buttons.begin(), m_buttons.end(), button),
        m_buttons.end()
    );
}
