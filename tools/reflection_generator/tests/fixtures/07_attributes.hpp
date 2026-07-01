// Field with Name and custom attributes

class [[ToastNode]] AttributeNode {
public:
    [[Reflect, Name("Display Name"), Custom("editor", "fancy")]] int m_value;
    [[Reflect, Group("Settings"), Name("Speed"), Custom("range", "0", "100")]] float m_speed;
};
