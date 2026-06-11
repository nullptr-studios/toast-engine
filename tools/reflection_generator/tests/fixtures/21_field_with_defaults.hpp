// Fields with various default values

class [[ToastNode]] DefaultsNode {
public:
    [[Reflect]] int m_int = 42;
    [[Reflect]] float m_float = 3.14f;
    [[Reflect]] std::string m_str = "hello";
    [[Reflect]] bool m_flag = true;
    [[Reflect]] glm::vec3 m_vec = glm::vec3(1, 2, 3);
};
