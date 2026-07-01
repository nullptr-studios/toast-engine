// Simple class: no inheritance, global namespace, basic group
namespace toast {
class [[ToastNode]] SimpleNode {
public:
    [[Reflect, Group("Transform")]] int m_x = 0;
    [[Reflect, Group("Transform")]] int m_y = 0;
    [[Reflect]] std::string m_name;

    void tick() {}
};
}
