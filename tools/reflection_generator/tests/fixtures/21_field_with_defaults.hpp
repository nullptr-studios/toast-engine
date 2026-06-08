// Fields with various default values

class [[ToastNode]] DefaultsNode {
public:
    [[Serialize]] int m_int = 42;
    [[Serialize]] float m_float = 3.14f;
    [[Serialize]] std::string m_str = "hello";
    [[Serialize]] bool m_flag = true;
    [[Serialize]] glm::vec3 m_vec = glm::vec3(1, 2, 3);
};
