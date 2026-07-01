// Pointer and reference types

class [[ToastNode]] PointerNode {
public:
    [[Reflect]] int* m_ptr;
    [[Reflect]] int& m_ref;
    [[Reflect]] Box<Texture> m_texture;
};
