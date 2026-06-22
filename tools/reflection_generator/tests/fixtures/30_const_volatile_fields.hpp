// const/volatile qualifiers on fields

class [[ToastNode]] QualifiersNode {
public:
    [[Reflect]] const int m_const_val;
    [[Reflect]] volatile int m_volatile_val;
    [[Reflect]] const volatile int m_both;
};
