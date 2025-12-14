#include "AgentSystem.h"
#include "MeshViewerWidget.h"
#include <algorithm>

void AgentSystem::clear() {
    m_agents.clear();
}

SurfaceAgent& AgentSystem::add(const SurfaceAgent& a) {
    m_agents.push_back(a);
    return m_agents.back();
}

SurfaceAgent& AgentSystem::add() {
    m_agents.emplace_back();
    return m_agents.back();
}

void AgentSystem::removeAt(std::size_t idx) {
    if (idx >= m_agents.size()) return;
    m_agents.erase(m_agents.begin() + static_cast<long>(idx));
}

void AgentSystem::removeIf(const std::function<bool(const SurfaceAgent&)>& pred) {
    m_agents.erase(std::remove_if(m_agents.begin(), m_agents.end(), pred), m_agents.end());
}

void AgentSystem::initialize(int count, const std::function<bool(SurfaceAgent&)>& spawner) {
    m_agents.clear();
    m_agents.reserve(count);
    for (int i = 0; i < count; ++i) {
        SurfaceAgent a;
        if (spawner && spawner(a)) {
            m_agents.push_back(a);
        }
    }
}
