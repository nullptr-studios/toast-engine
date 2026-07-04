using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Components.Elements;
using Tomlyn.Model;

namespace editor.Editors;

public partial class GenericFieldVM : ObservableObject, IRowSplittable, IRowVisible, IStructRow {

    public static readonly IReadOnlyList<string> AllFieldTypes = [
        "bool", "int", "float", "string", "enum", "node", "asset",
        "vec2", "vec3", "color3", "color4", "object", "array"
    ];

    // restrict to types expressible in plain TOML
    public static readonly IReadOnlyList<string> BasicFieldTypes = [
        "bool", "int", "float", "string", "object", "array"
    ];

    public IReadOnlyList<string> FieldTypes { get; set; } = AllFieldTypes;

    [ObservableProperty] private string m_name         = "";
    [ObservableProperty] private string m_typeKey      = "string";
    [ObservableProperty] private bool   m_nameEditable = true;

    [ObservableProperty] private float  m_floatVal;
    [ObservableProperty] private int    m_intVal;
    [ObservableProperty] private bool   m_boolVal;
    [ObservableProperty] private string m_stringVal = "";
    [ObservableProperty] private string m_refUid    = "";

    [ObservableProperty] private float m_x, m_y, m_z, m_w;

    [ObservableProperty] private double m_minimum = double.NegativeInfinity;
    [ObservableProperty] private double m_maximum = double.PositiveInfinity;

    [ObservableProperty] private string m_arrayElementType = "string";
    [ObservableProperty] private bool   m_childrenLocked;

    // Asset/node subtype constraint (x-toast-asset-type / x-toast-node-type)
    [ObservableProperty] private string m_refType = "";

    public IReadOnlyList<string> Variants { get; set; } = [];
    [ObservableProperty] private bool m_variantVisible = true;

    public bool RowVisible => VariantVisible;

    partial void OnVariantVisibleChanged(bool value) {
        OnPropertyChanged(nameof(RowVisible));
        NotifyDirty?.Invoke();
    }

    // True when children are free-form editable
    public bool ChildrenEditable => !ChildrenLocked;

    partial void OnChildrenLockedChanged(bool value) => OnPropertyChanged(nameof(ChildrenEditable));

    public Func<GenericFieldVM>? ArrayItemFactory { get; set; }

    private IReadOnlyList<string> m_enumAllowedValues = [];
    public IReadOnlyList<string> EnumAllowedValues {
        get => m_enumAllowedValues;
        set {
            m_enumAllowedValues = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasEnumOptions));
            OnPropertyChanged(nameof(IsEnumWithOptions));
            OnPropertyChanged(nameof(IsEnumWithoutOptions));
        }
    }

    public ObservableCollection<GenericFieldVM> Children { get; } = [];

    public Action? NotifyDirty { get; set; }
    public Action<GenericFieldVM>? RemoveCallback { get; set; }

    public bool ShouldSplitRow => NameEditable;

    // IStructRow: true only for a struct (object) array element (no name, not editable, object type)
    public bool IsStructRow => IsArrayItem && IsObject;
    System.Collections.IList IStructRow.StructKeys => Children;

    public bool IsFloat  => TypeKey == "float";
    public bool IsInt    => TypeKey == "int";
    public bool IsBool   => TypeKey == "bool";
    public bool IsString => TypeKey == "string";
    public bool IsNode   => TypeKey == "node";
    public bool IsAsset  => TypeKey == "asset";
    public bool IsVec2   => TypeKey == "vec2";
    public bool IsVec3   => TypeKey == "vec3";
    public bool IsColor3 => TypeKey == "color3";
    public bool IsColor4 => TypeKey == "color4";
    public bool IsObject => TypeKey == "object";
    public bool IsArray  => TypeKey == "array";
    public bool IsEnum   => TypeKey == "enum";

    public bool IsVectorLayout  => TypeKey is "vec2" or "vec3";
    public bool IsScalar        => !IsVectorLayout && !IsObject && !IsArray;
    public bool IsScalarOrVec   => !IsObject && !IsArray;

    public bool HasEnumOptions        => m_enumAllowedValues.Count > 0;
    public bool IsEnumWithOptions     => IsEnum && HasEnumOptions;
    public bool IsEnumWithoutOptions  => IsEnum && !HasEnumOptions;

    public bool IsNamedScalar   => IsScalar && !NameEditable && !string.IsNullOrEmpty(Name);
    public bool IsArrayItem     => !NameEditable && string.IsNullOrEmpty(Name);
    public bool IsNamedField    => !NameEditable && !string.IsNullOrEmpty(Name);
    public bool IsUnnamedScalar => IsScalar && !IsNamedScalar;

    partial void OnTypeKeyChanged(string value) {
        OnPropertyChanged(nameof(IsFloat));
        OnPropertyChanged(nameof(IsInt));
        OnPropertyChanged(nameof(IsBool));
        OnPropertyChanged(nameof(IsString));
        OnPropertyChanged(nameof(IsNode));
        OnPropertyChanged(nameof(IsAsset));
        OnPropertyChanged(nameof(IsVec2));
        OnPropertyChanged(nameof(IsVec3));
        OnPropertyChanged(nameof(IsColor3));
        OnPropertyChanged(nameof(IsColor4));
        OnPropertyChanged(nameof(IsObject));
        OnPropertyChanged(nameof(IsArray));
        OnPropertyChanged(nameof(IsEnum));
        OnPropertyChanged(nameof(IsVectorLayout));
        OnPropertyChanged(nameof(IsScalar));
        OnPropertyChanged(nameof(IsScalarOrVec));
        OnPropertyChanged(nameof(IsEnumWithOptions));
        OnPropertyChanged(nameof(IsEnumWithoutOptions));
        OnPropertyChanged(nameof(IsNamedScalar));
        OnPropertyChanged(nameof(IsUnnamedScalar));
        OnPropertyChanged(nameof(IsStructRow));
        NotifyDirty?.Invoke();
    }

    partial void OnNameChanged(string value) {
        OnPropertyChanged(nameof(IsNamedScalar));
        OnPropertyChanged(nameof(IsArrayItem));
        OnPropertyChanged(nameof(IsNamedField));
        OnPropertyChanged(nameof(IsUnnamedScalar));
        OnPropertyChanged(nameof(IsStructRow));
        NotifyDirty?.Invoke();
    }

    partial void OnNameEditableChanged(bool value) {
        OnPropertyChanged(nameof(ShouldSplitRow));
        OnPropertyChanged(nameof(IsNamedScalar));
        OnPropertyChanged(nameof(IsArrayItem));
        OnPropertyChanged(nameof(IsNamedField));
        OnPropertyChanged(nameof(IsUnnamedScalar));
        OnPropertyChanged(nameof(IsStructRow));
    }

    // Dirty propagation
    partial void OnFloatValChanged(float value)          => NotifyDirty?.Invoke();
    partial void OnIntValChanged(int value)              => NotifyDirty?.Invoke();
    partial void OnBoolValChanged(bool value)            => NotifyDirty?.Invoke();
    partial void OnStringValChanged(string value)        => NotifyDirty?.Invoke();
    partial void OnRefUidChanged(string value)           => NotifyDirty?.Invoke();
    partial void OnXChanged(float value)                 => NotifyDirty?.Invoke();
    partial void OnYChanged(float value)                 => NotifyDirty?.Invoke();
    partial void OnZChanged(float value)                 => NotifyDirty?.Invoke();
    partial void OnWChanged(float value)                 => NotifyDirty?.Invoke();
    partial void OnArrayElementTypeChanged(string value) {
        foreach (var child in Children) child.TypeKey = value;
        NotifyDirty?.Invoke();
    }

    [RelayCommand]
    private void RemoveThis() => RemoveCallback?.Invoke(this);

    [RelayCommand]
    private void AddArrayItem() {
        var child = ArrayItemFactory?.Invoke()
                    ?? new GenericFieldVM { TypeKey = ArrayElementType, NameEditable = false };
        WireChild(child);
        Children.Add(child);
        NotifyDirty?.Invoke();
    }

    [RelayCommand]
    private void AddObjectChild() {
        var child = new GenericFieldVM { Name = "field", TypeKey = "string", NameEditable = true };
        WireChild(child);
        Children.Add(child);
        NotifyDirty?.Invoke();
    }

    private void WireChild(GenericFieldVM child) {
        child.FieldTypes  = FieldTypes;
        child.NotifyDirty = () => NotifyDirty?.Invoke();
        child.Children.CollectionChanged += (_, _) => NotifyDirty?.Invoke();
    }

    public void SetFieldTypesRecursive(IReadOnlyList<string> types) {
        FieldTypes = types;
        foreach (var c in Children) c.SetFieldTypesRecursive(types);
    }

    public static GenericFieldVM FromToml(string name, object tomlValue, string typeHint = "") {
        var vm = new GenericFieldVM { Name = name };
        vm.TypeKey = string.IsNullOrEmpty(typeHint) ? InferType(tomlValue) : typeHint;

        switch (vm.TypeKey) {
            case "float":
                vm.FloatVal = TomlToFloat(tomlValue);
                break;
            case "int":
                vm.IntVal = TomlToInt(tomlValue);
                break;
            case "bool":
                vm.BoolVal = tomlValue is bool b && b;
                break;
            case "string" or "enum":
                vm.StringVal = tomlValue?.ToString() ?? "";
                break;
            case "node" or "asset":
                vm.RefUid = tomlValue?.ToString() ?? "";
                break;
            case "vec2" when tomlValue is TomlArray a2:
                vm.X = TomlToFloat(a2.Count > 0 ? a2[0] : 0f);
                vm.Y = TomlToFloat(a2.Count > 1 ? a2[1] : 0f);
                break;
            case "vec3" or "color3" when tomlValue is TomlArray a3:
                vm.X = TomlToFloat(a3.Count > 0 ? a3[0] : 0f);
                vm.Y = TomlToFloat(a3.Count > 1 ? a3[1] : 0f);
                vm.Z = TomlToFloat(a3.Count > 2 ? a3[2] : 0f);
                break;
            case "color4" when tomlValue is TomlArray a4:
                vm.X = TomlToFloat(a4.Count > 0 ? a4[0] : 0f);
                vm.Y = TomlToFloat(a4.Count > 1 ? a4[1] : 0f);
                vm.Z = TomlToFloat(a4.Count > 2 ? a4[2] : 0f);
                vm.W = TomlToFloat(a4.Count > 3 ? a4[3] : 0f);
                break;
            case "array" when tomlValue is TomlArray arr: {
                var elemType = InferArrayElementType(arr);
                vm.ArrayElementType = elemType;
                foreach (var elem in arr) {
                    var child = FromToml("", elem!, elemType);
                    child.NameEditable = false;
                    vm.Children.Add(child);
                }
                break;
            }
            case "array" when tomlValue is TomlTableArray tableArr: {
                vm.ArrayElementType = "object";
                foreach (var elem in tableArr) {
                    var child = FromToml("", elem, "object");
                    child.NameEditable = false;
                    vm.Children.Add(child);
                }
                break;
            }
            case "object" when tomlValue is TomlTable tbl:
                foreach (var (k, v) in tbl)
                    vm.Children.Add(FromToml(k, v));
                break;
        }

        return vm;
    }

    public object ToTomlValue() => TypeKey switch {
        "float"            => (object)FloatVal,
        "int"              => (long)IntVal,
        "bool"             => (object)BoolVal,
        "string" or "enum" => StringVal,
        "node" or "asset"  => RefUid,
        "vec2"             => new TomlArray { X, Y },
        "vec3" or "color3" => new TomlArray { X, Y, Z },
        "color4"           => new TomlArray { X, Y, Z, W },
        "array"            => BuildChildArray(),
        "object"           => BuildChildTable(),
        _                  => (object)StringVal
    };

    private TomlArray BuildChildArray() {
        var arr = new TomlArray();
        foreach (var c in Children) arr.Add(c.ToTomlValue());
        return arr;
    }

    private TomlTable BuildChildTable() {
        var t = new TomlTable();
        foreach (var c in Children) {
            if (!c.VariantVisible) continue;
            t[c.Name] = c.ToTomlValue();
        }
        return t;
    }

    private static string InferType(object? val) => val switch {
        bool              => "bool",
        long or int       => "int",
        double or float   => "float",
        TomlArray arr     => InferArrayType(arr),
        TomlTableArray    => "array",
        TomlTable         => "object",
        _                 => "string"
    };

    private static string InferArrayType(TomlArray arr) {
        bool allNumbers = arr.Count > 0 && arr.All(e => e is double or float or long or int);
        if (allNumbers) return arr.Count switch {
            2 => "vec2",
            3 => "vec3",
            4 => "color4",
            _ => "array"
        };
        return "array";
    }

    private static string InferArrayElementType(TomlArray arr) {
        if (arr.Count == 0) return "string";
        return arr[0] switch {
            bool      => "bool",
            long      => "int",
            double    => "float",
            TomlTable => "object",
            _         => "string"
        };
    }

    private static float TomlToFloat(object? v) => v switch {
        double d => (float)d,
        float  f => f,
        long   l => (float)l,
        int    i => (float)i,
        _        => 0f
    };

    private static int TomlToInt(object? v) => v switch {
        long   l => (int)l,
        int    i => i,
        double d => (int)d,
        _        => 0
    };
}

public record SchemaFieldDescriptor(
    string Name,
    string TypeKey,
    bool   IsArray,
    string DefaultStr,
    string Description,
    IReadOnlyList<string> EnumOptions,
    double? MinValue,
    double? MaxValue,
    IReadOnlyList<string> Variants,
    string RefType,
    TypeSwitchDescriptor? TypeSwitch
) {
    public SchemaFieldDescriptor(string Name, string TypeKey, bool IsArray, string DefaultStr, string Description)
        : this(Name, TypeKey, IsArray, DefaultStr, Description, [], null, null, [], "", null) { }

    public SchemaFieldDescriptor(string Name, string TypeKey, bool IsArray, string DefaultStr, string Description, IReadOnlyList<string> EnumOptions)
        : this(Name, TypeKey, IsArray, DefaultStr, Description, EnumOptions, null, null, [], "", null) { }

    public SchemaFieldDescriptor(string Name, string TypeKey, bool IsArray, string DefaultStr, string Description, IReadOnlyList<string> EnumOptions, double? MinValue, double? MaxValue)
        : this(Name, TypeKey, IsArray, DefaultStr, Description, EnumOptions, MinValue, MaxValue, [], "", null) { }
}

public record TypeSwitchCase(string TypeKey, string DefaultStr);

public record TypeSwitchDescriptor(string Field, IReadOnlyDictionary<string, TypeSwitchCase> Cases);

public record StructDef(string Discriminator, List<SchemaFieldDescriptor> Fields);
