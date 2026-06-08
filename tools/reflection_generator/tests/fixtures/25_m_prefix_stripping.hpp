// Test m_ prefix stripping for display names

class [[ToastNode]] PrefixNode {
public:
    [[Serialize]] int m_value;
    [[Serialize]] int raw_value;
    [[Serialize, Name("Custom")]] int m_hidden;
};
