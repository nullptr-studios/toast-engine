// Group/subgroup names with mixed case, spaces, underscores

class [[ToastNode]] SnakeCaseNode {
public:
    [[Serialize, Group("Physics Settings"), Subgroup("Rigid Body")]] float m_mass;
    [[Serialize, Group("Physics Settings"), Subgroup("Rigid Body")]] float m_friction;
    [[Serialize, Group("UI Components"), Subgroup("Button State")]] bool m_focused;
    [[Serialize, Group("AI_Behavior"), Subgroup("Pathfinding_Weights")]] float m_dist_weight;
};
