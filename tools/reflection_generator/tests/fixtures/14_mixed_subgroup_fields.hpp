// Group with both direct and subgroup fields

class [[ToastNode]] MixedSubgroupNode {
public:
    [[Serialize, Group("Data")]] int m_version;
    [[Serialize, Group("Data")]] std::string m_title;
    [[Serialize, Group("Data"), Subgroup("Content")]] std::string m_body;
    [[Serialize, Group("Data"), Subgroup("Content")]] int m_checksum;
    [[Serialize, Group("Data"), Subgroup("Metadata")]] int m_created;
    [[Serialize, Group("Data"), Subgroup("Metadata")]] int m_modified;
};
