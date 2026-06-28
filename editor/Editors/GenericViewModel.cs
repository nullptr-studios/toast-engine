using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using Avalonia.Controls;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using Tomlyn;
using Tomlyn.Model;
using Tomlyn.Serialization;

namespace editor.Editors;

public partial class GenericViewModel : Tool {
    [ObservableProperty] private string     m_currentUid   = "";
    [ObservableProperty] private string     m_currentPath  = "";
    [ObservableProperty] private string     m_fileName     = "";
    [ObservableProperty] private BaseAsset? m_definition;
    [ObservableProperty] private bool       m_isDirty;
    [ObservableProperty] private bool       m_schemaLocked;
    [ObservableProperty] private string     m_schemaLabel  = "";
    [ObservableProperty] private string     m_schemaUid    = "";
    [ObservableProperty] private string     m_displayTitle = "Data Editor";

    public ObservableCollection<GenericFieldVM> Fields { get; } = [];

    private bool   m_loading;
    private string m_prevSchemaUid = "";

    public GenericViewModel() {
        Fields.CollectionChanged += (_, _) => {
            if (!m_loading) IsDirty = true;
        };

        if (Design.IsDesignMode) InitDesignData();
    }

    private void InitDesignData() {
        m_loading    = true;
        SchemaLocked = true;
        SchemaLabel  = "character";
        CurrentPath  = "hero.toml";

        void Add(GenericFieldVM f) {
            f.NameEditable = false;
            f.NotifyDirty  = () => { };
            Fields.Add(f);
        }

        Add(new GenericFieldVM { Name = "health",       TypeKey = "int",    IntVal   = 100       });
        Add(new GenericFieldVM { Name = "speed",        TypeKey = "float",  FloatVal = 5.5f      });
        Add(new GenericFieldVM { Name = "is_flying",    TypeKey = "bool",   BoolVal  = false     });
        Add(new GenericFieldVM { Name = "display_name", TypeKey = "string", StringVal= "Hero"    });

        var factionF = new GenericFieldVM { Name = "faction", TypeKey = "enum", StringVal = "ally" };
        factionF.EnumAllowedValues = ["ally", "enemy", "neutral"];
        Add(factionF);

        Add(new GenericFieldVM { Name = "spawn_point", TypeKey = "vec2",   X = 12f,  Y = -3f         });
        Add(new GenericFieldVM { Name = "velocity",    TypeKey = "vec3",   X = 0.1f, Y = -0.5f, Z = 2f });
        Add(new GenericFieldVM { Name = "tint",        TypeKey = "color3", X = 1f,   Y = 0.4f,  Z = 0f });
        Add(new GenericFieldVM { Name = "glow",        TypeKey = "color4", X = 0.8f, Y = 0.2f,  Z = 1f, W = 0.5f });

        var statsF = new GenericFieldVM { Name = "stats", TypeKey = "object" };
        statsF.Children.Add(new GenericFieldVM { Name = "attack",  TypeKey = "int",   IntVal = 15, NameEditable = false });
        statsF.Children.Add(new GenericFieldVM { Name = "defense", TypeKey = "int",   IntVal = 8,  NameEditable = false });
        statsF.Children.Add(new GenericFieldVM { Name = "crit",    TypeKey = "float", FloatVal = 0.25f, NameEditable = false });
        Add(statsF);

        var tagsF = new GenericFieldVM { Name = "tags", TypeKey = "array", ArrayElementType = "string" };
        tagsF.Children.Add(new GenericFieldVM { TypeKey = "string", StringVal = "fighter",  NameEditable = false });
        tagsF.Children.Add(new GenericFieldVM { TypeKey = "string", StringVal = "playable",  NameEditable = false });
        Add(tagsF);

        m_loading = false;
        IsDirty   = false;
    }

    public bool HasContent   => !string.IsNullOrEmpty(CurrentPath);
    public bool CanAddFields => !SchemaLocked && string.IsNullOrEmpty(SchemaUid);

    // Exposed for DataTemplate bindings in GenericView.axaml
    public static IReadOnlyList<string> AllFieldTypes => GenericFieldVM.AllFieldTypes;

    partial void OnCurrentPathChanged(string value) {
        FileName = Path.GetFileName(value);
        OnPropertyChanged(nameof(HasContent));
        OnPropertyChanged(nameof(CanAddFields));
        UpdateTitle();
    }

    partial void OnIsDirtyChanged(bool value) => UpdateTitle();

    partial void OnSchemaLockedChanged(bool value) => OnPropertyChanged(nameof(CanAddFields));

    private void UpdateTitle() {
        DisplayTitle = string.IsNullOrEmpty(FileName) ? "Data Editor"
                     : IsDirty ? $"{FileName} *"
                     : FileName;
    }

    async partial void OnSchemaUidChanged(string value) {
        if (SchemaLocked) return;
        OnPropertyChanged(nameof(CanAddFields));

        if (string.IsNullOrEmpty(value)) {
            // Clearing the schema
            if (Fields.Count > 0 && App.MainWindow is { } clearWin) {
                var ok = await new MessageModal(new ModalConfig(
                    Title:   "Clear Schema",
                    Message: "Clearing the schema will erase all field data. Continue?",
                    Buttons: ModalButtons.OkCancel,
                    OkLabel: "Clear"
                )).ShowDialog<bool?>(clearWin);
                if (ok is not true) {
                    // Roll back
                    m_schemaUid = m_prevSchemaUid;
                    OnPropertyChanged(nameof(SchemaUid));
                    OnPropertyChanged(nameof(CanAddFields));
                    return;
                }
                Fields.Clear();
                IsDirty = true;
            }
            SchemaLabel    = "(no schema)";
            m_prevSchemaUid = "";
            return;
        }

        // Picking a new schema
        if (Fields.Count > 0 && App.MainWindow is { } owner) {
            var confirmed = await new MessageModal(new ModalConfig(
                Title:   "Change Schema",
                Message: "Changing the schema will clear all existing field data. Continue?",
                Buttons: ModalButtons.OkCancel,
                OkLabel: "Change Schema"
            )).ShowDialog<bool?>(owner);
            if (confirmed is not true) {
                m_schemaUid = "";
                OnPropertyChanged(nameof(SchemaUid));
                OnPropertyChanged(nameof(CanAddFields));
                return;
            }
        }

        Fields.Clear();
        IsDirty = true;

        if (!AssetDatabase.TryResolve(value, out var schemaVirtualPath, out _)) return;
        var schemaRealPath = ProjectContext.Resolve(schemaVirtualPath);
        if (!File.Exists(schemaRealPath)) return;

        SchemaLabel = Path.GetFileNameWithoutExtension(schemaVirtualPath);
        var descriptors = ParseSchemaProperties(File.ReadAllText(schemaRealPath));
        LoadSchemaGuided(new TomlTable(), descriptors);
        m_prevSchemaUid = value;
    }

    public void OpenFile(string uid, string virtualPath, BaseAsset definition) {
        m_loading   = true;
        CurrentUid  = uid;
        CurrentPath = virtualPath;
        Definition  = definition;
        Fields.Clear();

        SchemaLocked = !string.IsNullOrEmpty(definition.SchemaPath);

        var realPath = ProjectContext.Resolve(virtualPath);
        // Always reset schema UID before loading
        m_schemaUid     = "";
        m_prevSchemaUid = "";
        OnPropertyChanged(nameof(SchemaUid));
        OnPropertyChanged(nameof(CanAddFields));

        if (File.Exists(realPath)) try {
            var tomlText = File.ReadAllText(realPath);
            var table    = TomlSerializer.Deserialize<TomlTable>(tomlText);

            // Restore the saved schema UID for this file
            if (!SchemaLocked && table.TryGetValue("schema", out var savedUid)) {
                m_schemaUid     = savedUid?.ToString() ?? "";
                m_prevSchemaUid = m_schemaUid;
            }
            OnPropertyChanged(nameof(SchemaUid));
            OnPropertyChanged(nameof(CanAddFields));

            if (SchemaLocked) {
                var schemaPath = ProjectContext.Resolve(definition.SchemaPath);
                SchemaLabel    = Path.GetFileNameWithoutExtension(definition.SchemaPath);
                if (File.Exists(schemaPath)) {
                    var descriptors = ParseSchemaProperties(File.ReadAllText(schemaPath));
                    LoadSchemaGuided(table, descriptors);
                } else {
                    LoadFreeForm(table);
                }
            } else {
                if (!string.IsNullOrEmpty(m_schemaUid) &&
                    AssetDatabase.TryResolve(m_schemaUid, out var sVirtPath, out _)) {
                    var sRealPath = ProjectContext.Resolve(sVirtPath);
                    SchemaLabel = Path.GetFileNameWithoutExtension(sVirtPath);
                    if (File.Exists(sRealPath)) {
                        var descriptors = ParseSchemaProperties(File.ReadAllText(sRealPath));
                        LoadSchemaGuided(table, descriptors);
                    } else {
                        LoadFreeForm(table);
                    }
                } else {
                    SchemaLabel = "(no schema)";
                    LoadFreeForm(table);
                }
            }
        } catch { }

        IsDirty   = false;
        m_loading = false;
    }

    private void LoadFreeForm(TomlTable table) {
        foreach (var (key, val) in table) {
            if (key == "schema") continue;
            var field = GenericFieldVM.FromToml(key, val);
            field.SetFieldTypesRecursive(GenericFieldVM.BasicFieldTypes);
            AddField(field);
        }
    }

    private void LoadSchemaGuided(TomlTable table, List<SchemaFieldDescriptor> descriptors) {
        foreach (var desc in descriptors) {
            object? tomlVal = table.TryGetValue(desc.Name, out var v) ? v : null;

            GenericFieldVM field;
            if (desc.IsArray) {
                field = new GenericFieldVM {
                    Name             = desc.Name,
                    TypeKey          = "array",
                    ArrayElementType = desc.TypeKey
                };
                if (tomlVal is TomlArray arr) {
                    foreach (var elem in arr) {
                        var child = GenericFieldVM.FromToml("", elem!, desc.TypeKey);
                        child.NameEditable = false;
                        if (desc.EnumOptions.Count > 0) child.EnumAllowedValues = desc.EnumOptions;
                        field.Children.Add(child);
                    }
                }
            } else if (tomlVal is not null) {
                field = GenericFieldVM.FromToml(desc.Name, tomlVal, desc.TypeKey);
            } else {
                field = new GenericFieldVM { Name = desc.Name, TypeKey = desc.TypeKey };
                ApplyDefault(field, desc.DefaultStr);
            }

            if (!desc.IsArray && desc.EnumOptions.Count > 0)
                field.EnumAllowedValues = desc.EnumOptions;

            field.NameEditable = false;
            AddField(field);
        }
    }

    private void AddField(GenericFieldVM field) {
        field.NotifyDirty     = () => IsDirty = true;
        field.RemoveCallback  = f => { Fields.Remove(f); IsDirty = true; };
        field.Children.CollectionChanged += (_, _) => IsDirty = true;
        Fields.Add(field);
    }

    private static void ApplyDefault(GenericFieldVM field, string defaultStr) {
        if (string.IsNullOrWhiteSpace(defaultStr)) return;
        try {
            var node = JsonNode.Parse(defaultStr);
            switch (field.TypeKey) {
                case "float":  field.FloatVal  = (float)(node?.GetValue<double>() ?? 0); break;
                case "int":    field.IntVal    = node?.GetValue<int>()    ?? 0;          break;
                case "bool":   field.BoolVal   = node?.GetValue<bool>()   ?? false;      break;
                case "string": field.StringVal = node?.GetValue<string>() ?? "";         break;
            }
        } catch { }
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

    public void RefreshFromSchema(string schemaRealPath) {
        if (!HasContent) return;

        // Determine which schema path this file references
        string? mySchemaPath = null;
        if (SchemaLocked && Definition is { SchemaPath: { } sp } && !string.IsNullOrEmpty(sp))
            mySchemaPath = ProjectContext.Resolve(sp);
        else if (!string.IsNullOrEmpty(SchemaUid) &&
                 AssetDatabase.TryResolve(SchemaUid, out var sVirt, out _))
            mySchemaPath = ProjectContext.Resolve(sVirt);

        if (mySchemaPath is null ||
            !string.Equals(mySchemaPath, schemaRealPath, StringComparison.OrdinalIgnoreCase))
            return;

        // Serialize current values, then reload with updated schema
        var table = new TomlTable();
        foreach (var field in Fields) table[field.Name] = field.ToTomlValue();

        int prevCount = Fields.Count;
        m_loading = true;
        Fields.Clear();
        var descriptors = ParseSchemaProperties(File.ReadAllText(schemaRealPath));
        LoadSchemaGuided(table, descriptors);
        m_loading = false;
        if (Fields.Count != prevCount) IsDirty = true;
    }

    [RelayCommand]
    private void AddField() {
        if (SchemaLocked) return;
        var field = new GenericFieldVM {
            Name      = "new_field",
            TypeKey   = "string",
            FieldTypes = GenericFieldVM.BasicFieldTypes
        };
        AddField(field);
        IsDirty = true;
    }

    [RelayCommand]
    private async Task Save() {
        if (string.IsNullOrEmpty(CurrentPath)) return;

        var table = new TomlTable();

        // Preserve schema UID reference in free-form mode
        if (!SchemaLocked && !string.IsNullOrEmpty(SchemaUid))
            table["schema"] = SchemaUid;
        else if (!SchemaLocked) {
            // Check if the original file had a schema key and preserve it
            var realPath2 = ProjectContext.Resolve(CurrentPath);
            if (File.Exists(realPath2)) {
                try {
                    var existing = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(realPath2));
                    if (existing.TryGetValue("schema", out var s)) table["schema"] = s;
                } catch { }
            }
        }

        foreach (var field in Fields)
            table[field.Name] = field.ToTomlValue();

        var text     = TomlSerializer.Serialize(table);
        var realPath = ProjectContext.Resolve(CurrentPath);
        await File.WriteAllTextAsync(realPath, text);
        IsDirty = false;
    }

    private static List<SchemaFieldDescriptor> ParseSchemaProperties(string jsonText) {
        var result = new List<SchemaFieldDescriptor>();
        try {
            var root = JsonNode.Parse(jsonText)?.AsObject();
            if (root?["properties"] is not JsonObject props) return result;

            foreach (var (key, val) in props) {
                var xType = val?["x-toast-type"]?.GetValue<string>() ?? "";
                bool isArray = xType.EndsWith("[]");
                string typeKey = isArray ? xType[..^2] : xType;
                if (string.IsNullOrEmpty(typeKey)) typeKey = "string";

                // Read enum options if present
                var enumOptions = new List<string>();
                if (typeKey == "enum" && val?["enum"] is JsonArray enumArr) {
                    foreach (var opt in enumArr)
                        if (opt?.GetValue<string>() is { } s) enumOptions.Add(s);
                }

                result.Add(new SchemaFieldDescriptor(
                    Name:        key,
                    TypeKey:     typeKey,
                    IsArray:     isArray,
                    DefaultStr:  val?["default"]?.ToJsonString() ?? "",
                    Description: val?["description"]?.GetValue<string>() ?? "",
                    EnumOptions: enumOptions
                ));
            }
        } catch { }
        return result;
    }
}
