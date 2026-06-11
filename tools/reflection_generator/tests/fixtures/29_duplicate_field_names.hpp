// Fields with same name in different groups

class [[ToastNode]] DuplicateFieldNode {
public:
    [[Reflect, Group("A")]] int m_value;
    [[Reflect, Group("B")]] int m_value;
};
