// Many subgroups in same group

class [[ToastNode]] DeeplyNestedNode {
public:
    [[Reflect, Group("Physics"), Subgroup("Collider")]] float m_c1;
    [[Reflect, Group("Physics"), Subgroup("Collider")]] float m_c2;
    [[Reflect, Group("Physics"), Subgroup("Collider")]] float m_c3;
    [[Reflect, Group("Physics"), Subgroup("Dynamics")]] float m_d1;
    [[Reflect, Group("Physics"), Subgroup("Dynamics")]] float m_d2;
    [[Reflect, Group("Physics"), Subgroup("Constraints")]] float m_k1;
    [[Reflect, Group("Physics"), Subgroup("Constraints")]] float m_k2;
    [[Reflect, Group("Physics"), Subgroup("Constraints")]] float m_k3;
    [[Reflect, Group("Physics"), Subgroup("Constraints")]] float m_k4;
    [[Reflect, Group("Physics"), Subgroup("Debug")]] bool m_debug;
};
