// Class with parent (single inheritance)
namespace game {
class [[ToastNode]] Player : public toast::Node {
public:
    [[Reflect, Group("Stats")]] int m_health;
    [[Reflect, Group("Stats"), Subgroup("Damage")]] int m_armor;

    void init() {}
    void tick() {}
};
}
