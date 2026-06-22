// Multiple groups with multiple subgroups

class [[ToastNode]] ComplexNode {
public:
    [[Reflect, Group("Physics")]] float m_mass;
    [[Reflect, Group("Physics"), Subgroup("Velocity")]] float m_vx;
    [[Reflect, Group("Physics"), Subgroup("Velocity")]] float m_vy;
    [[Reflect, Group("Physics"), Subgroup("Acceleration")]] float m_ax;
    [[Reflect, Group("Physics"), Subgroup("Acceleration")]] float m_ay;
    [[Reflect, Group("Rendering")]] std::string m_shader;
    [[Reflect, Group("Rendering"), Subgroup("Transform")]] float m_scale;
};
