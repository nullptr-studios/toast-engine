// Fields with fully qualified type names
namespace game {

class [[ToastNode]] QualifiedNode {
public:
    [[Reflect]] std::string m_str;
    [[Reflect]] glm::vec3 m_pos;
    [[Reflect]] toast::UID m_id;
    [[Reflect]] ::global::Type m_global;
};
}
