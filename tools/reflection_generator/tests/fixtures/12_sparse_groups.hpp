// Interleaved groups and globals

class [[ToastNode]] SparseGroupNode {
public:
    [[Reflect]] int m_global1;
    [[Reflect, Group("A")]] int m_a1;
    [[Reflect]] int m_global2;
    [[Reflect, Group("B")]] int m_b1;
    [[Reflect, Group("A")]] int m_a2;
    [[Reflect]] int m_global3;
    [[Reflect, Group("B")]] int m_b2;
    [[Reflect, Group("A")]] int m_a3;
};
