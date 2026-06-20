// Class with fields, no tick functions
namespace data {

class [[ToastNode]] DataNode {
public:
    [[Reflect]] float m_value;
    [[Reflect]] std::vector<int> m_items;
};
}
