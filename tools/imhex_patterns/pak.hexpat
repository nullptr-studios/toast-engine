#pragma endian little

bitfield EntryFlags {
    compressed : 1;
    padding    : 7;
};

struct FileEntry {
    u64          hash;
    u32          path_len;
    char         path[path_len];
    u64          offset;
    u64          orig_size;
    u64          stored_size;
    EntryFlags   flags;
};

struct FileTable {
    u32       file_count;
    FileEntry entries[file_count];
};

struct PackHeader {
    char      magic[6];           // "PACK\0\0"
    u16       version;            // 2
    u32       file_count;
    u64       file_table_offset;
};

struct PACK {
    PackHeader header;
    FileTable  table @ header.file_table_offset;
};

PACK file @ 0x00;
