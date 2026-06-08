// Fields with same name in different groups

class [[ToastNode]] DuplicateFieldNode {
public:
    [[Serialize, Group("A")]] int m_value;
    [[Serialize, Group("B")]] int m_value;
};
