using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Assets.Types;
using editor.Components.Elements;
using editor.Engine;

namespace editor.Editors;

public partial class StringOptionVM : ObservableObject {
	[ObservableProperty] private string m_value = "";
}

public partial class SchemaFieldItemVM : ObservableObject, IRowSplittable {
	public static readonly IReadOnlyList<string> PrimitiveTypes = [
		"bool", "int", "float", "string", "enum", "node", "asset", "vec2", "vec3", "color3", "color4"
	];

	private readonly SchemaViewModel m_owner;

	private float m_defX, m_defY, m_defZ, m_defW;
	[ObservableProperty] private string m_defaultString = "";
	[ObservableProperty] private string m_description = "";
	[ObservableProperty] private bool m_isArray;
	[ObservableProperty] private string m_maxString = "";
	[ObservableProperty] private string m_minString = "";

	[ObservableProperty] private string m_name = "field";

	[ObservableProperty] private string m_refTypeString = "";
	[ObservableProperty] private string m_typeKey = "float";

	public SchemaFieldItemVM(SchemaViewModel owner) {
		m_owner = owner;
		EnumOptions.CollectionChanged += OnEnumOptionsChanged;
		Variants.CollectionChanged += (_, _) => m_owner.IsDirty = true;
	}

	public string TypeSwitchJson { get; set; } = "";

	public ObservableCollection<StringOptionVM> EnumOptions { get; } = [];

	public ObservableCollection<StringOptionVM> Variants { get; } = [];

	public IEnumerable<string> AssetTypeOptions => AssetTypeRegistry.All.Select(a => a.Type).OrderBy(t => t);

	public IEnumerable<string> NodeTypeOptions =>
		ReflectionDatabase.Nodes?.Keys.OrderBy(n => n) ?? Enumerable.Empty<string>();

	public bool IsEnum => TypeKey == "enum";
	public bool IsBoolType => TypeKey == "bool";
	public bool IsIntType => TypeKey == "int";
	public bool IsFloatType => TypeKey == "float";
	public bool IsStringType => TypeKey == "string";
	public bool IsNodeType => TypeKey == "node";
	public bool IsAssetType => TypeKey == "asset";
	public bool IsVec2Type => TypeKey == "vec2";
	public bool IsVec3Type => TypeKey == "vec3";
	public bool IsColor3Type => TypeKey == "color3";
	public bool IsColor4Type => TypeKey == "color4";

	public bool HasTypedDefault =>
		!IsArray && (IsBoolType || IsIntType || IsFloatType || IsStringType ||
			IsEnum || IsNodeType || IsAssetType || IsColor3Type || IsColor4Type);

	public bool HasMinMax => (IsIntType || IsFloatType || IsVec2Type || IsVec3Type) && !IsArray;

	public bool DefaultBool {
		get => DefaultString == "true";
		set => DefaultString = value ? "true" : "false";
	}

	public float DefaultFloat {
		get => float.TryParse(DefaultString, NumberStyles.Float, CultureInfo.InvariantCulture, out var f) ? f : 0f;
		set => DefaultString = value.ToString(CultureInfo.InvariantCulture);
	}

	public int DefaultInt {
		get => int.TryParse(DefaultString, out var i) ? i : 0;
		set => DefaultString = value.ToString();
	}

	public float DefaultX {
		get => m_defX;
		set {
			m_defX = value;
			SyncVecDefault();
		}
	}

	public float DefaultY {
		get => m_defY;
		set {
			m_defY = value;
			SyncVecDefault();
		}
	}

	public float DefaultZ {
		get => m_defZ;
		set {
			m_defZ = value;
			SyncVecDefault();
		}
	}

	public float DefaultW {
		get => m_defW;
		set {
			m_defW = value;
			SyncVecDefault();
		}
	}

	public IEnumerable<string> EnumOptionsStrings => EnumOptions.Select(o => o.Value);

	public IEnumerable<string> AvailableTypes => m_owner.AllTypes;
	public bool ShouldSplitRow => true;

	private void SyncVecDefault() {
		DefaultString = TypeKey switch {
			"vec2" =>
				$"[{m_defX.ToString(CultureInfo.InvariantCulture)}, {m_defY.ToString(CultureInfo.InvariantCulture)}]",
			"vec3" or "color3" =>
				$"[{m_defX.ToString(CultureInfo.InvariantCulture)}, {m_defY.ToString(CultureInfo.InvariantCulture)}, {m_defZ.ToString(CultureInfo.InvariantCulture)}]",
			"color4" =>
				$"[{m_defX.ToString(CultureInfo.InvariantCulture)}, {m_defY.ToString(CultureInfo.InvariantCulture)}, {m_defZ.ToString(CultureInfo.InvariantCulture)}, {m_defW.ToString(CultureInfo.InvariantCulture)}]",
			_ => DefaultString
		};
	}

	private void LoadVecDefault() {
		try {
			var arr = JsonSerializer.Deserialize<float[]>(DefaultString ?? "[]") ?? [];
			m_defX = arr.Length > 0 ? arr[0] : 0f;
			m_defY = arr.Length > 1 ? arr[1] : 0f;
			m_defZ = arr.Length > 2 ? arr[2] : 0f;
			m_defW = arr.Length > 3 ? arr[3] : 0f;
		} catch {
			m_defX = m_defY = m_defZ = m_defW = 0f;
		}

		OnPropertyChanged(nameof(DefaultX));
		OnPropertyChanged(nameof(DefaultY));
		OnPropertyChanged(nameof(DefaultZ));
		OnPropertyChanged(nameof(DefaultW));
	}

	private void OnEnumOptionsChanged(object? sender, NotifyCollectionChangedEventArgs e) {
		m_owner.IsDirty = true;
		OnPropertyChanged(nameof(EnumOptionsStrings));
	}

	[RelayCommand]
	private void AddEnumOption() {
		EnumOptions.Add(new StringOptionVM { Value = "option" });
	}

	[RelayCommand]
	private void AddVariant() {
		Variants.Add(new StringOptionVM { Value = "variant" });
	}

	public void NotifyAvailableTypesChanged() {
		OnPropertyChanged(nameof(AvailableTypes));
	}

	partial void OnTypeKeyChanged(string value) {
		m_owner.IsDirty = true;
		OnPropertyChanged(nameof(IsEnum));
		OnPropertyChanged(nameof(IsBoolType));
		OnPropertyChanged(nameof(IsIntType));
		OnPropertyChanged(nameof(IsFloatType));
		OnPropertyChanged(nameof(IsStringType));
		OnPropertyChanged(nameof(IsNodeType));
		OnPropertyChanged(nameof(IsAssetType));
		OnPropertyChanged(nameof(IsVec2Type));
		OnPropertyChanged(nameof(IsVec3Type));
		OnPropertyChanged(nameof(IsColor3Type));
		OnPropertyChanged(nameof(IsColor4Type));
		OnPropertyChanged(nameof(HasTypedDefault));
		OnPropertyChanged(nameof(HasMinMax));
		LoadVecDefault();
	}

	partial void OnNameChanged(string value) {
		m_owner.IsDirty = true;
	}

	partial void OnRefTypeStringChanged(string value) {
		m_owner.IsDirty = true;
	}

	partial void OnIsArrayChanged(bool value) {
		m_owner.IsDirty = true;
		OnPropertyChanged(nameof(HasTypedDefault));
		OnPropertyChanged(nameof(HasMinMax));
	}

	partial void OnDescriptionChanged(string value) {
		m_owner.IsDirty = true;
	}

	partial void OnMinStringChanged(string value) {
		m_owner.IsDirty = true;
	}

	partial void OnMaxStringChanged(string value) {
		m_owner.IsDirty = true;
	}

	partial void OnDefaultStringChanged(string value) {
		m_owner.IsDirty = true;
		OnPropertyChanged(nameof(DefaultBool));
		OnPropertyChanged(nameof(DefaultFloat));
		OnPropertyChanged(nameof(DefaultInt));
	}
}

public partial class StructTypeVM : ObservableObject, IRowSplittable {
	private readonly SchemaViewModel m_owner;
	[ObservableProperty] private string m_discriminator = "";
	[ObservableProperty] private bool m_isEditingName;

	[ObservableProperty] private string m_name = "NewType";
	[ObservableProperty] private string m_nameDraft = "";

	public StructTypeVM(SchemaViewModel owner) {
		m_owner = owner;
		Fields.CollectionChanged += (_, _) => {
			m_owner.IsDirty = true;
			OnPropertyChanged(nameof(DiscriminatorOptions));
		};
	}

	public ObservableCollection<SchemaFieldItemVM> Fields { get; } = [];

	public IEnumerable<string> DiscriminatorOptions =>
		new[] { "" }.Concat(Fields.Where(f => f.TypeKey == "enum").Select(f => f.Name));

	public bool ShouldSplitRow => true;

	partial void OnDiscriminatorChanged(string value) {
		m_owner.IsDirty = true;
	}

	[RelayCommand]
	private void BeginEditName() {
		NameDraft = Name;
		IsEditingName = true;
	}

	[RelayCommand]
	private void CommitEditName() {
		if (!string.IsNullOrWhiteSpace(NameDraft))
			Name = NameDraft;
		IsEditingName = false;
	}

	[RelayCommand]
	private void AddField() {
		Fields.Add(new SchemaFieldItemVM(m_owner) { Name = NextName(Fields) });
		m_owner.IsDirty = true;
	}

	internal static string NextName(IEnumerable<SchemaFieldItemVM> existing) {
		var names = new HashSet<string>(existing.Select(f => f.Name));
		if (!names.Contains("field")) return "field";
		var i = 2;
		while (names.Contains($"field_{i}")) i++;
		return $"field_{i}";
	}

	partial void OnNameChanged(string value) {
		m_owner.IsDirty = true;
		m_owner.NotifyTypesChanged();
	}
}
