// Interleaved groups and globals

class [[ToastNode]] SparseGroupNode {
public:
    [[Serialize]] int m_global1;
    [[Serialize, Group("A")]] int m_a1;
    [[Serialize]] int m_global2;
    [[Serialize, Group("B")]] int m_b1;
    [[Serialize, Group("A")]] int m_a2;
    [[Serialize]] int m_global3;
    [[Serialize, Group("B")]] int m_b2;
    [[Serialize, Group("A")]] int m_a3;
};
