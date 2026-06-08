// Edge case: only global fields, no groups

class [[ToastNode]] EdgeCaseNode {
public:
    [[Serialize]] int m_value;
    [[Serialize]] float m_another;
};
