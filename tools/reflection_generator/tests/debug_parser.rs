use reflection_generator::parse;

#[test]
fn debug_parse_simple() {
    let source = r#"
namespace toast {
[[ToastNode]]
class SimpleNode {
public:
    [[Serialize, Group("Transform")]] int m_x = 0;
    [[Serialize]] std::string m_name;

    void tick() {}
};
}
"#;

    let classes = parse(source);
    eprintln!("Found {} classes", classes.len());
    for class in &classes {
        eprintln!("Class: {}", class.name);
        eprintln!("  Namespace: {:?}", class.namespace);
        eprintln!("  Attributes: {} ({:?})", class.attributes.len(), class.attributes.iter().map(|a| &a.name).collect::<Vec<_>>());
        eprintln!("  Fields: {}", class.fields.len());
        for field in &class.fields {
            eprintln!("    - {}: {}", field.name, field.type_name);
        }
        eprintln!("  Functions: {:?}", class.functions);
    }

    assert!(!classes.is_empty(), "Parser should find at least one class");
}
