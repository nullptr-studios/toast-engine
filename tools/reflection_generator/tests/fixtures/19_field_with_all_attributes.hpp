// Single field with multiple attributes

class [[ToastNode]] AttributeRichNode {
public:
    [[Reflect, Name("Player Health"), Custom("editor", "slider"), Custom("range", "0", "100"), Custom("color", "red")]] int m_health;
};
