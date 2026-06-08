// All supported field types

class [[ToastNode]] AllTypesNode {
public:
    [[Serialize]] bool m_bool_val;
    [[Serialize]] int m_int_val;
    [[Serialize]] float m_float_val;
    [[Serialize]] double m_double_val;
    [[Serialize]] std::string m_string_val;
    [[Serialize]] glm::vec2 m_vec2_val;
    [[Serialize]] glm::vec3 m_vec3_val;
    [[Serialize]] glm::vec4 m_vec4_val;
    [[Serialize]] glm::quat m_quat_val;
    [[Serialize]] UUID m_uuid_val;
    [[Serialize]] std::vector<int> m_int_array;
};
