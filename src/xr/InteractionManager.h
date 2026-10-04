#pragma once

#include <memory>
#include <vector>
#include "HandController.h"
#include "JoystickNode.h"
#include "ButtonNode.h"
#include "scene/Scene.h"
#include "VRInput.h"

class InteractionManager {
public:
    static InteractionManager& getInstance();

    void init(Scene* scene);
    void update(const VRInputFrame& input, const glm::mat4& rigToWorld);

    void addButton(ButtonNode* button);
    void removeButton(ButtonNode* button);

private:
    InteractionManager() = default;
    
    Scene* m_scene = nullptr;
    std::vector<HandController*> m_hands;
    std::vector<JoystickNode*> m_joysticks;
    std::vector<ButtonNode*> m_buttons;
};
