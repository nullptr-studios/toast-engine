// Multiple classes in one file
namespace test {

class [[ToastNode]] FirstNode {
public:
    [[Serialize]] int m_value;
    void tick() {}
};


class [[ToastNode]] SecondNode {
public:
    [[Serialize]] std::string m_name;
};
}
