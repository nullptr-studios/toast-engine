// Fields ONLY in subgroups, no direct group fields

class [[ToastNode]] SubgroupOnlyNode {
public:
    [[Serialize, Group("Settings"), Subgroup("Audio")]] float m_volume;
    [[Serialize, Group("Settings"), Subgroup("Audio")]] float m_pitch;
    [[Serialize, Group("Settings"), Subgroup("Video")]] int m_width;
    [[Serialize, Group("Settings"), Subgroup("Video")]] int m_height;
};
