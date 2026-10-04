//[cite: 12]
#include "PhysicsSystem.h"
#include "scene/MeshNode.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef KITBASHER_ENABLE_JOLT
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace {
bool joltAssertFailed(const char* expression, const char* message,
                      const char* file, JPH::uint line) {
    std::cerr << "[Jolt] assertion failed: " << (expression ? expression : "")
              << " at " << (file ? file : "<unknown>") << ':' << line;
    if (message != nullptr)
        std::cerr << " - " << message;
    std::cerr << '\n';
    std::cerr.flush();
#ifdef _WIN32
    std::string diagnostic = "[Jolt] assertion failed: ";
    diagnostic += expression != nullptr ? expression : "<unknown>";
    diagnostic += " at ";
    diagnostic += file != nullptr ? file : "<unknown>";
    diagnostic += ':' + std::to_string(line) + '\n';
    OutputDebugStringA(diagnostic.c_str());
#endif
    return false;
}

void joltTrace(const char* format, ...) {
    char buffer[2048] = {};
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    va_end(args);
    std::cerr << "[Jolt] " << buffer << '\n';
    std::cerr.flush();
#ifdef _WIN32
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
#endif
}

constexpr JPH::ObjectLayer cNonMoving = 0;
constexpr JPH::ObjectLayer cMoving = 1;
constexpr JPH::BroadPhaseLayer cBroadPhaseNonMoving(0);
constexpr JPH::BroadPhaseLayer cBroadPhaseMoving(1);

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == cMoving ? cBroadPhaseMoving : cBroadPhaseNonMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == cBroadPhaseMoving ? "Moving" : "NonMoving";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        return layer == cMoving || broadPhase == cBroadPhaseMoving;
    }
};

class ObjectLayerPair final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        return first == cMoving || second == cMoving;
    }
};

BroadPhaseLayers gBroadPhaseLayers;
ObjectVsBroadPhase gObjectVsBroadPhase;
ObjectLayerPair gObjectLayerPair;
}
#endif

PhysicsBody::PhysicsBody(Node* node, BodyType type, const glm::vec3& size)
    : m_node(node), m_type(type), m_size(size)
#ifdef KITBASHER_ENABLE_JOLT
    , m_bodyID(JPH::BodyID())
#endif
{
}

PhysicsBody::~PhysicsBody() = default;

void PhysicsBody::syncFromPhysics() {
#ifdef KITBASHER_ENABLE_JOLT
    if (m_node == nullptr || m_bodyID.IsInvalid())
        return;
    JPH::RVec3 position;
    JPH::Quat rotation;
    PhysicsSystem::getInstance().m_physicsSystem.GetBodyInterface().GetPositionAndRotation(
        m_bodyID, position, rotation);
    m_node->setPosition(glm::vec3(static_cast<float>(position.GetX()),
                                  static_cast<float>(position.GetY()),
                                  static_cast<float>(position.GetZ())));
    if (!m_node->isAnimationDriven()) {
        m_node->setRotation(glm::quat(rotation.GetW(), rotation.GetX(),
                                      rotation.GetY(), rotation.GetZ()));
    }
#endif
}

void PhysicsBody::syncToPhysics() {
#ifdef KITBASHER_ENABLE_JOLT
    if (m_bodyID.IsInvalid())
        return;
    auto& bodyInterface = PhysicsSystem::getInstance().m_physicsSystem.GetBodyInterface();
    if (m_type == BodyType::Dynamic) {
        bodyInterface.SetLinearVelocity(m_bodyID, JPH::Vec3::sZero());
        bodyInterface.SetAngularVelocity(m_bodyID, JPH::Vec3::sZero());
    }

    bodyInterface.SetPositionAndRotation(
        m_bodyID,
        JPH::RVec3(m_node->getPosition().x, m_node->getPosition().y, m_node->getPosition().z),
        JPH::Quat(m_node->getRotation().x, m_node->getRotation().y,
                  m_node->getRotation().z, m_node->getRotation().w),
        JPH::EActivation::DontActivate);
    if (m_type != BodyType::Dynamic)
        PhysicsSystem::getInstance().wakeDynamicBodies();
#endif
}

void PhysicsBody::beginEditorManipulation() {
#ifdef KITBASHER_ENABLE_JOLT
    if (m_bodyID.IsInvalid())
        return;
    auto& bodyInterface = PhysicsSystem::getInstance().m_physicsSystem.GetBodyInterface();
    if (m_type == BodyType::Dynamic) {
        bodyInterface.SetLinearVelocity(m_bodyID, JPH::Vec3::sZero());
        bodyInterface.SetAngularVelocity(m_bodyID, JPH::Vec3::sZero());
        bodyInterface.SetMotionType(m_bodyID, JPH::EMotionType::Kinematic,
                                    JPH::EActivation::DontActivate);
    } else {
        // Jolt does not wake sleeping dynamic bodies when static geometry moves.
        PhysicsSystem::getInstance().wakeDynamicBodies();
    }
#endif
}

void PhysicsBody::endEditorManipulation() {
#ifdef KITBASHER_ENABLE_JOLT
    if (m_bodyID.IsInvalid() || m_type != BodyType::Dynamic)
        return;
    auto& bodyInterface = PhysicsSystem::getInstance().m_physicsSystem.GetBodyInterface();
    bodyInterface.SetMotionType(m_bodyID, JPH::EMotionType::Dynamic,
                                JPH::EActivation::Activate);
    bodyInterface.SetLinearVelocity(m_bodyID, JPH::Vec3::sZero());
    bodyInterface.SetAngularVelocity(m_bodyID, JPH::Vec3::sZero());
#endif
}

void PhysicsBody::appendDebugLines(std::vector<PhysicsDebugLine>& lines) const {
#ifdef KITBASHER_ENABLE_JOLT
    const char* debugStage = "validation";
#if defined(_CPPUNWIND)
    try {
#endif
    if (m_node == nullptr || m_bodyID.IsInvalid() || m_shape == nullptr)
        return;
    debugStage = "body registration";
    const auto& bodyInterface =
        PhysicsSystem::getInstance().m_physicsSystem.GetBodyInterface();
    if (!bodyInterface.IsAdded(m_bodyID)) {
        if (!m_debugWarningLogged) {
            std::cerr << "[Physics] Skipping collision debug for a body that is "
                         "no longer registered\n";
            m_debugWarningLogged = true;
        }
        return;
    }
    debugStage = "body transform";
    JPH::RVec3 position;
    JPH::Quat rotation;
    bodyInterface.GetPositionAndRotation(m_bodyID, position, rotation);
    const glm::quat q(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());
    const glm::vec3 center(static_cast<float>(position.GetX()),
                           static_cast<float>(position.GetY()),
                           static_cast<float>(position.GetZ()));
    debugStage = "shape subtype";
    const JPH::EShapeSubType subType = m_shape->GetSubType();

    if (subType == JPH::EShapeSubType::ConvexHull) {
        debugStage = "convex hull cast";
        // The subtype check above is Jolt's runtime type check. Avoid C++ RTTI
        // here: Jolt may be built with different RTTI settings than the engine.
        const auto* hull = static_cast<const JPH::ConvexHullShape*>(m_shape.GetPtr());
        if (hull != nullptr) {
            debugStage = "convex hull faces";
            for (uint32_t faceIndex = 0; faceIndex < hull->GetNumFaces(); ++faceIndex) {
                debugStage = "face vertex count";
                const uint32_t count = hull->GetNumVerticesInFace(faceIndex);
                if (count < 2)
                    continue;
                std::vector<uint32_t> face(count);
                debugStage = "face vertex indices";
                const uint32_t written = hull->GetFaceVertices(
                    faceIndex, count, face.data());
                if (written != count)
                    continue;
                for (uint32_t i = 0; i < count; ++i) {
                    if (face[i] >= hull->GetNumPoints() ||
                        face[(i + 1) % count] >= hull->GetNumPoints()) {
                        if (!m_debugWarningLogged) {
                            std::cerr << "[Physics] Skipping collision debug face "
                                         "with an invalid vertex index\n";
                            m_debugWarningLogged = true;
                        }
                        break;
                    }
                    debugStage = "hull point access";
                    const JPH::Vec3 a = hull->GetPoint(face[i]);
                    const JPH::Vec3 b = hull->GetPoint(face[(i + 1) % count]);
                    const glm::vec3 from = center + q * glm::vec3(a.GetX(), a.GetY(), a.GetZ());
                    const glm::vec3 to = center + q * glm::vec3(b.GetX(), b.GetY(), b.GetZ());
                    if (!std::isfinite(from.x) || !std::isfinite(from.y) ||
                        !std::isfinite(from.z) || !std::isfinite(to.x) ||
                        !std::isfinite(to.y) || !std::isfinite(to.z)) {
                        if (!m_debugWarningLogged) {
                            std::cerr << "[Physics] Skipping collision debug face with "
                                         "non-finite vertex data\n";
                            m_debugWarningLogged = true;
                        }
                        break;
                    }
                    lines.push_back({from, to});
                }
            }
        } else if (!m_debugWarningLogged) {
            std::cerr << "[Physics] Collision debug shape subtype is ConvexHull but "
                         "the concrete hull cast failed for node '"
                      << m_node->getName() << "'\n";
            m_debugWarningLogged = true;
        }
    } else if (subType == JPH::EShapeSubType::Box) {
        // Box-shaped bodies (explicit ColliderType::Box, too few points to hull,
        // or genuinely degenerate geometry) previously had no debug visual at
        // all, which made a body silently invisible to "Show Collision Debug"
        // rather than showing something. Draw its 12 edges instead.
        debugStage = "box shape cast";
        const auto* box = static_cast<const JPH::BoxShape*>(m_shape.GetPtr());
        if (box != nullptr) {
            debugStage = "box half extent";
            const JPH::Vec3 he = box->GetHalfExtent();
            const glm::vec3 h(he.GetX(), he.GetY(), he.GetZ());
            const glm::vec3 corners[8] = {
                {-h.x, -h.y, -h.z}, { h.x, -h.y, -h.z}, { h.x,  h.y, -h.z}, {-h.x,  h.y, -h.z},
                {-h.x, -h.y,  h.z}, { h.x, -h.y,  h.z}, { h.x,  h.y,  h.z}, {-h.x,  h.y,  h.z},
            };
            debugStage = "box world corners";
            glm::vec3 world[8];
            bool valid = true;
            for (int i = 0; i < 8 && valid; ++i) {
                world[i] = center + q * corners[i];
                if (!std::isfinite(world[i].x) || !std::isfinite(world[i].y) ||
                    !std::isfinite(world[i].z)) {
                    if (!m_debugWarningLogged) {
                        std::cerr << "[Physics] Skipping box collision debug with "
                                     "non-finite vertex data for node '"
                                  << m_node->getName() << "'\n";
                        m_debugWarningLogged = true;
                    }
                    valid = false;
                }
            }
            if (valid) {
                static constexpr int edges[12][2] = {
                    {0, 1}, {1, 2}, {2, 3}, {3, 0}, // bottom face
                    {4, 5}, {5, 6}, {6, 7}, {7, 4}, // top face
                    {0, 4}, {1, 5}, {2, 6}, {3, 7}, // verticals
                };
                for (const auto& edge : edges)
                    lines.push_back({world[edge[0]], world[edge[1]]});
            }
        } else if (!m_debugWarningLogged) {
            std::cerr << "[Physics] Collision debug shape subtype is Box but "
                         "the concrete box cast failed for node '"
                      << m_node->getName() << "'\n";
            m_debugWarningLogged = true;
        }
    } else if (!m_debugWarningLogged) {
        std::cerr << "[Physics] Skipping collision debug for unsupported "
                     "shape type\n";
        m_debugWarningLogged = true;
    }
#if defined(_CPPUNWIND)
    } catch (const std::exception&) {
        throw;
    } catch (const std::string& error) {
        if (!m_debugExceptionLogged) {
            std::cerr << "[Physics] Collision debug string exception for node '"
                      << (m_node != nullptr ? m_node->getName() : "<null>")
                      << "' during " << debugStage << ": " << error << '\n';
            m_debugExceptionLogged = true;
        }
        throw;
    } catch (const char* error) {
        if (!m_debugExceptionLogged) {
            std::cerr << "[Physics] Collision debug C-string exception for node '"
                      << (m_node != nullptr ? m_node->getName() : "<null>")
                      << "' during " << debugStage << ": "
                      << (error != nullptr ? error : "<null>") << '\n';
            m_debugExceptionLogged = true;
        }
        throw;
    } catch (...) {
        if (!m_debugExceptionLogged) {
            std::cerr << "[Physics] Collision debug unknown exception for node '"
                      << (m_node != nullptr ? m_node->getName() : "<null>")
                      << "' during " << debugStage << '\n';
            m_debugExceptionLogged = true;
        }
        throw;
    }
#endif
#else
    (void)lines;
#endif
}

PhysicsSystem& PhysicsSystem::getInstance() {
    static PhysicsSystem instance;
    return instance;
}

void PhysicsSystem::beginEditorManipulation(Node* node) {
    for (auto& body : m_bodies)
        if (body != nullptr && body->m_node == node)
            body->beginEditorManipulation();
}

void PhysicsSystem::endEditorManipulation(Node* node) {
    for (auto& body : m_bodies)
        if (body != nullptr && body->m_node == node)
            body->endEditorManipulation();
}

void PhysicsSystem::wakeDynamicBodies() {
#ifdef KITBASHER_ENABLE_JOLT
    auto& bodyInterface = m_physicsSystem.GetBodyInterface();
    for (const auto& body : m_bodies) {
        if (body != nullptr && body->m_type == BodyType::Dynamic &&
            !body->m_bodyID.IsInvalid() && bodyInterface.IsAdded(body->m_bodyID)) {
            bodyInterface.ActivateBody(body->m_bodyID);
        }
    }
#endif
}

void PhysicsSystem::syncAnimationDrivenNodes() {
    for (const auto& body : m_bodies) {
        if (body != nullptr && body->m_node != nullptr &&
            body->m_node->isAnimationDriven())
            body->syncToPhysics();
    }
}

PhysicsSystem::~PhysicsSystem() {
    shutdown();
}

void PhysicsSystem::init() {
    if (m_initialized)
        return;
#ifdef KITBASHER_ENABLE_JOLT
    JPH::RegisterDefaultAllocator();
    JPH::Trace = joltTrace;
    JPH::AssertFailed = joltAssertFailed;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    m_tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    m_jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
    m_physicsSystem.Init(10240, 0, 10240, 10240, gBroadPhaseLayers,
                         gObjectVsBroadPhase, gObjectLayerPair);
    m_physicsSystem.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
#endif
    m_initialized = true;
}

void PhysicsSystem::shutdown() {
    if (!m_initialized)
        return;
    m_bodies.clear();
#ifdef KITBASHER_ENABLE_JOLT
    delete m_jobSystem;
    delete m_tempAllocator;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
#endif
    m_accumulator = 0.0;
    m_initialized = false;
}

PhysicsBody* PhysicsSystem::createRigidBody(Node* node, BodyType type,
                                              const glm::vec3& size,
                                              ColliderType colliderType,
                                              float friction, float restitution) {

    if (node == nullptr) {
        std::cerr << "[Physics] Cannot create rigid body for a null node\n";
        return nullptr;
    }
    if (!m_initialized)
        PhysicsSystem::getInstance().init();
    auto body = std::make_unique<PhysicsBody>(node, type, size);
    PhysicsBody* result = body.get();
#ifdef KITBASHER_ENABLE_JOLT
    std::vector<JPH::Vec3> hullPoints;
    std::function<void(Node*)> collectPoints = [&](Node* node) {
        if (auto* mesh = dynamic_cast<const MeshNode*>(node)) {
            constexpr float epsilon = 1.0e-4f;
            for (const glm::vec3& point : mesh->getCollisionVertices()) {
                bool duplicate = false;
                for (const JPH::Vec3& existing : hullPoints) {
                    if ((point.x - existing.GetX()) * (point.x - existing.GetX()) +
                        (point.y - existing.GetY()) * (point.y - existing.GetY()) +
                        (point.z - existing.GetZ()) * (point.z - existing.GetZ()) <
                        epsilon * epsilon) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    hullPoints.emplace_back(point.x, point.y, point.z);
            }
        }
        for (const auto& child : node->getChildren())
            collectPoints(child.get());
    };
    collectPoints(node);

    if (hullPoints.empty()) {
        std::cerr << "[Physics] Warning: Rigid body for node '" << node->getName() 
                  << "' has zero collision vertices after merging children. "
                  << "Falling back to box shape.\n";
    }

    // Guard against truly degenerate point sets - a set that has collapsed onto
    // a line or a single point (2 or more near-zero axes). This is the case
    // that can pass Jolt's HasError() validation but crash later inside its
    // internal face/plane math (a raw access violation, not a catchable C++
    // exception). A single thin axis is NOT degenerate - it's completely
    // normal for a floor, wall, or any flat mesh, and Jolt builds a valid
    // convex hull from flat/planar point sets without issue. Only bail to the
    // box fallback when the geometry is thin on 2+ axes at once.
    bool geometryDegenerate = false;
    if (hullPoints.size() >= 4) {
        glm::vec3 minPoint(std::numeric_limits<float>::max());
        glm::vec3 maxPoint(std::numeric_limits<float>::lowest());
        for (const JPH::Vec3& p : hullPoints) {
            const glm::vec3 v(p.GetX(), p.GetY(), p.GetZ());
            minPoint = glm::min(minPoint, v);
            maxPoint = glm::max(maxPoint, v);
        }
        const glm::vec3 extent = maxPoint - minPoint;
        constexpr float kMinExtent = 0.005f; // 5mm; tune to your world scale
        const int thinAxes = (extent.x < kMinExtent ? 1 : 0) +
                             (extent.y < kMinExtent ? 1 : 0) +
                             (extent.z < kMinExtent ? 1 : 0);
        if (thinAxes >= 2) {
            geometryDegenerate = true;
            std::cerr << "[Physics] Node '" << node->getName()
                      << "' collision geometry is collinear/point-like "
                      << "(extent " << extent.x << ", " << extent.y << ", "
                      << extent.z << ") - using box shape instead of a convex "
                      << "hull to avoid an unstable hull build.\n";
        }
    }

    if (colliderType == ColliderType::Box || hullPoints.size() < 4 || geometryDegenerate) {
        const glm::vec3 safeSize = glm::max(size, glm::vec3(0.01f));
        JPH::BoxShapeSettings shapeSettings(
            JPH::Vec3(safeSize.x * 0.5f, safeSize.y * 0.5f, safeSize.z * 0.5f),
            0.0f);
        const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
        if (shape.HasError()) {
            std::cerr << "Jolt fallback box shape creation failed: "
                      << shape.GetError() << std::endl;
            return nullptr;
        }
        const JPH::EMotionType motion = type == BodyType::Static ? JPH::EMotionType::Static :
            type == BodyType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
        const JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? cNonMoving : cMoving;
        JPH::BodyCreationSettings settings(
            shape.Get(), JPH::RVec3(node->getPosition().x, node->getPosition().y, node->getPosition().z),
            JPH::Quat(node->getRotation().x, node->getRotation().y,
                      node->getRotation().z, node->getRotation().w),
            motion, layer);
        settings.mFriction = friction;
        settings.mRestitution = restitution;
        const JPH::BodyID bodyID = m_physicsSystem.GetBodyInterface().CreateAndAddBody(
            settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate :
            JPH::EActivation::Activate);
        if (bodyID.IsInvalid()) {
            std::cerr << "[Physics] Failed to create fallback rigid body for node '"
                      << node->getName() << "'\n";
            return nullptr;
        }
        if (!m_physicsSystem.GetBodyInterface().IsAdded(bodyID)) {
            std::cerr << "[Physics] Fallback rigid body was not added for node '"
                      << node->getName() << "'\n";
            return nullptr;
        }
        result->m_bodyID = bodyID;
        result->m_shape = shape.Get();
        m_bodies.push_back(std::move(body));
        return result;
    }
    const glm::vec3 safeSize = glm::max(size, glm::vec3(0.01f));
    JPH::ConvexHullShapeSettings shapeSettings(
        hullPoints.data(), static_cast<int>(hullPoints.size()));
    const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
    JPH::ShapeRefC collisionShape;
    if (shape.HasError()) {
        std::cerr << "[Physics] Convex hull creation failed for node '"
                  << node->getName() << "': " << shape.GetError()
                  << "; using bounds box fallback\n";
        JPH::BoxShapeSettings fallbackSettings(
            JPH::Vec3(safeSize.x * 0.5f, safeSize.y * 0.5f, safeSize.z * 0.5f),
            0.0f);
        const JPH::ShapeSettings::ShapeResult fallback =
            fallbackSettings.Create();
        if (fallback.HasError()) {
            std::cerr << "[Physics] Bounds box fallback failed: "
                      << fallback.GetError() << '\n';
            return nullptr;
        }
        collisionShape = fallback.Get();
    } else {
        collisionShape = shape.Get();
    }
    const JPH::EMotionType motion = type == BodyType::Static ? JPH::EMotionType::Static :
        type == BodyType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    const JPH::ObjectLayer layer = motion == JPH::EMotionType::Static ? cNonMoving : cMoving;
    JPH::BodyCreationSettings settings(
        collisionShape, JPH::RVec3(node->getPosition().x, node->getPosition().y, node->getPosition().z),
        JPH::Quat(node->getRotation().x, node->getRotation().y,
                  node->getRotation().z, node->getRotation().w),
        motion, layer);
    settings.mFriction = friction;
    settings.mRestitution = restitution;
    const JPH::BodyID bodyID = m_physicsSystem.GetBodyInterface().CreateAndAddBody(
        settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate :
        JPH::EActivation::Activate);
    if (bodyID.IsInvalid()) {
        std::cerr << "[Physics] Failed to create rigid body for node '"
                  << node->getName() << "'\n";
        return nullptr;
    }
    if (!m_physicsSystem.GetBodyInterface().IsAdded(bodyID)) {
        std::cerr << "[Physics] Rigid body was not added for node '"
                      << node->getName() << "'\n";
        return nullptr;
    }
    result->m_bodyID = bodyID;
    result->m_shape = collisionShape;
#else
    (void)friction;
    (void)restitution;
#endif
    m_bodies.push_back(std::move(body));
    return result;
}

void PhysicsSystem::update(float renderDeltaTime) {
    if (!m_initialized)
        return;
    m_accumulator += std::min(static_cast<double>(renderDeltaTime), 0.25);
    constexpr double fixedStep = 1.0 / 60.0;
    while (m_accumulator >= fixedStep) {
#ifdef KITBASHER_ENABLE_JOLT
        m_physicsSystem.Update(static_cast<float>(fixedStep), 1,
                               m_tempAllocator, m_jobSystem);
#endif
        m_accumulator -= fixedStep;
    }
    for (auto& body : m_bodies) {
        if (body == nullptr || body->m_node == nullptr)
            continue;
        if (body->m_node->isEditorManipulated())
            body->syncToPhysics();
        else
            body->syncFromPhysics();
    }
}

std::vector<PhysicsDebugLine> PhysicsSystem::getDebugLines() const {
    std::vector<PhysicsDebugLine> lines;
    if (!m_debugDrawEnabled)
        return lines;
    for (const auto& body : m_bodies) {
        if (body == nullptr)
            continue;
#if defined(_CPPUNWIND)
        try {
#endif
            body->appendDebugLines(lines);
#if defined(_CPPUNWIND)
        } catch (const std::exception& error) {
            if (!body->m_debugExceptionLogged) {
                std::cerr << "[Physics] Collision debug exception for node '"
                          << (body->m_node != nullptr ? body->m_node->getName() : "<null>")
                          << "': " << error.what() << '\n';
                body->m_debugExceptionLogged = true;
            }
        } catch (const std::string& error) {
            if (!body->m_debugExceptionLogged) {
                std::cerr << "[Physics] Collision debug string exception for node '"
                          << (body->m_node != nullptr ? body->m_node->getName() : "<null>")
                          << "': " << error << '\n';
                body->m_debugExceptionLogged = true;
            }
        } catch (const char* error) {
            if (!body->m_debugExceptionLogged) {
                std::cerr << "[Physics] Collision debug C-string exception for node '"
                          << (body->m_node != nullptr ? body->m_node->getName() : "<null>")
                          << "': " << (error != nullptr ? error : "<null>") << '\n';
                body->m_debugExceptionLogged = true;
            }
        } catch (...) {
            if (!body->m_debugExceptionLogged) {
                std::cerr << "[Physics] Collision debug unknown exception for node '"
                          << (body->m_node != nullptr ? body->m_node->getName() : "<null>")
                          << "'\n";
                body->m_debugExceptionLogged = true;
            }
        }
#endif
    }
    return lines;
}

void PhysicsSystem::setBodyPosition(Node* node, const glm::vec3& position) {
#ifdef KITBASHER_ENABLE_JOLT
    for (const auto& body : m_bodies) {
        if (body != nullptr && body->m_node == node && !body->m_bodyID.IsInvalid()) {
            m_physicsSystem.GetBodyInterface().SetPosition(
                body->m_bodyID,
                JPH::RVec3(position.x, position.y, position.z),
                JPH::EActivation::Activate);
            break;
        }
    }
#endif
}

void PhysicsSystem::addForce(Node* node, const glm::vec3& force) {
#ifdef KITBASHER_ENABLE_JOLT
    for (const auto& body : m_bodies) {
        if (body != nullptr && body->m_node == node && !body->m_bodyID.IsInvalid()) {
            m_physicsSystem.GetBodyInterface().AddForce(
                body->m_bodyID,
                JPH::Vec3(force.x, force.y, force.z));
            break;
        }
    }
#endif
}

void PhysicsSystem::setLinearVelocity(Node* node, const glm::vec3& velocity) {
#ifdef KITBASHER_ENABLE_JOLT
    for (const auto& body : m_bodies) {
        if (body != nullptr && body->m_node == node && !body->m_bodyID.IsInvalid()) {
            m_physicsSystem.GetBodyInterface().SetLinearVelocity(
                body->m_bodyID,
                JPH::Vec3(velocity.x, velocity.y, velocity.z));
            break;
        }
    }
#endif
}