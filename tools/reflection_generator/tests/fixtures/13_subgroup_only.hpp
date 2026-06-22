// Fields ONLY in subgroups, no direct group fields

class [[ToastNode]] SubgroupOnlyNode {
public:
    [[Reflect, Group("Settings"), Subgroup("Audio")]] float m_volume;
    [[Reflect, Group("Settings"), Subgroup("Audio")]] float m_pitch;
    [[Reflect, Group("Settings"), Subgroup("Video")]] int m_width;
    [[Reflect, Group("Settings"), Subgroup("Video")]] int m_height;
};
