#pragma endian little

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
};

struct Vec4 {
    float x, y, z, w;
};

struct Vertex {
    Vec3 position [[color("e74c3c")]];
    Vec3 normal   [[color("2ecc71")]];
    Vec2 uv       [[color("3498db")]];
    Vec4 tangent  [[color("e67e22")]];
    Vec3 color    [[color("9b59b6")]];
};

struct MeshHeader {
    char   magic[6];     // "TMESH\0"
    u16    version;      // currently 1
    u32    vertex_count;
    u32    index_count;
};

struct TMesh {
    MeshHeader header;
    u8         name_length;
    char       name[name_length];
    Vertex     vertices[header.vertex_count];
    u32        indices[header.index_count];
};

TMesh mesh @ 0x00;
