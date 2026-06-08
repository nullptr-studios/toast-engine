// Multiple groups with multiple subgroups

class [[ToastNode]] ComplexNode {
public:
    [[Serialize, Group("Physics")]] float m_mass;
    [[Serialize, Group("Physics"), Subgroup("Velocity")]] float m_vx;
    [[Serialize, Group("Physics"), Subgroup("Velocity")]] float m_vy;
    [[Serialize, Group("Physics"), Subgroup("Acceleration")]] float m_ax;
    [[Serialize, Group("Physics"), Subgroup("Acceleration")]] float m_ay;
    [[Serialize, Group("Rendering")]] std::string m_shader;
    [[Serialize, Group("Rendering"), Subgroup("Transform")]] float m_scale;
};
