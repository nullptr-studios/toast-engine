// All supported field types

class [[ToastNode]] AllTypesNode {
public:
    [[Reflect]] bool m_bool_val;
    [[Reflect]] int m_int_val;
    [[Reflect]] float m_float_val;
    [[Reflect]] double m_double_val;
    [[Reflect]] std::string m_string_val;
    [[Reflect]] glm::vec2 m_vec2_val;
    [[Reflect]] glm::vec3 m_vec3_val;
    [[Reflect]] glm::vec4 m_vec4_val;
    [[Reflect]] glm::quat m_quat_val;
    [[Reflect]] UID m_uid_val;
    [[Reflect]] std::vector<int> m_int_array;
};
