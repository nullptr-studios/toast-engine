use tree_sitter::{Parser};

#[test]
fn debug_tree_simple() {
    let source = r#"class [[ToastNode]] SimpleNode {
public:
    int m_x;
};"#;

    let mut parser = Parser::new();
    parser.set_language(&tree_sitter_cpp::LANGUAGE.into()).expect("Language error");
    let tree = parser.parse(source, None).unwrap();

    print_tree(&tree.root_node(), source, 0);
}

fn print_tree(node: &tree_sitter::Node, source: &str, indent: usize) {
    let kind = node.kind();
    let text = if node.child_count() == 0 {
        format!(" \"{}\"", &source[node.byte_range()])
    } else {
        String::new()
    };
    eprintln!("{}{}{}", " ".repeat(indent * 2), kind, text);

    for child in node.children(&mut node.walk()) {
        print_tree(&child, source, indent + 1);
    }
}
