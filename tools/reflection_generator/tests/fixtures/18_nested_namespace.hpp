// Class in deeply nested namespace
namespace foo::bar::baz {

class [[ToastNode]] DeepNode {
public:
    [[Serialize]] float m_x;
};
}
