#include "Toast/Physics/ColliderRenderable.hpp"
#include "glm/matrix.hpp"

#include <vector>
#include <algorithm>
#include <glm/glm.hpp>

using namespace glm;

static bool isPointInTriangle(const vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    vec2 ab = b - a;
    vec2 bc = c - b;
    vec2 ca = a - c;

    vec2 ap = p - a;
    vec2 bp = p - b;
    vec2 cp = p - c;

    float cross1 = determinant(glm::mat2{ab, ap});
    float cross2 = determinant(glm::mat2{bc, bp});
    float cross3 = determinant(glm::mat2{ca, cp});

    // If the point is inside, all cross products will have the same sign.
    bool has_neg = (cross1 < 0.0f) || (cross2 < 0.0f) || (cross3 < 0.0f);
    bool has_pos = (cross1 > 0.0f) || (cross2 > 0.0f) || (cross3 > 0.0f);

    return !(has_neg && has_pos);
}

static std::vector<uint16_t> triangulate(const std::vector<vec3>& vertices) {
    std::vector<uint16_t> indices;
    
    // A polygon must have at least 3 vertices
    if (vertices.size() < 3) {
        return indices;
    }

    // Create a working list of indices representing the remaining polygon
    std::vector<size_t> remaining_indices(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        remaining_indices[i] = i;
    }

    // 1. Determine the overall winding order of the polygon.
    // Even if you don't care about the output winding, the algorithm needs to 
    // know what "inside" vs "outside" means to find convex angles.
    float area = 0.0f;
    for (size_t i = 0; i < vertices.size(); ++i) {
        size_t j = (i + 1) % vertices.size();
        area += vertices[i].x * vertices[j].y - vertices[j].x * vertices[i].y;
    }

    // If the area is negative, the vertices are clockwise.
    // We reverse our working indices to process everything as counter-clockwise.
    if (area < 0.0f) {
        std::reverse(remaining_indices.begin(), remaining_indices.end());
    }

    // 2. Ear Clipping Loop
    // We use a safety counter to prevent infinite loops if the polygon is 
    // self-intersecting or contains degenerate (overlapping) data.
    size_t safety_counter = vertices.size() * 2; 
    
    while (remaining_indices.size() > 3) {
        if (--safety_counter == 0) break; 

        bool ear_found = false;
        size_t count = remaining_indices.size();

        for (size_t i = 0; i < count; ++i) {
            size_t prev_idx = remaining_indices[(i + count - 1) % count];
            size_t curr_idx = remaining_indices[i];
            size_t next_idx = remaining_indices[(i + 1) % count];

            const vec2& prev = vertices[prev_idx];
            const vec2& curr = vertices[curr_idx];
            const vec2& next = vertices[next_idx];

            // Check if the vertex is convex (interior angle < 180).
            // Because we forced CCW, the cross product of the edges must be positive.
            vec2 edge1 = curr - prev;
            vec2 edge2 = next - curr;
            if (determinant(glm::mat2{edge1, edge2}) <= 0.00001f) {
                continue; // Angle is concave or collinear, cannot be an ear tip
            }

            // Check if any OTHER vertex of the polygon is inside this triangle
            bool is_ear = true;
            for (size_t j = 0; j < count; ++j) {
                if (j == i || j == (i + count - 1) % count || j == (i + 1) % count) {
                    continue; // Skip the vertices that make up the triangle itself
                }
                
                size_t test_idx = remaining_indices[j];
                if (isPointInTriangle(vertices[test_idx], prev, curr, next)) {
                    is_ear = false;
                    break;
                }
            }

            // If it's a valid ear, clip it!
            if (is_ear) {
                indices.push_back(prev_idx);
                indices.push_back(curr_idx);
                indices.push_back(next_idx);

                // Remove the ear tip from our working list
                remaining_indices.erase(remaining_indices.begin() + i);
                ear_found = true;
                break;
            }
        }

        // If we looped through all remaining vertices and couldn't find an ear, 
        // the polygon is likely invalid (e.g., self-intersecting). Break out.
        if (!ear_found) {
            break;
        }
    }

    // 3. Add the final remaining 3 vertices as the last triangle
    if (remaining_indices.size() == 3) {
        indices.push_back(remaining_indices[0]);
        indices.push_back(remaining_indices[1]);
        indices.push_back(remaining_indices[2]);
    }

    return indices;
}

void physics::ColliderRenderable::SendVertices(std::vector<glm::vec3>& points) {
    // clear things just in case
    m.points.clear();
    m.indices.clear();
    m.vertices.clear();
    m.topVertices.clear();
    m.topIndices.clear();

    this->m.points = points;
    this->m.indices = triangulate(points);
    CalculateBoundingBox();

    for (const auto& point : m.points) {
        m.vertices.emplace_back(renderer::SpineVertex {
                .position = glm::vec3 { point.x, point.y, 0.0 },
                .texCoord = { 0.0, 0.0 },
                .colorABGR = 0xFFFFFFFF
        });
    }

    m.mesh.UpdateDynamicSpine(m.vertices.data(), m.vertices.size(), m.indices.data(), m.indices.size());

    if (m.showTop && m.points.size() >= 2) {
        float cosThreshold = cos(glm::radians(m.maxSlope));
        float currentDistance = 0.0f;

        for (size_t i = 0; i < m.points.size(); ++i) {
            size_t nextIdx = (i + 1) % m.points.size();
            glm::vec3 p1 = m.points[i];
            glm::vec3 p2 = m.points[nextIdx];

            glm::vec3 edge = p2 - p1;
            float edgeLen = glm::length(edge);
            if (edgeLen < 0.0001f) continue;

            // Outward normal for CCW: (dy, -dx) But we need to know if it points UP
            glm::vec2 normal = glm::normalize(glm::vec2(p1.y - p2.y, p2.x - p1.x));
            
            // If normal points UP and within slope
            if (normal.y > cosThreshold) {
                uint16_t baseIdx = static_cast<uint16_t>(m.topVertices.size());
                
                float nextDistance = currentDistance + edgeLen;

                // V0: Bottom Left
                m.topVertices.emplace_back(renderer::SpineVertex{
                    .position = p1,
                    .texCoord = { currentDistance, 0.0f },
                    .colorABGR = 0xFFFFFFFF
                });
                // V1: Bottom Right
                m.topVertices.emplace_back(renderer::SpineVertex{
                    .position = p2,
                    .texCoord = { nextDistance, 0.0f },
                    .colorABGR = 0xFFFFFFFF
                });
                // V2: Top Right
                m.topVertices.emplace_back(renderer::SpineVertex{
                    .position = p2 + glm::vec3(0.0f, m.topHeight, 0.0f),
                    .texCoord = { nextDistance, 1.0f },
                    .colorABGR = 0xFFFFFFFF
                });
                // V3: Top Left
                m.topVertices.emplace_back(renderer::SpineVertex{
                    .position = p1 + glm::vec3(0.0f, m.topHeight, 0.0f),
                    .texCoord = { currentDistance, 1.0f },
                    .colorABGR = 0xFFFFFFFF
                });

                m.topIndices.push_back(baseIdx + 0);
                m.topIndices.push_back(baseIdx + 1);
                m.topIndices.push_back(baseIdx + 2);
                m.topIndices.push_back(baseIdx + 0);
                m.topIndices.push_back(baseIdx + 2);
                m.topIndices.push_back(baseIdx + 3);

                currentDistance = nextDistance;
            } else {
                // If we have a break in the top layer, we might want to reset distance 
                // but usually ground textures are continuous.
                // currentDistance = 0.0f; 
            }
        }
        
        if (!m.topVertices.empty()) {
            m.topMesh.UpdateDynamicSpine(m.topVertices.data(), m.topVertices.size(), m.topIndices.data(), m.topIndices.size());
        }
    }
}

