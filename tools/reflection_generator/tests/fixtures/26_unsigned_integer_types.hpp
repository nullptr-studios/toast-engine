// Various unsigned int types

class [[ToastNode]] UnsignedNode {
public:
    [[Serialize]] uint8_t m_u8;
    [[Serialize]] uint16_t m_u16;
    [[Serialize]] uint32_t m_u32;
    [[Serialize]] uint64_t m_u64;
    [[Serialize]] unsigned int m_uint;
};
