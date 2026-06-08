// Various signed int types

class [[ToastNode]] SignedNode {
public:
    [[Serialize]] int8_t m_s8;
    [[Serialize]] int16_t m_s16;
    [[Serialize]] int32_t m_s32;
    [[Serialize]] int64_t m_s64;
    [[Serialize]] signed int m_sint;
};
