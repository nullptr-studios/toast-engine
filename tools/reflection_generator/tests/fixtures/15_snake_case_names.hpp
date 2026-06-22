// Group/subgroup names with mixed case, spaces, underscores

class [[ToastNode]] SnakeCaseNode {
public:
    [[Reflect, Group("Physics Settings"), Subgroup("Rigid Body")]] float m_mass;
    [[Reflect, Group("Physics Settings"), Subgroup("Rigid Body")]] float m_friction;
    [[Reflect, Group("UI Components"), Subgroup("Button State")]] bool m_focused;
    [[Reflect, Group("AI_Behavior"), Subgroup("Pathfinding_Weights")]] float m_dist_weight;
};
