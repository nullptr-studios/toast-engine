// [Reflect]               expose in the material editor
// [Range(min, max)]       slider range
// [Color]                 float3 or float4 as a colour
// [Name("Display Name")]  inspector name
// [Group("group name")]   group and one named like a texture lives in the texture TOML table
// [Subgroup("sub name")]  nested group inside a [Group]
// [Unit("m/s")]           unit suffix
// [Default("black")]      unbound Sampler2D fallback "white" "black" or "flat_normal"
// [Linear]                data texture; sRGB input warns

[__AttributeUsage(_AttributeTargets.Var)]
public struct ReflectAttribute {};

[__AttributeUsage(_AttributeTargets.Var)]
public struct RangeAttribute
{
    float min;
    float max;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct ColorAttribute {};

[__AttributeUsage(_AttributeTargets.Var)]
public struct NameAttribute
{
    string name;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct GroupAttribute
{
    string name;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct SubgroupAttribute
{
    string name;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct UnitAttribute
{
    string unit;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct DefaultAttribute
{
    string fallback;
};

[__AttributeUsage(_AttributeTargets.Var)]
public struct LinearAttribute {};
