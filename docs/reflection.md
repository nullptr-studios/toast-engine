# Reflection

> Authors: Xein (& Dante)
>
> Created on: 11 Jun 2026

The reflection system allows you to get real-time information about any member, function,
and other properties of any Node class. It also allows you to find and set any of their
values by string, which is useful when you don't know what members are on a class or if
one of them exists.

Any `Node` class comes with its own `toast::NodeInfo* info() const;` where you can access
all the information about its reflected data:

```c++
auto info = my_node->info();
auto player_health = info.search("health");
if (player_health) player_health.set(my_node, 100.0f);
```

It also allows as, for example, to create a `newNode()` function that can return different
node types without needing to be templated or by checking the type on runtime:

```c++
auto World::newNode(std::string_view type) -> Node* {
    // this searches for the type in the world registry database
    const toast::NodeInfo* info = m.node_registry.reflect(type);
    
    // generate it using the constructor function on the reflect info
    Node* raw_node = info ? info.construct() : new Node;
    return raw_node;
}
```

## Allowed attributes

### Node level attributes

- `[[ToastNode]]`: every node should have this attribute to generate reflection information
- `[[Color("str")]]`: changes the color a Node will have on the Inspector and Hierarchy panels
- `[[Icon("str")]]`: changes the icon a Node will have on the Inspector and Hierarchy panels
- `[[Hidden]]`: hides this Node from the Create Node window

### Field level attributes

- `[[Reflect]]`: marks a field to be reflected
- `[[Name("str")]]`: overrides the attribute name on the Inspector panel
- `[[ReadOnly]]`: value will appear as read-only on the Inspector panel
- `[[Hidden]]`: value will be saved on files but won't appear on the Inspector panel
- `[[Group("str")]]`: adds attribute to a given group
- `[[Subgroup("str")]]`: adds attribute to a given subgroup (must have a valid Group attribute)
- `[[Range(int, int)]]`: limits the range in the Inspector panel
- `[[Enum("str", ...)]]`: makes the int field appears as a drop-down menu
- `[[BitEnum("str", ...)]]`: makes the int field appear as a multiple choice drop-down menu

## Motivation

We needed a reflection system for our game engine, which will be used for a lot of core
components like serialization and deserialization, editor interoperability, ticking, etc.

Initially, we presented the idea of using C++26 reflection for a deeply integrated static
reflection system, but the lack of support for cross-compilation made us having to create
our own reflection implementation.

The core of this feature was to implement some sort of static-based member reflection with
a code generator but without interfering the client with odd syntax or macros when working
on a game. That set us with the rules of *not using macros and not modifying the class
header file* which were some very difficult ground rules.

We implemented the system by parsing the header files with an AST generator and using
native `[[attributes]]` to add aditional information to the fields on our classes.

A completely `constexpr` templated struct called `Reflect<T>` would hold all reflected
fields, functions and attributes on a given class, with getters and setters and a bunch
of other functions like RTTI, Factory functions, destructors, etc. to avoid the use of
vtables on the `Node` classes while maintaining inheritance and polymorphism. A generator
coded mainly in rust (`tools/reflection_generator`) would then parse all header files that
contain a `[[ToastNode]]` attribute and generate an Abstract Syntax Tree with Tree-sitter.
Then, it would generate C++ files that would get compiled with the project and would fill
out all the information required by the `Reflect<T>` class.

A simple header file
```c++
class [[ToastNode]] Demo {
    [[Reflect
    int my_variable;
    
    [[Reflect, Name("Enable")]]
    bool is_enabled;
};
```
gets a generated file that populates a `Reflect<Demo>` struct in the following way:
```c++
// Simplified example, missing properties
template<>
struct Reflect<Demo> {
    static constexpr std::string_view name = "::Demo";
    
    inline static const std::array<FieldInfo, 2> _all_field_info = {{
        {
            .name       = "my_variable",
            .type       = "int",
            .attributes = {{}},
            .get        = &FieldAccess<Demo, int, &my_variable>::get,
            .set        = &FieldAccess<Demo, int, &my_variable>::set,
        },
        {
            .name       = "Enable",
            .type       = "bool,
            .attributes = {"Name" : ["Enable"]},
            .get        = &FieldAccess<Demo, bool, &is_enabled>::get,
            .set        = &FieldAccess<Demo, bool, &is_enabled>::set,
        }
    }};
    
    inline static const toast::NodeInfo type_info = {
        .type       = name,
        .base_type  = nullptr,
    };
};
```

This is the interface for a NodeInfo struct:
```c++
struct TOAST_API NodeInfo {
using Factory = Node* (*)();
using Deleter = void (*)(Node*);

	std::string_view type;                      // this is the literal type as a string (contains namespaces)
	const NodeInfo* base_type;                  // this holds a pointer to the type it inherits from (nullptr if none)
	std::span<const FieldInfo> all_fields;      // list of all of the fields [variables] of the class
	std::span<const FieldInfo* const> fields;   // list of all the ungrouped fields of the class
	std::span<const GroupInfo> groups;          // list of all of the groups of the class

	TickFunctions functions;                    // contains pointers to the tick functions
                                                // when called from the world, it also calls the ones on base_type

	Factory construct = nullptr;                // function to create a new Node
	Deleter destroy = nullptr;                  // function to destroy the Node

	[[nodiscard]]
	auto getField(std::string_view field_name) const -> const FieldInfo*;

	[[nodiscard]]
	auto isA(const NodeInfo* other) const -> bool; // RTTI alternative to check if it is a type or inherits from it

	[[nodiscard]]
	auto hasFunction(TickFunctionList mask) const -> bool;

	[[nodiscard]]
	constexpr auto id() const -> uint32_t;          // uint32_t hash of the std::string_view type
                                                    // do not confuse with the UID uid() of a node

	[[nodiscard]]
	auto getGroup(std::string_view group_name) const -> const GroupInfo*;

	[[nodiscard]]
	auto search(std::string_view field_name) const -> const FieldInfo*;

	// Walk the inheritance chain and call fn for each NodeInfo (base → derived)
	template<typename F>
	void forEachBaseType(F&& fn) const;
};
```
