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
    explicit WrongCtor(float other) : value(0) {}
};
"#;
    let events = parse_events(source, "events.hpp");
    assert_eq!(events.len(), 2);
    assert!(events[0].skip_reason.as_deref().unwrap().contains("unsupported type"));
    assert!(events[1].supported && !events[1].sendable);
}

#[test]
fn accepts_aggregate_events_with_node_and_enum_fields() {
    let source = r#"
enum class DamageType : uint8_t { physical, bullet };
struct DamageEvent : public event::Event<DamageEvent> {
    toast::Box<toast::Node> target;
    toast::Box<toast::Node> attacker;
    float amount = 0.0f;
    DamageType type = DamageType::physical;
};
struct Defaulted : Event<Defaulted> {
    int value;
    Defaulted() : value(0) {}
};
"#;
    let events = parse_events(source, "damage_event.hpp");
    assert_eq!(events.len(), 2);
    assert!(events.iter().all(|e| e.supported), "{:?}", events.iter().map(|e| &e.skip_reason).collect::<Vec<_>>());
    let types: Vec<_> = events[0].fields.iter().map(|f| f.lua_type.as_deref().unwrap()).collect();
    assert_eq!(types, ["Node", "Node", "number", "integer"]);
}
