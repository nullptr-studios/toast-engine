#pragma endian little

enum BrickTag : u8 {
    Empty   = 0,
    Uniform = 1,
    Shared  = 2,
    Owned   = 3
};

bitfield BrickEntry {
    BrickTag tag : 2;
    payload      : 30;
};

struct VoxelFileHeader {
    char magic[6];
    u16  version;
    u16  dim_x;          // brick dimensions
    u16  dim_y;
    u16  dim_z;
    u16  reserved;
};

struct Brick {
    // palette ids, x->y->z order
    // 8x8x8 size
    u8 materials[512];
};

struct TVOX {
    VoxelFileHeader header;
    u64             palette_uid;
    u32             stored_brick_count;

    BrickEntry grid[header.dim_x * header.dim_y * header.dim_z];
    Brick      bricks[stored_brick_count];
};

TVOX file @ 0x00;
