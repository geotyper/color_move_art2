#pragma once

#include <vector>
#include <functional>

struct SurfaceAgent;

// Simple container/manager for surface agents with lifecycle helpers.
class AgentSystem {
public:
    std::vector<SurfaceAgent>& agents() { return m_agents; }
    const std::vector<SurfaceAgent>& agents() const { return m_agents; }

    void clear();
    int count() const { return static_cast<int>(m_agents.size()); }

    SurfaceAgent& add(const SurfaceAgent& a);
    SurfaceAgent& add();
    void removeAt(std::size_t idx);
    void removeIf(const std::function<bool(const SurfaceAgent&)>& pred);

    // Utility to initialize N agents via provided spawn callback.
    void initialize(int count, const std::function<bool(SurfaceAgent&)>& spawner);

private:
    std::vector<SurfaceAgent> m_agents;
};
