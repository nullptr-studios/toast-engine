// Reflected member functions: parameters, default values, custom struct args, and void returns.
// Only [[Reflect]]-marked functions are reflected; plain members and tick functions are not.
namespace toast {

struct AttackPlan {
    int strikes;
};

struct DamageReport {
    int total;
    bool lethal;
};

class [[ToastNode]] FunctionNode {
public:
    [[Reflect, Group("Stats")]] int m_health = 100;

    [[Reflect]] int takeDamage(int amount, bool crit = false) { return m_health; }

    [[Reflect]] void teleport(glm::vec3 destination) {}

    [[Reflect]] DamageReport simulate(const AttackPlan& plan, float scale = 1.0f) { return {}; }

    [[Reflect]] bool isAlive() const { return m_health > 0; }

    // Not reflected: no annotation
    void internalHelper() {}

    void tick() {}
};
}
