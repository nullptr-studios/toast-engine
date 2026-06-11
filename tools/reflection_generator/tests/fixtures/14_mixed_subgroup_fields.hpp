// Group with both direct and subgroup fields

class [[ToastNode]] MixedSubgroupNode {
public:
    [[Reflect, Group("Data")]] int m_version;
    [[Reflect, Group("Data")]] std::string m_title;
    [[Reflect, Group("Data"), Subgroup("Content")]] std::string m_body;
    [[Reflect, Group("Data"), Subgroup("Content")]] int m_checksum;
    [[Reflect, Group("Data"), Subgroup("Metadata")]] int m_created;
    [[Reflect, Group("Data"), Subgroup("Metadata")]] int m_modified;
};
