#pragma once
#include "scene/Node.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>

#ifdef KITBASHER_ENABLE_JOLT
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystem.h>
#endif

enum class BodyType { Static, Dynamic, Kinematic };
enum class ColliderType { Box, Convex };

struct PhysicsDebugLine {
    glm::vec3 from;
    glm::vec3 to;
};

class PhysicsBody {
public:
    PhysicsBody(Node* node, BodyType type, const glm::vec3& size);
    ~PhysicsBody();

    void syncFromPhysics();
    void syncToPhysics();
    void beginEditorManipulation();
    void endEditorManipulation();
    void appendDebugLines(std::vector<PhysicsDebugLine>& lines) const;

private:
    friend class PhysicsSystem;
    Node* m_node;
    BodyType m_type;
    glm::vec3 m_size;
#ifdef KITBASHER_ENABLE_JOLT
    JPH::BodyID m_bodyID;
    JPH::ShapeRefC m_shape;
    mutable bool m_debugWarningLogged = false;
    mutable bool m_debugExceptionLogged = false;
#endif
};

class PhysicsSystem {
public:
    static PhysicsSystem& getInstance();

    void init();
    void shutdown();
    PhysicsBody* createRigidBody(Node* node, BodyType type,
                                   const glm::vec3& size,
                                   ColliderType colliderType = ColliderType::Box,
                                   float friction = 0.5f,
                                   float restitution = 0.1f);

    void update(float renderDeltaTime);
    void syncAnimationDrivenNodes();
    void setDebugDrawEnabled(bool enabled) { m_debugDrawEnabled = enabled; }
    bool isDebugDrawEnabled() const { return m_debugDrawEnabled; }
    void beginEditorManipulation(Node* node);
    void endEditorManipulation(Node* node);
    void wakeDynamicBodies();
    std::vector<PhysicsDebugLine> getDebugLines() const;

    void setBodyPosition(Node* node, const glm::vec3& position);
    void addForce(Node* node, const glm::vec3& force);
    void setLinearVelocity(Node* node, const glm::vec3& velocity);
    void setBodyNetworkRemote(Node* node, bool remote);

private:
    friend class PhysicsBody;
    PhysicsSystem() = default;
    ~PhysicsSystem();
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    std::vector<std::unique_ptr<PhysicsBody>> m_bodies;
    double m_accumulator = 0.0;
    bool m_initialized = false;
    bool m_debugDrawEnabled = false;

#ifdef KITBASHER_ENABLE_JOLT
    std::unordered_map<Node*, JPH::BodyID> m_nodeToBody;
    JPH::BodyID getBodyIdFromNode(Node* node);

    JPH::PhysicsSystem m_physicsSystem;
    JPH::TempAllocator* m_tempAllocator = nullptr;
    JPH::JobSystem* m_jobSystem = nullptr;
#endif
};