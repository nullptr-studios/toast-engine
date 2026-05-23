#pragma endian little

enum ItemType : u8 {
    bool_t = 0,
    int_t = 1,
    float_t = 2,
    string_t = 3,
    double_t = 4,
    uuid_t = 5,
    vec2_t = 6,
    vec3_t = 7,
    vec4_t = 8,
    quaternion_t = 9
};

struct String {
    u32 length;
    char data[length];
} [[inline]];

struct Vec2 {
    float x;
    float y;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct Quat {
    float x;
    float y;
    float z;
    float w;
};

struct UUID {
    u8 data[11];
};

struct Item {
    String name;
    ItemType type;
    u8 is_array;

    if (is_array != 0) {
        u32 count;
        if (type == ItemType::bool_t) { bool values[count]; }
        else if (type == ItemType::int_t) { s32 values[count]; }
        else if (type == ItemType::float_t) { float values[count]; }
        else if (type == ItemType::double_t) { double values[count]; }
        else if (type == ItemType::string_t) { String values[count]; }
        else if (type == ItemType::uuid_t) { UUID values[count]; }
        else if (type == ItemType::vec2_t) { Vec2 values[count]; }
        else if (type == ItemType::vec3_t) { Vec3 values[count]; }
        else if (type == ItemType::vec4_t) { Vec4 values[count]; }
        else if (type == ItemType::quaternion_t) { Quat values[count]; }
    } else {
        if (type == ItemType::bool_t) { bool value; }
        else if (type == ItemType::int_t) { s32 value; }
        else if (type == ItemType::float_t) { float value; }
        else if (type == ItemType::double_t) { double value; }
        else if (type == ItemType::string_t) { String value; }
        else if (type == ItemType::uuid_t) { UUID value; }
        else if (type == ItemType::vec2_t) { Vec2 value; }
        else if (type == ItemType::vec3_t) { Vec3 value; }
        else if (type == ItemType::vec4_t) { Vec4 value; }
        else if (type == ItemType::quaternion_t) { Quat value; }
    }
};

struct Subgroup {
    String name;
    u32 item_count;
    Item items[item_count];
};

struct Group {
    String name;
    u32 item_count;
    Item items[item_count];
    
    u32 subgroup_count;
    Subgroup subgroups[subgroup_count];
};

struct Node {
    String name;
    String type;
    
    u32 item_count;
    Item items[item_count];
    
    u32 group_count;
    Group groups[group_count];
};

struct NodeFileBinaryHeader {
    char magic[6];
    u16 version;
    u32 node_count;
};

struct TNODE {
    NodeFileBinaryHeader header;
    
    u32 global_item_count;
    Item global_items[global_item_count];
    
    Node nodes[header.node_count];
};

TNODE file @ 0x00;