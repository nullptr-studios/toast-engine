// Multi-level inheritance with namespaced parent
namespace game {

class [[ToastNode]] GrandchildNode : public game::ParentNode {
public:
    [[Serialize]] int m_extra;
};
}
