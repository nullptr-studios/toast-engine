// Fields with fully qualified type names
namespace game {

class [[ToastNode]] QualifiedNode {
public:
    [[Serialize]] std::string m_str;
    [[Serialize]] glm::vec3 m_pos;
    [[Serialize]] toast::UUID m_id;
    [[Serialize]] ::global::Type m_global;
};
}
