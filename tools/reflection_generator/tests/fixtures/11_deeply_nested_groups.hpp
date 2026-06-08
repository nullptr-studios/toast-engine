// Many subgroups in same group

class [[ToastNode]] DeeplyNestedNode {
public:
    [[Serialize, Group("Physics"), Subgroup("Collider")]] float m_c1;
    [[Serialize, Group("Physics"), Subgroup("Collider")]] float m_c2;
    [[Serialize, Group("Physics"), Subgroup("Collider")]] float m_c3;
    [[Serialize, Group("Physics"), Subgroup("Dynamics")]] float m_d1;
    [[Serialize, Group("Physics"), Subgroup("Dynamics")]] float m_d2;
    [[Serialize, Group("Physics"), Subgroup("Constraints")]] float m_k1;
    [[Serialize, Group("Physics"), Subgroup("Constraints")]] float m_k2;
    [[Serialize, Group("Physics"), Subgroup("Constraints")]] float m_k3;
    [[Serialize, Group("Physics"), Subgroup("Constraints")]] float m_k4;
    [[Serialize, Group("Physics"), Subgroup("Debug")]] bool m_debug;
};
