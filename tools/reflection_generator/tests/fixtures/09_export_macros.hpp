// Class with export macros (stripped during parsing)
namespace toast {

class [[ToastNode]] TOAST_API ExportedNode {
public:
    [[Serialize]] int m_value;
};
}
