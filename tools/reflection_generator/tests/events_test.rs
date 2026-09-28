use reflection_generator::{generate_event_lua_stubs, parse_events};

#[test]
fn discovers_both_event_base_spellings_and_public_fields() {
    let source = r#"
namespace event {
struct Resize : Event<Resize> {
    int width, height;
    Resize(int width, int height) : width(width), height(height) {}
};
struct Scale : event::Event<Scale> {
    float value;
    explicit Scale(float value) : value(value) {}
private:
    int hidden;
};
}
"#;
    let events = parse_events(source, "events.hpp");
    assert_eq!(events.len(), 2);
    assert_eq!(events[0].fields.len(), 2);
    assert_eq!(events[0].fields[0].lua_type.as_deref(), Some("integer"));
    assert_eq!(events[1].fields.len(), 1);
    assert!(events.iter().all(|e| e.constructor_compatible && e.supported));
    let stubs = generate_event_lua_stubs(&events);
    assert!(stubs.contains("---@field Resize EventDescriptor"));
    assert!(stubs.contains("---@field width integer"));
}

#[test]
fn reports_unsupported_fields_and_incompatible_constructors() {
    let source = r#"
struct Unsupported : Event<Unsupported> {
    Widget value;
    Unsupported(Widget value) : value(value) {}
};
struct WrongCtor : Event<WrongCtor> {
    int value;
    WrongCtor() : value(0) {}
};
"#;
    let events = parse_events(source, "events.hpp");
    assert_eq!(events.len(), 2);
    assert!(events[0].skip_reason.as_deref().unwrap().contains("unsupported type"));
    assert!(events[1].skip_reason.as_deref().unwrap().contains("no constructor"));
}
