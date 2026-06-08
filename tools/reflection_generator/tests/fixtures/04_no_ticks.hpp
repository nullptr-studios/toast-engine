// Class with fields, no tick functions
namespace data {

class [[ToastNode]] DataNode {
public:
    [[Serialize]] float m_value;
    [[Serialize]] std::vector<int> m_items;
};
}
