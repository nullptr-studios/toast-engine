using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using editor.Components.Modals;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;

namespace editor.Editors;

public partial class SchemaViewModel : Tool {
    [ObservableProperty] private string m_currentUid    = "";
    [ObservableProperty] private string m_currentPath  = "";
    [ObservableProperty] private string m_fileName     = "";
    [ObservableProperty] private bool   m_isDirty;
    [ObservableProperty] private string m_displayTitle = "Schema Editor";

    public event Action<string>? SchemaSaved;

    private bool m_loading;

    public ObservableCollection<SchemaFieldItemVM> Fields      { get; } = [];
    public ObservableCollection<StructTypeVM>      StructTypes { get; } = [];

    public SchemaViewModel() {
        Fields.CollectionChanged      += (_, _) => { if (!m_loading) IsDirty = true; };
        StructTypes.CollectionChanged += (_, _) => { if (!m_loading) { IsDirty = true; NotifyTypesChanged(); } };

        if (Design.IsDesignMode) InitDesignData();
    }

    private void InitDesignData() {
        m_loading = true;
        CurrentPath = "character.schema.json";

        SchemaFieldItemVM F(string name, string type) => new(this) { Name = name, TypeKey = type };

        Fields.Add(F("health",      "int")   );
        Fields.Add(F("speed",       "float") );
        Fields.Add(F("is_flying",   "bool")  );
        Fields.Add(F("display_name","string"));
        Fields.Add(F("spawn_point", "vec2")  );
        Fields.Add(F("velocity",    "vec3")  );
        Fields.Add(F("tint",        "color3"));
        Fields.Add(F("glow",        "color4"));
        Fields.Add(F("target",      "node")  );
        Fields.Add(F("icon",        "asset") );

        var arrayField = F("tags", "string");
        arrayField.IsArray = true;
        Fields.Add(arrayField);

        var enumField = F("faction", "enum");
        enumField.EnumOptions.Add(new StringOptionVM { Value = "ally"    });
        enumField.EnumOptions.Add(new StringOptionVM { Value = "enemy"   });
        enumField.EnumOptions.Add(new StringOptionVM { Value = "neutral" });
        Fields.Add(enumField);

        var statsType = new StructTypeVM(this) { Name = "Stats" };
        statsType.Fields.Add(new SchemaFieldItemVM(this) { Name = "attack",  TypeKey = "int"   });
        statsType.Fields.Add(new SchemaFieldItemVM(this) { Name = "defense", TypeKey = "int"   });
        statsType.Fields.Add(new SchemaFieldItemVM(this) { Name = "crit",    TypeKey = "float" });
        StructTypes.Add(statsType);

        m_loading = false;
        IsDirty   = false;
    }

    public IEnumerable<string> AllTypes =>
        SchemaFieldItemVM.PrimitiveTypes.Concat(StructTypes.Select(s => s.Name));

    public bool IsStructType(string key) => StructTypes.Any(s => s.Name == key);

    public bool HasContent => !string.IsNullOrEmpty(CurrentPath);

    partial void OnCurrentPathChanged(string value) {
        FileName = Path.GetFileName(value);
        OnPropertyChanged(nameof(HasContent));
        UpdateTitle();
    }

    partial void OnIsDirtyChanged(bool value) => UpdateTitle();

    private void UpdateTitle() {
        DisplayTitle = string.IsNullOrEmpty(FileName) ? "Schema Editor"
                     : IsDirty ? $"{FileName} *"
                     : FileName;
    }

    public void NotifyTypesChanged() {
        OnPropertyChanged(nameof(AllTypes));
        foreach (var f in Fields) f.NotifyAvailableTypesChanged();
        foreach (var st in StructTypes)
            foreach (var f in st.Fields) f.NotifyAvailableTypesChanged();
    }

    public void OpenFile(string uid, string virtualPath) {
        m_loading   = true;
        CurrentUid  = uid;
        CurrentPath = virtualPath;
        Fields.Clear();
        StructTypes.Clear();

        var realPath = ProjectContext.Resolve(CurrentPath);
        try {
            if (File.Exists(realPath)) LoadFromJson(File.ReadAllText(realPath));
        } catch { }

        IsDirty   = false;
        m_loading = false;
    }

    private void LoadFromJson(string text) {
        var root = JsonNode.Parse(text)?.AsObject();
        if (root is null) return;

        if (root["definitions"] is JsonObject defs) {
            foreach (var (typeName, typeNode) in defs) {
                var st = new StructTypeVM(this) { Name = typeName };
                if (typeNode?["properties"] is JsonObject stProps)
                    LoadFieldsInto(stProps, st.Fields);
                StructTypes.Add(st);
            }
        }

        if (root["properties"] is JsonObject props)
            LoadFieldsInto(props, Fields);
    }

    private void LoadFieldsInto(JsonObject props, ObservableCollection<SchemaFieldItemVM> target) {
        foreach (var (key, val) in props) {
            var field = new SchemaFieldItemVM(this) { Name = key };

            var xType = val?["x-toast-type"]?.GetValue<string>() ?? "";
            if (xType.EndsWith("[]")) {
                field.IsArray = true;
                field.TypeKey = xType[..^2];
            } else if (!string.IsNullOrEmpty(xType)) {
                field.TypeKey = xType;
            } else {
                field.TypeKey = val?["type"]?.GetValue<string>() switch {
                    "boolean" => "bool",
                    "integer" => "int",
                    "number"  => "float",
                    _         => "string"
                };
            }

            // Restore enum options
            if (field.TypeKey == "enum" && val?["enum"] is JsonArray enumArr) {
                foreach (var opt in enumArr)
                    if (opt?.GetValue<string>() is { } s)
                        field.EnumOptions.Add(new StringOptionVM { Value = s });
            }

            field.Description   = val?["description"]?.GetValue<string>() ?? "";
            var def             = val?["default"];
            field.DefaultString = def is not null ? def.ToJsonString() : "";

            target.Add(field);
        }
    }

    [RelayCommand]
    private void AddField() {
        Fields.Add(new SchemaFieldItemVM(this) { Name = StructTypeVM.NextName(Fields) });
        IsDirty = true;
    }

    public void RemoveStructType(StructTypeVM st) {
        StructTypes.Remove(st);
        IsDirty = true;
        NotifyTypesChanged();
    }

    [RelayCommand]
    private void AddStructType() {
        StructTypes.Add(new StructTypeVM(this));
        IsDirty = true;
        NotifyTypesChanged();
    }

    [RelayCommand]
    private async Task Save() {
        if (string.IsNullOrEmpty(CurrentPath)) return;

        var root = new JsonObject {
            ["$schema"] = "http://json-schema.org/draft-07/schema",
            ["type"]    = "object"
        };

        if (StructTypes.Count > 0) {
            var defs = new JsonObject();
            foreach (var st in StructTypes) {
                var stProps = new JsonObject();
                foreach (var f in st.Fields) stProps[f.Name] = BuildFieldNode(f);
                defs[st.Name] = new JsonObject { ["type"] = "object", ["properties"] = stProps };
            }
            root["definitions"] = defs;
        }

        var props = new JsonObject();
        foreach (var field in Fields) props[field.Name] = BuildFieldNode(field);
        root["properties"] = props;

        var options = new JsonSerializerOptions { WriteIndented = true };
        var text    = root.ToJsonString(options);

        var realPath = ProjectContext.Resolve(CurrentPath);
        await File.WriteAllTextAsync(realPath, text);
        IsDirty = false;
        SchemaSaved?.Invoke(realPath);
    }

    public async Task<bool> ConfirmCloseCurrentAsync() {
        if (!IsDirty || !HasContent) return true;
        if (App.MainWindow is not { } owner) return true;
        var result = await new MessageModal(new ModalConfig(
            Title:   "Unsaved Changes",
            Message: $"Save changes to '{FileName}'?",
            Buttons: ModalButtons.OkNoCancel,
            OkLabel: "Save"
        )).ShowDialog<bool?>(owner);
        if (result is null) return false;
        if (result is true) await Save();
        return true;
    }

    private JsonObject BuildFieldNode(SchemaFieldItemVM field) {
        var typeKey = field.TypeKey;
        var xType   = field.IsArray ? typeKey + "[]" : typeKey;

        var node = new JsonObject { ["x-toast-type"] = xType };

        if (!string.IsNullOrEmpty(field.Description))
            node["description"] = field.Description;

        if (!string.IsNullOrEmpty(field.DefaultString)) {
            try { node["default"] = JsonNode.Parse(field.DefaultString); }
            catch { node["default"] = field.DefaultString; }
        }

        // serialize allowed values
        if (typeKey == "enum") {
            node["type"] = "string";
            var arr = new JsonArray();
            foreach (var opt in field.EnumOptions) arr.Add(JsonValue.Create(opt.Value));
            node["enum"] = arr;
            return node;
        }

        bool isStructRef = IsStructType(typeKey);
        if (isStructRef) {
            if (field.IsArray) {
                node["type"]  = "array";
                node["items"] = new JsonObject { ["$ref"] = $"#/definitions/{typeKey}" };
            } else {
                node["$ref"] = $"#/definitions/{typeKey}";
            }
        } else {
            ApplyJsonSchemaType(node, typeKey, field.IsArray);
        }

        return node;
    }

    private static void ApplyJsonSchemaType(JsonObject node, string typeKey, bool isArray) {
        var (jsonType, minItems, maxItems) = typeKey switch {
            "bool"   => ("boolean", (int?)null, (int?)null),
            "int"    => ("integer", null, null),
            "float"  => ("number",  null, null),
            "string" => ("string",  null, null),
            "node" or "asset" => ("string", null, null),
            "vec2"   => ("array",  (int?)2, (int?)2),
            "vec3"   => ("array",  3,       3),
            "color3" => ("array",  3,       3),
            "color4" => ("array",  4,       4),
            _        => ("string", null, null)
        };

        if (isArray) {
            node["type"]  = "array";
            var items = new JsonObject { ["type"] = jsonType };
            if (minItems.HasValue) { items["minItems"] = minItems; items["maxItems"] = maxItems; }
            node["items"] = items;
        } else {
            node["type"] = jsonType;
            if (minItems.HasValue) { node["minItems"] = minItems; node["maxItems"] = maxItems; }
            if (typeKey is "node" or "asset") node["pattern"] = "^[A-Za-z0-9_-]{11}$";
        }
    }
}
