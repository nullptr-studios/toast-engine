// Field with Name and custom attributes

class [[ToastNode]] AttributeNode {
public:
    [[Serialize, Name("Display Name"), Custom("editor", "fancy")]] int m_value;
    [[Serialize, Group("Settings"), Name("Speed"), Custom("range", "0", "100")]] float m_speed;
};
