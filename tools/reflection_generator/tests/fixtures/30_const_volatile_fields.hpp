// const/volatile qualifiers on fields

class [[ToastNode]] QualifiersNode {
public:
    [[Serialize]] const int m_const_val;
    [[Serialize]] volatile int m_volatile_val;
    [[Serialize]] const volatile int m_both;
};
