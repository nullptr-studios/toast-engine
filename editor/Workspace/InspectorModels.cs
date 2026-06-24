using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Components.Elements;
using editor.Engine;

namespace editor.Workspace;

public enum WidgetKind {
	Float,
	Int,
	Bool,
	String,
	Vec2,
	Vec3,
	Vec4,
	AssetRef,
	NodeRef,
	ReadOnly
}

public static class InspectorFormat {
	private static readonly Regex NumberToken = new(@"-?\d+(\.\d+)?([eE][-+]?\d+)?", RegexOptions.Compiled);

	public static WidgetKind KindOf(string fieldType, bool isArray, string typeName) {
		if (isArray) return WidgetKind.ReadOnly; // TODO: editable array widgets

		return fieldType switch {
			"bool_t" => WidgetKind.Bool,
			"int_t" => WidgetKind.Int,
			"float_t" => WidgetKind.Float,
			"double_t" => WidgetKind.Float,
			"string_t" => WidgetKind.String,
			"vec2_t" => WidgetKind.Vec2,
			"vec3_t" => WidgetKind.Vec3,
			"vec4_t" => WidgetKind.Vec4,
			"quaternion_t" => WidgetKind.Vec3, // shown as euler degrees; engine converts on the inspector path
			"uid_t" => typeName.Contains("AssetHandle<") ? WidgetKind.AssetRef : WidgetKind.NodeRef,
			_ => WidgetKind.ReadOnly
		};
	}

	// "m_local_position" -> "Local Position"; "m_uid" -> "Uid"
	public static string DisplayName(string rawName) {
		var name = rawName;
		if (name.StartsWith("m_", StringComparison.Ordinal)) name = name[2..];
		var words = name.Split('_', StringSplitOptions.RemoveEmptyEntries)
			.Select(w => w.Length == 0 ? w : char.ToUpperInvariant(w[0]) + w[1..]);
		return string.Join(' ', words);
	}

	public static string Float(float v) => v.ToString("R", CultureInfo.InvariantCulture);

	public static bool TryFloat(string s, out float v) =>
		float.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out v);

	public static bool TryInt(string s, out int v) =>
		int.TryParse(s.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out v);

	public static float[] Floats(string s) =>
		s.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)
			.Select(t => TryFloat(t, out var f) ? f : 0f)
			.ToArray();

	// the inner T of AssetHandle<T> or Box<T>, bare-named, used for picker/drag filtering
	public static string? InnerType(string typeName) {
		var open = typeName.IndexOf('<');
		var close = typeName.LastIndexOf('>');
		if (open < 0 || close <= open) return null;
		var inner = typeName.Substring(open + 1, close - open - 1).Trim();
		var sep = inner.LastIndexOf(':');
		return sep >= 0 ? inner[(sep + 1)..] : inner;
	}

	/// <summary>Normalizes a C++ default initializer into the engine text encoding, or null if not parseable</summary>
	public static string? NormalizeDefault(WidgetKind kind, string? raw) {
		if (string.IsNullOrWhiteSpace(raw)) return null;

		switch (kind) {
			case WidgetKind.Float:
			case WidgetKind.Int: {
				var m = NumberToken.Match(raw);
				return m.Success ? m.Value : null;
			}
			case WidgetKind.Bool:
				if (raw.Contains("true", StringComparison.OrdinalIgnoreCase)) return "true";
				if (raw.Contains("false", StringComparison.OrdinalIgnoreCase)) return "false";
				return null;
			case WidgetKind.String: {
				var q = raw.IndexOf('"');
				var q2 = raw.LastIndexOf('"');
				return q >= 0 && q2 > q ? raw.Substring(q + 1, q2 - q - 1) : null;
			}
			case WidgetKind.Vec2:
			case WidgetKind.Vec3:
			case WidgetKind.Vec4: {
				var n = kind == WidgetKind.Vec2 ? 2 : kind == WidgetKind.Vec3 ? 3 : 4;
				var tokens = NumberToken.Matches(raw).Select(m => m.Value).ToList();
				if (tokens.Count == 0) return null;
				if (tokens.Count == 1) tokens = Enumerable.Repeat(tokens[0], n).ToList(); // glm::vecN(x) fills all
				if (tokens.Count < n) return null;
				return string.Join(' ', tokens.Take(n));
			}
			default:
				return null;
		}
	}

	public static string? TypeZero(WidgetKind kind) => kind switch {
		WidgetKind.Float or WidgetKind.Int => "0",
		WidgetKind.Bool => "false",
		WidgetKind.String => "",
		WidgetKind.Vec2 => "0 0",
		WidgetKind.Vec3 => "0 0 0",
		WidgetKind.Vec4 => "0 0 0 0",
		_ => null
	};

	public static bool ValuesEqual(WidgetKind kind, string a, string b) {
		switch (kind) {
			case WidgetKind.Float:
			case WidgetKind.Int:
			case WidgetKind.Vec2:
			case WidgetKind.Vec3:
			case WidgetKind.Vec4: {
				var fa = Floats(a);
				var fb = Floats(b);
				if (fa.Length != fb.Length) return false;
				return !fa.Where((t, i) => Math.Abs(t - fb[i]) > 1e-4f).Any();
			}
			default:
				return string.Equals(a.Trim(), b.Trim(), StringComparison.Ordinal);
		}
	}
}

public partial class FieldVM : ObservableObject {
	public string ParameterName { get; }
	public string DisplayName { get; }
	public WidgetKind Kind { get; }
	public bool ReadOnly { get; }
	public double Min { get; }
	public double Max { get; }
	public string? Unit { get; }
	public string? RefType { get; }

	private readonly string? m_default; // engine-encoded, null if unknown
	private bool m_suppress;            // true while applying an engine value

	public event Action<FieldVM, string>? Edited;

	public DateTime LastEdit { get; private set; } = DateTime.MinValue;

	[ObservableProperty] private float m_float;
	[ObservableProperty] private int m_int;
	[ObservableProperty] private bool m_bool;
	[ObservableProperty] private string? m_string = "";
	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;
	[ObservableProperty] private float m_z;
	[ObservableProperty] private float m_w;
	[ObservableProperty] private string? m_ref;
	[ObservableProperty] private string m_readOnlyText = "";

	[ObservableProperty] private bool m_isDefault = true;
	[ObservableProperty] private bool m_visible = true;

	public bool ShowReset => Editable && !IsDefault;

	partial void OnIsDefaultChanged(bool value) => OnPropertyChanged(nameof(ShowReset));

	public ObservableCollection<TextSegment> Segments { get; } = [];

	public FieldVM(FieldInfo info) {
		ParameterName = info.Name;
		Kind = InspectorFormat.KindOf(info.FieldType, info.IsArray, info.TypeName);

		var nameOverride = ReflectionDatabase.GetAttr(info.Attributes, "Name");
		DisplayName = string.IsNullOrEmpty(nameOverride) ? InspectorFormat.DisplayName(info.Name) : nameOverride;

		ReadOnly = ReflectionDatabase.HasAttr(info.Attributes, "ReadOnly")
		           || ReflectionDatabase.HasAttr(info.Attributes, "InspectorNoModify");

		Unit = ReflectionDatabase.GetAttr(info.Attributes, "Unit");
		RefType = InspectorFormat.InnerType(info.TypeName);

		var range = ReflectionDatabase.GetAttrArgs(info.Attributes, "Range");
		Min = range.Length > 0 && InspectorFormat.TryFloat(range[0], out var lo) ? lo : double.NegativeInfinity;
		Max = range.Length > 1 && InspectorFormat.TryFloat(range[1], out var hi) ? hi : double.PositiveInfinity;

		m_default = InspectorFormat.NormalizeDefault(Kind, info.Default) ?? InspectorFormat.TypeZero(Kind);

		Segments.Add(new TextSegment(DisplayName, false));
	}

	public bool IsFloat => Kind == WidgetKind.Float;
	public bool IsInt => Kind == WidgetKind.Int;
	public bool IsBool => Kind == WidgetKind.Bool;
	public bool IsString => Kind == WidgetKind.String;
	public bool IsVec2 => Kind == WidgetKind.Vec2;
	public bool IsVec3 => Kind == WidgetKind.Vec3;
	public bool IsVec4 => Kind == WidgetKind.Vec4;
	public bool IsAssetRef => Kind == WidgetKind.AssetRef;
	public bool IsNodeRef => Kind == WidgetKind.NodeRef;
	public bool IsReadOnlyText => Kind == WidgetKind.ReadOnly;
	public bool IsVectorLayout => Kind is WidgetKind.Vec2 or WidgetKind.Vec3 or WidgetKind.Vec4;
	public bool Editable => !ReadOnly && Kind != WidgetKind.ReadOnly;

	partial void OnFloatChanged(float value) => OnUserEdited();
	partial void OnIntChanged(int value) => OnUserEdited();
	partial void OnBoolChanged(bool value) => OnUserEdited();
	partial void OnStringChanged(string? value) => OnUserEdited();
	partial void OnXChanged(float value) => OnUserEdited();
	partial void OnYChanged(float value) => OnUserEdited();
	partial void OnZChanged(float value) => OnUserEdited();
	partial void OnWChanged(float value) => OnUserEdited();
	partial void OnRefChanged(string? value) => OnUserEdited();

	private void OnUserEdited() {
		if (m_suppress) return;
		LastEdit = DateTime.UtcNow;
		var s = ToEngineString();
		UpdateIsDefault(s);
		Edited?.Invoke(this, s);
	}

	private string ToEngineString() {
		return Kind switch {
			WidgetKind.Float => InspectorFormat.Float(Float),
			WidgetKind.Int => Int.ToString(CultureInfo.InvariantCulture),
			WidgetKind.Bool => Bool ? "true" : "false",
			WidgetKind.String => String ?? "",
			WidgetKind.Vec2 => $"{InspectorFormat.Float(X)} {InspectorFormat.Float(Y)}",
			WidgetKind.Vec3 => $"{InspectorFormat.Float(X)} {InspectorFormat.Float(Y)} {InspectorFormat.Float(Z)}",
			WidgetKind.Vec4 =>
				$"{InspectorFormat.Float(X)} {InspectorFormat.Float(Y)} {InspectorFormat.Float(Z)} {InspectorFormat.Float(W)}",
			WidgetKind.AssetRef or WidgetKind.NodeRef => Ref ?? "0",
			_ => ReadOnlyText
		};
	}

	public void ApplyEngineString(string s) {
		m_suppress = true;
		try {
			switch (Kind) {
				case WidgetKind.Float:
					if (InspectorFormat.TryFloat(s, out var f)) Float = f;
					break;
				case WidgetKind.Int:
					if (InspectorFormat.TryInt(s, out var i)) Int = i;
					break;
				case WidgetKind.Bool:
					Bool = s.Trim().Equals("true", StringComparison.OrdinalIgnoreCase);
					break;
				case WidgetKind.String:
					String = s;
					break;
				case WidgetKind.Vec2: {
					var p = InspectorFormat.Floats(s);
					if (p.Length >= 2) { X = p[0]; Y = p[1]; }
					break;
				}
				case WidgetKind.Vec3: {
					var p = InspectorFormat.Floats(s);
					if (p.Length >= 3) { X = p[0]; Y = p[1]; Z = p[2]; }
					break;
				}
				case WidgetKind.Vec4: {
					var p = InspectorFormat.Floats(s);
					if (p.Length >= 4) { X = p[0]; Y = p[1]; Z = p[2]; W = p[3]; }
					break;
				}
				case WidgetKind.AssetRef:
				case WidgetKind.NodeRef:
					Ref = string.IsNullOrEmpty(s) || s == "0" ? null : s;
					break;
				default:
					ReadOnlyText = s;
					break;
			}
		}
		finally {
			m_suppress = false;
		}

		UpdateIsDefault(s);
	}

	private void UpdateIsDefault(string current) {
		IsDefault = m_default is null || InspectorFormat.ValuesEqual(Kind, current, m_default);
	}

	[RelayCommand]
	private void Reset() {
		if (m_default is null) return;
		Edited?.Invoke(this, m_default);
		ApplyEngineString(m_default);
	}

	public bool ApplyFilter(string query) {
		Segments.Clear();
		if (string.IsNullOrEmpty(query)) {
			Segments.Add(new TextSegment(DisplayName, false));
			Visible = true;
			return true;
		}

		var q = query.Trim();
		var idx = DisplayName.IndexOf(q, StringComparison.OrdinalIgnoreCase);
		if (idx >= 0) {
			if (idx > 0) Segments.Add(new TextSegment(DisplayName[..idx], false));
			Segments.Add(new TextSegment(DisplayName.Substring(idx, q.Length), true));
			if (idx + q.Length < DisplayName.Length) Segments.Add(new TextSegment(DisplayName[(idx + q.Length)..], false));
			Visible = true;
		}
		else {
			Segments.Add(new TextSegment(DisplayName, false));
			// space-insensitive fallback
			Visible = DisplayName.Replace(" ", "").Contains(q.Replace(" ", ""), StringComparison.OrdinalIgnoreCase);
		}

		return Visible;
	}
}

public partial class ButtonVM : ObservableObject {
	public string Label { get; }
	public string Function { get; }
	private readonly Action<string> m_invoke;

	public ButtonVM(string label, string function, Action<string> invoke) {
		Label = label;
		Function = function;
		m_invoke = invoke;
	}

	[RelayCommand]
	private void Click() => m_invoke(Function);
}

public partial class SubgroupVM : ObservableObject {
	public string Name { get; }
	public ObservableCollection<FieldVM> Fields { get; } = [];

	private readonly string m_key;
	private readonly InspectorState m_state;
	private bool m_ready;

	[ObservableProperty] private bool m_collapsed;
	[ObservableProperty] private bool m_visible = true;

	public SubgroupVM(string name, string key, InspectorState state) {
		Name = name;
		m_key = key;
		m_state = state;
		Collapsed = state.Get(key, true);
		m_ready = true;
	}

	public bool Expanded {
		get => !Collapsed;
		set => Collapsed = !value;
	}

	partial void OnCollapsedChanged(bool value) {
		OnPropertyChanged(nameof(Expanded));
		if (m_ready) m_state.Set(m_key, value);
	}

	public bool ApplyFilter(string query) {
		var any = false;
		foreach (var f in Fields) any |= f.ApplyFilter(query);
		Visible = any;
		return any;
	}
}

public partial class GroupVM : ObservableObject {
	public string Name { get; }
	public string ColorKey { get; }
	public ObservableCollection<FieldVM> Fields { get; } = [];
	public ObservableCollection<SubgroupVM> Subgroups { get; } = [];

	private readonly string m_key;
	private readonly InspectorState m_state;
	private bool m_ready;

	[ObservableProperty] private bool m_collapsed;
	[ObservableProperty] private bool m_visible = true;

	public GroupVM(string name, string colorKey, string key, InspectorState state) {
		Name = name;
		ColorKey = colorKey;
		m_key = key;
		m_state = state;
		Collapsed = state.Get(key, true);
		m_ready = true;
	}

	public bool Expanded {
		get => !Collapsed;
		set => Collapsed = !value;
	}

	partial void OnCollapsedChanged(bool value) {
		OnPropertyChanged(nameof(Expanded));
		if (m_ready) m_state.Set(m_key, value);
	}

	public bool ApplyFilter(string query) {
		var any = false;
		foreach (var f in Fields) any |= f.ApplyFilter(query);
		foreach (var s in Subgroups) any |= s.ApplyFilter(query);
		Visible = any;
		return any;
	}
}

public partial class ClassCardVM : ObservableObject {
	public string TypeName { get; }
	public string ColorKey { get; }
	public ObservableCollection<FieldVM> Fields { get; } = [];
	public ObservableCollection<GroupVM> Groups { get; } = [];
	public ObservableCollection<ButtonVM> Buttons { get; } = [];

	private readonly string m_key;
	private readonly InspectorState m_state;
	private bool m_ready;

	[ObservableProperty] private bool m_expanded = true;

	public ClassCardVM(string typeName, string colorKey, string key, InspectorState state) {
		TypeName = typeName;
		ColorKey = colorKey;
		m_key = key;
		m_state = state;
		Expanded = !state.Get(key, false); // class cards default expanded
		m_ready = true;
	}

	partial void OnExpandedChanged(bool value) {
		if (m_ready) m_state.Set(m_key, !value);
	}

	public void ApplyFilter(string query) {
		foreach (var f in Fields) f.ApplyFilter(query);
		foreach (var g in Groups) g.ApplyFilter(query);
	}
}
