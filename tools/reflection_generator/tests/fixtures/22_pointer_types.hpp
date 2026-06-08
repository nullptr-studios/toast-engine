// Pointer and reference types

class [[ToastNode]] PointerNode {
public:
    [[Serialize]] int* m_ptr;
    [[Serialize]] int& m_ref;
    [[Serialize]] Box<Texture> m_texture;
};
