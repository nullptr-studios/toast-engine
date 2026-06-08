// Vector fields with various element types

class [[ToastNode]] VectorTypesNode {
public:
    [[Serialize]] std::vector<int> m_ints;
    [[Serialize]] std::vector<float> m_floats;
    [[Serialize]] std::vector<std::string> m_strings;
    [[Serialize]] std::vector<glm::vec3> m_vectors;
};
