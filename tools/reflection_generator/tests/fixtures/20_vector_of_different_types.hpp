// Vector fields with various element types

class [[ToastNode]] VectorTypesNode {
public:
    [[Reflect]] std::vector<int> m_ints;
    [[Reflect]] std::vector<float> m_floats;
    [[Reflect]] std::vector<std::string> m_strings;
    [[Reflect]] std::vector<glm::vec3> m_vectors;
};
