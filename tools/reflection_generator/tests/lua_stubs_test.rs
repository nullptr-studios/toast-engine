use reflection_generator::*;

#[test]
fn stubs_contain_class_fields_methods_and_marker() {
    let source = r#"
namespace toast {
class [[ToastNode]] StubNode {
public:
    [[Reflect]] int m_health = 100;
    [[Reflect, Color]] glm::vec3 m_tint {1.0f};
    [[Reflect, Hidden]] int m_secret = 0;
    [[Reflect]] std::vector<assets::Handle<assets::Texture>> m_frames;
    [[Reflect]] Box<Node3D> m_target;

    [[Reflect]] float takeDamage(float amount, bool crit = false) { return 0.0f; }

    void tick() {}
};
}
"#;
    let classes = parse(source, "stub_node.hpp");
    assert_eq!(classes.len(), 1);
    let nodes: Vec<_> = classes.iter().map(build_node).collect();
    let stubs = generate_lua_stubs(&nodes);

    assert!(stubs.contains("---@class StubNode"), "class missing:\n{stubs}");
    assert!(stubs.contains("---@field health integer"), "m_ prefix should be stripped:\n{stubs}");
    assert!(stubs.contains("---@field tint color3"), "[[Color]] vec3 should be color3:\n{stubs}");
    assert!(!stubs.contains("secret"), "[[Hidden]] fields should be skipped:\n{stubs}");
    assert!(stubs.contains("---@field frames Asset[]"), "asset arrays should be Asset[]:\n{stubs}");
    assert!(stubs.contains("---@field target Node3D"), "Box<T> should use the bare node type:\n{stubs}");
    assert!(
        stubs.contains("---@field takeDamage fun(self: StubNode, amount: number, crit?: boolean): number"),
        "method signature wrong:\n{stubs}"
    );
    assert!(stubs.contains("StubNode = nil"), "type marker global missing:\n{stubs}");
}
