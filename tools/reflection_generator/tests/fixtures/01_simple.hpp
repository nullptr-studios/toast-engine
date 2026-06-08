// Simple class: no inheritance, global namespace, basic group
namespace toast {
class [[ToastNode]] SimpleNode {
public:
    [[Serialize, Group("Transform")]] int m_x = 0;
    [[Serialize, Group("Transform")]] int m_y = 0;
    [[Serialize]] std::string m_name;

    void tick() {}
};
}
