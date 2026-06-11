// Multiple classes in one file
namespace test {

class [[ToastNode]] FirstNode {
public:
    [[Reflect]] int m_value;
    void tick() {}
};


class [[ToastNode]] SecondNode {
public:
    [[Reflect]] std::string m_name;
};
}
