// Class with parent (single inheritance)
namespace game {
class [[ToastNode]] Player : public toast::Node {
public:
    [[Serialize, Group("Stats")]] int m_health;
    [[Serialize, Group("Stats"), Subgroup("Damage")]] int m_armor;

    void init() {}
    void tick() {}
};
}
