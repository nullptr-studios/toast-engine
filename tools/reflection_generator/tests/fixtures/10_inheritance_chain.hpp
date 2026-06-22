// Multi-level inheritance with namespaced parent
namespace game {

class [[ToastNode]] GrandchildNode : public game::ParentNode {
public:
    [[Reflect]] int m_extra;
};
}
