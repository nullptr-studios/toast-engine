using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Workspace;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Editors;

public partial class GenericViewModel : Tool, IAutosavable {
	[ObservableProperty] private string m_currentPath = "";
	[ObservableProperty] private string m_currentUid = "";
	[ObservableProperty] private BaseAsset? m_definition;
	[ObservableProperty] private string m_displayTitle = "Data Editor";
	[ObservableProperty] private string m_fileName = "";
	[ObservableProperty] private bool m_isDirty;

	private bool m_loading;
	private string m_prevSchemaUid = "";

	// Materials and material instances generate their schema from shader reflection
	private bool m_dynamicSchema;
	private bool m_dynamicRebuildQueued;
	[ObservableProperty] private string m_schemaLabel = "";
	[ObservableProperty] private bool m_schemaLocked;
	[ObservableProperty] private string m_schemaUid = "";

	public GenericViewModel() {
		Fields.CollectionChanged += (_, _) => {
			if (!m_loading) IsDirty = true;
		};

		if (Design.IsDesignMode) InitDesignData();
	}

	public ObservableCollection<GenericFieldVM> Fields { get; } = [];

	public bool HasContent => !string.IsNullOrEmpty(CurrentPath);
	public bool CanAddFields => !SchemaLocked && string.IsNullOrEmpty(SchemaUid);

	// Exposed for DataTemplate bindings in GenericView.axaml
	public static IReadOnlyList<string> AllFieldTypes => GenericFieldVM.AllFieldTypes;

	public bool IsAutosaveDirty => IsDirty && HasContent;

	public string? AutosaveFileName => HasContent ? CurrentUid + AssetTypeRegistry.GetExtension(CurrentPath) : null;

	public Task WriteAutosaveAsync(string virtualPath) {
		return File.WriteAllTextAsync(ProjectContext.Resolve(virtualPath), SerializeDocument());
	}

	private void InitDesignData() {
		m_loading = true;
		SchemaLocked = false;
		SchemaLabel = "character";
		CurrentPath = "hero.toml";

		void Add(GenericFieldVM f) {
			f.NameEditable = false;
			f.NotifyDirty = () => { };
			Fields.Add(f);
		}

		Add(new GenericFieldVM { Name = "health", TypeKey = "int", IntVal = 100 });
		Add(new GenericFieldVM { Name = "speed", TypeKey = "float", FloatVal = 5.5f });
		Add(new GenericFieldVM { Name = "is_flying", TypeKey = "bool", BoolVal = false });
		Add(new GenericFieldVM { Name = "display_name", TypeKey = "string", StringVal = "Hero" });

		var factionF = new GenericFieldVM { Name = "faction", TypeKey = "enum", StringVal = "ally" };
		factionF.EnumAllowedValues = ["ally", "enemy", "neutral"];
		Add(factionF);

		Add(new GenericFieldVM { Name = "spawn_point", TypeKey = "vec2", X = 12f, Y = -3f });
		Add(new GenericFieldVM { Name = "velocity", TypeKey = "vec3", X = 0.1f, Y = -0.5f, Z = 2f });
		Add(new GenericFieldVM { Name = "tint", TypeKey = "color3", X = 1f, Y = 0.4f, Z = 0f });
		Add(new GenericFieldVM { Name = "glow", TypeKey = "color4", X = 0.8f, Y = 0.2f, Z = 1f, W = 0.5f });

		var statsF = new GenericFieldVM { Name = "stats", TypeKey = "object" };
		statsF.Children.Add(new GenericFieldVM { Name = "attack", TypeKey = "int", IntVal = 15, NameEditable = false });
		statsF.Children.Add(new GenericFieldVM { Name = "defense", TypeKey = "int", IntVal = 8, NameEditable = false });
		statsF.Children.Add(new GenericFieldVM
			{ Name = "crit", TypeKey = "float", FloatVal = 0.25f, NameEditable = false });
		Add(statsF);

		var tagsF = new GenericFieldVM { Name = "tags", TypeKey = "array", ArrayElementType = "string" };
		tagsF.Children.Add(new GenericFieldVM { TypeKey = "string", StringVal = "fighter", NameEditable = false });
		tagsF.Children.Add(new GenericFieldVM { TypeKey = "string", StringVal = "playable", NameEditable = false });
		Add(tagsF);

		m_loading = false;
		IsDirty = false;
	}

	partial void OnCurrentPathChanged(string value) {
		FileName = Path.GetFileName(value);
		OnPropertyChanged(nameof(HasContent));
		OnPropertyChanged(nameof(CanAddFields));
		UpdateTitle();
	}

	partial void OnIsDirtyChanged(bool value) {
		UpdateTitle();
	}

	partial void OnSchemaLockedChanged(bool value) {
		OnPropertyChanged(nameof(CanAddFields));
	}

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
					"Clear Schema",
					"Clearing the schema will erase all field data. Continue?",
					ModalButtons.OkCancel,
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

			SchemaLabel = "(no schema)";
			m_prevSchemaUid = "";
			return;
		}

		// Picking a new schema
		if (Fields.Count > 0 && App.MainWindow is { } owner) {
			var confirmed = await new MessageModal(new ModalConfig(
				"Change Schema",
				"Changing the schema will clear all existing field data. Continue?",
				ModalButtons.OkCancel,
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
		var (descriptors, definitions) = ParseSchema(File.ReadAllText(schemaRealPath));
		LoadSchemaGuided(new TomlTable(), descriptors, definitions);
		m_prevSchemaUid = value;
	}

	public void OpenFile(string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null) {
		m_loading = true;
		CurrentUid = uid;
		CurrentPath = virtualPath;
		Definition = definition;
		Fields.Clear();

		m_dynamicSchema = definition.Type is "material" or "material_instance";
		SchemaLocked = !string.IsNullOrEmpty(definition.SchemaPath) || m_dynamicSchema;

		var realPath = contentSourceRealPath ?? ProjectContext.Resolve(virtualPath);
		SchemaUid = "";
		m_prevSchemaUid = "";
		OnPropertyChanged(nameof(SchemaUid));
		OnPropertyChanged(nameof(CanAddFields));

		if (File.Exists(realPath))
			try {
				var tomlText = File.ReadAllText(realPath);
				var table = TomlSerializer.Deserialize<TomlTable>(tomlText);

				// Restore the saved schema UID for this file
				if (!SchemaLocked && table!.TryGetValue("schema", out var savedUid)) {
					SchemaUid = savedUid?.ToString() ?? "";
					m_prevSchemaUid = SchemaUid;
				}

				OnPropertyChanged(nameof(SchemaUid));
				OnPropertyChanged(nameof(CanAddFields));

				if (m_dynamicSchema) {
					LoadDynamicSchema(table!);
				} else if (SchemaLocked) {
					var schemaPath = ProjectContext.Resolve(definition.SchemaPath);
					SchemaLabel = Path.GetFileNameWithoutExtension(definition.SchemaPath);
					if (File.Exists(schemaPath)) {
						var (descriptors, definitions) = ParseSchema(File.ReadAllText(schemaPath));
						LoadSchemaGuided(table!, descriptors, definitions);
					} else {
						LoadFreeForm(table!);
					}
				} else {
					if (!string.IsNullOrEmpty(SchemaUid) &&
					    AssetDatabase.TryResolve(SchemaUid, out var sVirtPath, out _)) {
						var sRealPath = ProjectContext.Resolve(sVirtPath);
						SchemaLabel = Path.GetFileNameWithoutExtension(sVirtPath);
						if (File.Exists(sRealPath)) {
							var (descriptors, definitions) = ParseSchema(File.ReadAllText(sRealPath));
							LoadSchemaGuided(table!, descriptors, definitions);
						} else {
							LoadFreeForm(table!);
						}
					} else {
						SchemaLabel = "(no schema)";
						LoadFreeForm(table!);
					}
				}
			} catch {
				// ignored
			}

		IsDirty = contentSourceRealPath != null;
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

	private void LoadSchemaGuided(
		TomlTable table, List<SchemaFieldDescriptor> descriptors,
		Dictionary<string, StructDef> definitions) {
		var ctx = new LoadContext(definitions);
		foreach (var desc in descriptors) {
			object? tomlVal = table.TryGetValue(desc.Name, out var v) ? v : null;
			AddField(BuildField(desc, tomlVal, ctx));
		}

		WireTypeSwitches(ctx);
	}

	private GenericFieldVM BuildField(SchemaFieldDescriptor desc, object? tomlVal, LoadContext ctx) {
		bool isStruct = ctx.Definitions.ContainsKey(desc.TypeKey);
		string vmType = isStruct ? "object" : desc.TypeKey;

		GenericFieldVM field;
		if (desc.IsArray) {
			field = new GenericFieldVM {
				Name = desc.Name,
				TypeKey = "array",
				ArrayElementType = vmType
			};
			if (isStruct) {
				// Factory so "Add Item" creates a properly populated struct instance
				var capturedDef = ctx.Definitions[desc.TypeKey];
				field.ArrayItemFactory = () => {
					var item = new GenericFieldVM { TypeKey = "object", NameEditable = false, ChildrenLocked = true };
					LoadStructChildren(item, new TomlTable(), capturedDef, ctx);
					return item;
				};
			}

			IEnumerable? elements = tomlVal switch {
				TomlArray ta => ta,
				TomlTableArray tta => tta,
				_ => null
			};
			if (elements != null) {
				foreach (var elem in elements) {
					GenericFieldVM child;
					if (isStruct) {
						child = new GenericFieldVM { TypeKey = "object", NameEditable = false, ChildrenLocked = true };
						LoadStructChildren(child, elem as TomlTable ?? [], ctx.Definitions[desc.TypeKey], ctx);
					} else {
						child = GenericFieldVM.FromToml("", elem!, vmType);
						child.NameEditable = false;
						SetChildrenNameEditable(child, false);
					}

					if (desc.EnumOptions.Count > 0) child.EnumAllowedValues = desc.EnumOptions;
					if (desc.MinValue.HasValue) child.Minimum = desc.MinValue.Value;
					if (desc.MaxValue.HasValue) child.Maximum = desc.MaxValue.Value;
					child.RefType = desc.RefType;
					field.Children.Add(child);
				}
			}
		} else if (isStruct) {
			field = new GenericFieldVM { Name = desc.Name, TypeKey = "object", ChildrenLocked = true };
			LoadStructChildren(field, tomlVal as TomlTable ?? [], ctx.Definitions[desc.TypeKey], ctx);
		} else if (tomlVal is not null) {
			field = GenericFieldVM.FromToml(desc.Name, tomlVal, desc.TypeKey);
		} else {
			field = new GenericFieldVM { Name = desc.Name, TypeKey = desc.TypeKey };
			ApplyDefault(field, desc.DefaultStr);
		}

		if (!desc.IsArray && desc.EnumOptions.Count > 0)
			field.EnumAllowedValues = desc.EnumOptions;

		if (desc.MinValue.HasValue) field.Minimum = desc.MinValue.Value;
		if (desc.MaxValue.HasValue) field.Maximum = desc.MaxValue.Value;

		field.RefType = desc.RefType;
		field.Variants = desc.Variants;
		field.DisplayName = desc.DisplayName;
		field.NameEditable = false;

		if (desc.TypeSwitch is not null)
			ctx.RegisterTypeSwitch(field, desc, tomlVal is not null);

		return field;
	}

	private void LoadStructChildren(
		GenericFieldVM parent, TomlTable table,
		StructDef structDef, LoadContext ctx) {
		foreach (var desc in structDef.Fields) {
			object? tomlVal = table.TryGetValue(desc.Name, out var v) ? v : null;
			var child = BuildField(desc, tomlVal, ctx);
			child.NotifyDirty = () => parent.NotifyDirty?.Invoke();
			child.Children.CollectionChanged += (_, _) => parent.NotifyDirty?.Invoke();
			parent.Children.Add(child);
		}

		WireVariantVisibility(parent, structDef.Discriminator);
	}

	/// Shows only the fields whose Variants list contains the discriminator's
	/// current value
	private static void WireVariantVisibility(GenericFieldVM parent, string discriminator) {
		if (string.IsNullOrEmpty(discriminator)) return;
		var disc = parent.Children.FirstOrDefault(c => c.Name == discriminator);
		if (disc is null) return;

		void Apply() {
			foreach (var sibling in parent.Children) {
				if (ReferenceEquals(sibling, disc)) continue;
				sibling.VariantVisible = sibling.Variants.Count == 0 || sibling.Variants.Contains(disc.StringVal);
			}
		}

		disc.PropertyChanged += (_, e) => {
			if (e.PropertyName == nameof(GenericFieldVM.StringVal)) Apply();
		};
		Apply();
	}

	private void WireTypeSwitches(LoadContext ctx) {
		foreach (var reg in ctx.PendingTypeSwitches) WireTypeSwitch(reg);
		ctx.PendingTypeSwitches.Clear();
		// items created later through ArrayItemFactory wire up on the spot
		ctx.WireImmediately = WireTypeSwitch;
	}

	private void WireTypeSwitch(TypeSwitchRegistration reg) {
		var sw = reg.Desc.TypeSwitch!;
		var controller = Fields.FirstOrDefault(f => f.Name == sw.Field);
		if (controller is null) return;

		void Apply(bool applyDefault) {
			if (!sw.Cases.TryGetValue(controller.StringVal, out var c)) return;
			reg.Vm.TypeKey = c.TypeKey;
			if (applyDefault) ApplyDefault(reg.Vm, c.DefaultStr);
		}

		controller.PropertyChanged += (_, e) => {
			if (e.PropertyName == nameof(GenericFieldVM.StringVal)) Apply(true);
		};
		Apply(!reg.HadValue);
	}

	private void AddField(GenericFieldVM field) {
		field.NotifyDirty = () => IsDirty = true;
		field.RemoveCallback = f => {
			Fields.Remove(f);
			IsDirty = true;
		};
		field.Children.CollectionChanged += (_, _) => IsDirty = true;
		Fields.Add(field);
	}

	private static void SetChildrenNameEditable(GenericFieldVM vm, bool editable) {
		foreach (var child in vm.Children) {
			child.NameEditable = editable;
			SetChildrenNameEditable(child, editable);
		}
	}

	private static void ApplyDefault(GenericFieldVM field, string defaultStr) {
		if (string.IsNullOrWhiteSpace(defaultStr)) return;
		try {
			var node = JsonNode.Parse(defaultStr);
			switch (field.TypeKey) {
				case "float": field.FloatVal = (float)(node?.GetValue<double>() ?? 0); break;
				case "int": field.IntVal = node?.GetValue<int>() ?? 0; break;
				case "bool": field.BoolVal = node?.GetValue<bool>() ?? false; break;
				case "string" or "enum": field.StringVal = node?.GetValue<string>() ?? ""; break;
				case "node" or "asset": field.RefUid = node?.GetValue<string>() ?? ""; break;
				case "vec2" or "vec3" or "color3" or "color4":
					if (node is JsonArray arr) {
						float At(int i) {
							return arr.Count > i ? (float)(arr[i]?.GetValue<double>() ?? 0) : 0f;
						}

						field.X = At(0);
						field.Y = At(1);
						field.Z = At(2);
						field.W = At(3);
					}

					break;
			}
		} catch { }
	}

	public async Task<bool> ConfirmCloseCurrentAsync() {
		if (!IsDirty || !HasContent) return true;
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig(
			"Unsaved Changes",
			$"Save changes to '{FileName}'?",
			ModalButtons.OkNoCancel,
			OkLabel: "Save"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) await Save();
		else AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
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
		var (descriptors, definitions) = ParseSchema(File.ReadAllText(schemaRealPath));
		LoadSchemaGuided(table, descriptors, definitions);
		m_loading = false;
		if (Fields.Count != prevCount) IsDirty = true;
	}

	/// Builds the schema from shader reflection
	private void LoadDynamicSchema(TomlTable table) {
		string schemaJson;
		if (Definition?.Type == "material_instance") {
			var parentUid = table.TryGetValue("material", out var m) ? m?.ToString() ?? "" : "";
			schemaJson = MaterialSchemaGenerator.BuildForInstance(parentUid);
			SchemaLabel = "(parent material)";
		} else {
			schemaJson = MaterialSchemaGenerator.BuildForShaders(MaterialSchemaGenerator.ReadShaderUids(table));
			SchemaLabel = "(shader reflection)";
		}

		var (descriptors, definitions) = ParseSchema(schemaJson);
		LoadSchemaGuided(table, descriptors, definitions);
		WireDynamicRebuild();
	}

	/// Rebuilds the field list live when the shaders array or the parent changes
	private void WireDynamicRebuild() {
		var triggerName = Definition?.Type == "material_instance" ? "material" : "shaders";
		var trigger = Fields.FirstOrDefault(f => f.Name == triggerName);
		if (trigger is null) return;

		void OnItemChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) {
			if (e.PropertyName == nameof(GenericFieldVM.RefUid)) ScheduleDynamicRebuild();
		}

		if (triggerName == "material") {
			trigger.PropertyChanged += OnItemChanged;
		} else {
			foreach (var child in trigger.Children) child.PropertyChanged += OnItemChanged;
			trigger.Children.CollectionChanged += (_, e) => {
				if (e.NewItems != null)
					foreach (GenericFieldVM child in e.NewItems)
						child.PropertyChanged += OnItemChanged;
				ScheduleDynamicRebuild();
			};
		}
	}

	private void ScheduleDynamicRebuild() {
		if (m_loading || !m_dynamicSchema || m_dynamicRebuildQueued) return;
		m_dynamicRebuildQueued = true;
		Dispatcher.UIThread.Post(() => {
			m_dynamicRebuildQueued = false;
			RebuildDynamicSchema();
		});
	}

	private void RebuildDynamicSchema() {
		if (!m_dynamicSchema || !HasContent) return;

		// Preserve current values across the rebuild
		var table = new TomlTable();
		foreach (var field in Fields) table[field.Name] = field.ToTomlValue();

		m_loading = true;
		Fields.Clear();
		LoadDynamicSchema(table);
		m_loading = false;
		IsDirty = true;
	}

	/// Material instances save only the values differing from their parent material
	private string SerializeInstanceDelta() {
		var table = new TomlTable();
		var parentUid = Fields.FirstOrDefault(f => f.Name == "material")?.RefUid ?? "";
		table["material"] = parentUid;

		var parent = MaterialSchemaGenerator.LoadMaterialTable(parentUid);
		foreach (var field in Fields) {
			if (field.Name == "material") continue;
			var value = field.ToTomlValue();
			object? parentValue = parent != null && parent.TryGetValue(field.Name, out var pv) ? pv : null;

			if (parentValue is null) {
				table[field.Name] = value;
				continue;
			}

			if (value is TomlTable childTable && parentValue is TomlTable parentChild) {
				// Object parameters diff field by field
				var delta = new TomlTable();
				foreach (var (k, v) in childTable)
					if (!parentChild.TryGetValue(k, out var pcv) || !TomlValuesEqual(v, pcv))
						delta[k] = v;
				if (delta.Count > 0) table[field.Name] = delta;
			} else if (!TomlValuesEqual(value, parentValue)) {
				table[field.Name] = value;
			}
		}

		return TomlSerializer.Serialize(table);
	}

	private static bool TomlValuesEqual(object? a, object? b) {
		if (a is null || b is null) return a is null && b is null;

		if (a is TomlArray arrA && b is TomlArray arrB) {
			if (arrA.Count != arrB.Count) return false;
			for (var i = 0; i < arrA.Count; i++)
				if (!TomlValuesEqual(arrA[i], arrB[i]))
					return false;
			return true;
		}

		if (a is TomlTable tblA && b is TomlTable tblB) {
			if (tblA.Count != tblB.Count) return false;
			foreach (var (k, v) in tblA) {
				if (!tblB.TryGetValue(k, out var other) || !TomlValuesEqual(v, other)) return false;
			}

			return true;
		}

		// Numbers compare across integer/floating representations
		if (IsNumber(a) && IsNumber(b)) return Math.Abs(ToDouble(a) - ToDouble(b)) < 1e-6;

		return a.Equals(b);

		static bool IsNumber(object o) {
			return o is sbyte or byte or short or ushort or int or uint or long or ulong or float or double or decimal;
		}

		static double ToDouble(object o) {
			return Convert.ToDouble(o);
		}
	}

	[RelayCommand]
	private void AddField() {
		if (SchemaLocked) return;
		var field = new GenericFieldVM {
			Name = "new_field",
			TypeKey = "string",
			FieldTypes = GenericFieldVM.BasicFieldTypes
		};
		AddField(field);
		IsDirty = true;
	}

	[RelayCommand]
	private async Task Save() {
		if (string.IsNullOrEmpty(CurrentPath)) return;

		var realPath = ProjectContext.Resolve(CurrentPath);
		await File.WriteAllTextAsync(realPath, SerializeDocument());

		if (Definition is ProjectSettingsAsset) {
			ProjectContext.ReloadProjectSettings();
		} else {
			MetaFile.Touch(CurrentPath);
			AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		}

		IsDirty = false;
	}

	private string SerializeDocument() {
		if (m_dynamicSchema && Definition?.Type == "material_instance") return SerializeInstanceDelta();

		var table = new TomlTable();

		// Preserve schema UID reference
		if (!SchemaLocked && !string.IsNullOrEmpty(SchemaUid))
			table["schema"] = SchemaUid;
		else if (!SchemaLocked) {
			// Check if the original file had a schema key and preserve it
			var realPath = ProjectContext.Resolve(CurrentPath);
			if (File.Exists(realPath)) {
				try {
					var existing = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(realPath));
					if (existing!.TryGetValue("schema", out var s)) table["schema"] = s;
				} catch {
					// ignored
				}
			}
		}

		foreach (var field in Fields)
			table[field.Name] = field.ToTomlValue();

		return TomlSerializer.Serialize(table);
	}

	private static (List<SchemaFieldDescriptor> Fields, Dictionary<string, StructDef> Defs)
		ParseSchema(string jsonText) {
		var fields = new List<SchemaFieldDescriptor>();
		var defs = new Dictionary<string, StructDef>();
		try {
			var root = JsonNode.Parse(jsonText)?.AsObject();
			if (root is null) return (fields, defs);

			if (root["definitions"] is JsonObject defsNode)
				foreach (var (typeName, typeNode) in defsNode)
					if (typeNode?["properties"] is JsonObject defProps)
						defs[typeName] = new StructDef(
							typeNode["x-toast-discriminator"]?.GetValue<string>() ?? "",
							ParseProperties(defProps));

			if (root["properties"] is JsonObject props)
				fields = ParseProperties(props);
		} catch { }

		return (fields, defs);
	}

	private static List<SchemaFieldDescriptor> ParseProperties(JsonObject props) {
		var result = new List<SchemaFieldDescriptor>();
		foreach (var (key, val) in props) {
			var xType = val?["x-toast-type"]?.GetValue<string>() ?? "";
			bool isArray = xType.EndsWith("[]");
			string typeKey = isArray ? xType[..^2] : xType;

			var typeSwitch = ParseTypeSwitch(val?["x-toast-type-switch"]);

			if (typeSwitch is not null) {
				typeKey = "";
				isArray = false;
			} else if (string.IsNullOrEmpty(typeKey) && val?["type"]?.GetValue<string>() == "array") {
				isArray = true;
				typeKey = val["items"] is JsonObject items ? ItemsElementType(items) : "string";
			}

			if (string.IsNullOrEmpty(typeKey) && typeSwitch is null) typeKey = "string";

			var enumOptions = new List<string>();
			if (typeKey == "enum" && val?["enum"] is JsonArray enumArr)
				foreach (var opt in enumArr)
					if (opt?.GetValue<string>() is { } s)
						enumOptions.Add(s);

			double? minValue = null, maxValue = null;
			if (val?["minimum"] is JsonValue minNode && minNode.TryGetValue<double>(out var minD)) minValue = minD;
			else if (val?["exclusiveMinimum"] is JsonValue exMinNode && exMinNode.TryGetValue<double>(out var exMinD))
				minValue = exMinD;
			if (val?["maximum"] is JsonValue maxNode && maxNode.TryGetValue<double>(out var maxD)) maxValue = maxD;
			else if (val?["exclusiveMaximum"] is JsonValue exMaxNode && exMaxNode.TryGetValue<double>(out var exMaxD))
				maxValue = exMaxD;

			var variants = new List<string>();
			if (val?["x-toast-variants"] is JsonArray varArr)
				foreach (var opt in varArr)
					if (opt?.GetValue<string>() is { } s)
						variants.Add(s);

			var refType = val?["x-toast-asset-type"]?.GetValue<string>()
				?? val?["x-toast-node-type"]?.GetValue<string>()
				?? "";

			result.Add(new SchemaFieldDescriptor(
				key,
				typeKey,
				isArray,
				val?["default"]?.ToJsonString() ?? "",
				val?["description"]?.GetValue<string>() ?? "",
				enumOptions,
				minValue,
				maxValue,
				variants,
				refType,
				typeSwitch
			) { DisplayName = val?["x-toast-display-name"]?.GetValue<string>() ?? "" });
		}

		return result;
	}

	private static string ItemsElementType(JsonObject items) {
		if (items["x-toast-type"]?.GetValue<string>() is { Length: > 0 } xt) return xt;
		if (items["$ref"]?.GetValue<string>() is { } refStr)
			return refStr[(refStr.LastIndexOf('/') + 1)..];
		return items["type"]?.GetValue<string>() switch {
			"boolean" => "bool",
			"integer" => "int",
			"number" => "float",
			"object" => "object",
			_ => "string"
		};
	}

	private static TypeSwitchDescriptor? ParseTypeSwitch(JsonNode? node) {
		if (node is not JsonObject sw) return null;
		if (sw["field"]?.GetValue<string>() is not { Length: > 0 } field) return null;
		if (sw["cases"] is not JsonObject casesNode) return null;

		var cases = new Dictionary<string, TypeSwitchCase>();
		foreach (var (caseKey, caseVal) in casesNode) {
			if (caseVal is not JsonObject caseObj) continue;
			cases[caseKey] = new TypeSwitchCase(
				caseObj["type"]?.GetValue<string>() ?? "string",
				caseObj["default"]?.ToJsonString() ?? "");
		}

		return cases.Count > 0 ? new TypeSwitchDescriptor(field, cases) : null;
	}

	private sealed class LoadContext(Dictionary<string, StructDef> definitions) {
		public Dictionary<string, StructDef> Definitions { get; } = definitions;
		public List<TypeSwitchRegistration> PendingTypeSwitches { get; } = [];
		public Action<TypeSwitchRegistration>? WireImmediately { get; set; }

		public void RegisterTypeSwitch(GenericFieldVM vm, SchemaFieldDescriptor desc, bool hadValue) {
			var reg = new TypeSwitchRegistration(vm, desc, hadValue);
			if (WireImmediately is not null) WireImmediately(reg);
			else PendingTypeSwitches.Add(reg);
		}
	}

	private sealed record TypeSwitchRegistration(GenericFieldVM Vm, SchemaFieldDescriptor Desc, bool HadValue);
}
