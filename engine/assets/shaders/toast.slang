// Toast inspector attributes
//
// Import this module (`import toast;`) to tag material parameters for the editor
//
// Only parameters marked [Reflect] show up in the material inspector; everything
// else is engine-owned data (matrices, light info...)
//
//   [Reflect]                  expose this parameter in the material editor
//   [Range(min, max)]          clamp/slider range for numeric parameters
//   [Color]                    treat a float3/float4 as a color
//   [Name("Display Name")]     override the inspector display name
//   [Group("group name")]      group parameters together; when the group name matches
//                              a texture's name (or its [Name]) the parameters live in
//                              that texture's TOML table
//   [Subgroup("sub name")]     nested grouping inside a [Group]
//   [Unit("m/s")]              display unit suffix

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
