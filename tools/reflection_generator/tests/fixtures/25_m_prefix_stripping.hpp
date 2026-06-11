// Test m_ prefix stripping for display names

class [[ToastNode]] PrefixNode {
public:
    [[Reflect]] int m_value;
    [[Reflect]] int raw_value;
    [[Reflect, Name("Custom")]] int m_hidden;
};
