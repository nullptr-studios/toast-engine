using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
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
	ReadOnly,
	Color3,
	Color4,
	Color3Array,
	Color4Array,
	Array
}

public static class InspectorFormat {
	internal const string NullUid = "AAAAAAAAAAA";
	private static readonly Regex NumberToken = new(@"(?<!\w)-?\d+(\.\d+)?([eE][-+]?\d+)?", RegexOptions.Compiled);

	public static WidgetKind KindOf(string fieldType, bool isArray, string typeName) {
		if (isArray) return WidgetKind.Array;

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

	public static string Float(float v) {
		return v.ToString("R", CultureInfo.InvariantCulture);
	}

	public static bool TryFloat(string s, out float v) {
		return float.TryParse(s.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out v);
	}

	public static bool TryInt(string s, out int v) {
		return int.TryParse(s.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out v);
	}

	public static float[] Floats(string s) {
		var cleaned = s.Replace('[', ' ').Replace(']', ' ').Replace(',', ' ').Replace(';', ' ');
		return cleaned.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
			.Select(t => TryFloat(t, out var f) ? f : 0f)
			.ToArray();
	}

	// the inner T of AssetHandle<T> or Box<T>, bare-named, used for picker/drag filtering
	public static string? InnerType(string typeName) {
		var open = typeName.IndexOf('<');
		if (open < 0) return null;
		var depth = 1;
		for (var i = open + 1; i < typeName.Length; i++)
			if (typeName[i] == '<') {
				depth++;
			} else if (typeName[i] == '>') {
				depth--;
				if (depth == 0) {
					var inner = typeName.Substring(open + 1, i - open - 1).Trim();
					if (inner.Contains('<')) return InnerType(inner);
					var sep = inner.LastIndexOf(':');
					return sep >= 0 ? inner[(sep + 1)..] : inner;
				}
			}

		return null;
	}

	/// <summary>Normalizes a C++ default initializer into the engine text encoding, or null if not parseable</summary>
	public static string? NormalizeDefault(WidgetKind kind, string? raw, string? fieldType = null) {
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
			case WidgetKind.Color3:
			case WidgetKind.Vec4:
			case WidgetKind.Color4: {
				if (fieldType == "quaternion_t") return QuaternionDefaultToEuler(raw);
				var n = kind is WidgetKind.Vec2 ? 2 : kind is WidgetKind.Vec3 or WidgetKind.Color3 ? 3 : 4;
				var tokens = NumberToken.Matches(raw).Select(m => m.Value).ToList();
				if (tokens.Count == 0) return null;
				if (tokens.Count == 1) tokens = Enumerable.Repeat(tokens[0], n).ToList(); // glm::vecN(x) broadcasts
				if (tokens.Count < n) return null;
				return string.Join(' ', tokens.Take(n));
			}
			default:
				return null;
		}
	}

	private static string? QuaternionDefaultToEuler(string raw) {
		var tokens = NumberToken.Matches(raw).Select(m => m.Value).ToList();
		if (tokens.Count == 0) return "0 0 0"; // {} → identity quaternion
		if (tokens.Count != 4) return null;
		if (!TryFloat(tokens[0], out var a) || !TryFloat(tokens[1], out var b) ||
		    !TryFloat(tokens[2], out var c) || !TryFloat(tokens[3], out var d)) return null;

		// glm brace-init uses struct member order x,y,z,w
		// glm constructor call uses mathematical order w,x,y,z
		// this is so retarded -x
		float x, y, z, w;
		if (raw.Contains('{')) {
			x = a;
			y = b;
			z = c;
			w = d;
		} else {
			w = a;
			x = b;
			y = c;
			z = d;
		}

		// XYZ intrinsic euler angles in radians → degrees
		var roll = MathF.Atan2(2f * (w * x + y * z), 1f - 2f * (x * x + y * y));
		var pitch = MathF.Asin(Math.Clamp(2f * (w * y - z * x), -1f, 1f));
		var yaw = MathF.Atan2(2f * (w * z + x * y), 1f - 2f * (y * y + z * z));

		const float toDeg = 180f / MathF.PI;
		return $"{Float(roll * toDeg)} {Float(pitch * toDeg)} {Float(yaw * toDeg)}";
	}

	public static string? TypeZero(WidgetKind kind) {
		return kind switch {
			WidgetKind.Float or WidgetKind.Int => "0",
			WidgetKind.Bool => "false",
			WidgetKind.String => "",
			WidgetKind.Vec2 => "0 0",
			WidgetKind.Vec3 or WidgetKind.Color3 => "0 0 0",
			WidgetKind.Vec4 or WidgetKind.Color4 => "0 0 0 0",
			WidgetKind.Array => "",
			_ => null
		};
	}

	public static int ArrayElementStride(WidgetKind elementKind) {
		return elementKind switch {
			WidgetKind.Float or WidgetKind.Int or WidgetKind.Bool or WidgetKind.String
				or WidgetKind.AssetRef or WidgetKind.NodeRef => 1,
			WidgetKind.Vec2 => 2,
			WidgetKind.Vec3 or WidgetKind.Color3 => 3,
			WidgetKind.Vec4 or WidgetKind.Color4 => 4,
			_ => 1
		};
	}

	public static bool ValuesEqual(WidgetKind kind, string a, string b) {
		switch (kind) {
			case WidgetKind.Float:
			case WidgetKind.Int:
			case WidgetKind.Vec2:
			case WidgetKind.Vec3:
			case WidgetKind.Color3:
			case WidgetKind.Vec4:
			case WidgetKind.Color4: {
				var fa = Floats(a);
				var fb = Floats(b);
				if (fa.Length != fb.Length) return false;
				return !fa.Where((t, i) => Math.Abs(t - fb[i]) > 1e-4f).Any();
			}
			case WidgetKind.Array:
				return string.Equals(a.Trim(), b.Trim(), StringComparison.Ordinal);
			default:
				return string.Equals(a.Trim(), b.Trim(), StringComparison.Ordinal);
		}
	}
}

public partial class FieldVM : ObservableObject {
	private readonly string? m_default; // engine-encoded, null if unknown
	[ObservableProperty] private bool m_bool;

	[ObservableProperty] private float m_float;
	[ObservableProperty] private int m_int;

	[ObservableProperty] private bool m_isDefault = true;
	[ObservableProperty] private string m_readOnlyText = "";
	[ObservableProperty] private string? m_ref;
	[ObservableProperty] private string? m_string = "";
	private bool m_suppress; // true while applying an engine value
	[ObservableProperty] private bool m_visible = true;
	[ObservableProperty] private float m_w;
	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;
	[ObservableProperty] private float m_z;

	public FieldVM(FieldInfo info) {
		ParameterName = info.Name;
		Kind = InspectorFormat.KindOf(info.FieldType, info.IsArray, info.TypeName);

		if (ReflectionDatabase.HasAttr(info.Attributes, "Color")) {
			if (info.IsArray) {
				if (info.FieldType == "vec3_t") Kind = WidgetKind.Color3Array;
				else if (info.FieldType == "vec4_t") Kind = WidgetKind.Color4Array;
			} else {
				if (info.FieldType == "vec3_t") Kind = WidgetKind.Color3;
				else if (info.FieldType == "vec4_t") Kind = WidgetKind.Color4;
			}
		}

		var nameOverride = ReflectionDatabase.GetAttr(info.Attributes, "Name");
		DisplayName = string.IsNullOrEmpty(nameOverride) ? InspectorFormat.DisplayName(info.Name) : nameOverride;

		ReadOnly = ReflectionDatabase.HasAttr(info.Attributes, "ReadOnly")
			|| ReflectionDatabase.HasAttr(info.Attributes, "InspectorNoModify");

		Unit = ReflectionDatabase.GetAttr(info.Attributes, "Unit");
		RefType = InspectorFormat.InnerType(info.TypeName);

		var range = ReflectionDatabase.GetAttrArgs(info.Attributes, "Range");
		Min = range.Length > 0 && InspectorFormat.TryFloat(range[0], out var lo) ? lo : double.NegativeInfinity;
		Max = range.Length > 1 && InspectorFormat.TryFloat(range[1], out var hi) ? hi : double.PositiveInfinity;

		m_default = InspectorFormat.NormalizeDefault(Kind, info.Default, info.FieldType) ??
			InspectorFormat.TypeZero(Kind);

		Segments.Add(new TextSegment(DisplayName, false));

		if (m_default != null) ApplyEngineString(m_default);

		if (Kind == WidgetKind.Array) {
			ArrayElementKind = InspectorFormat.KindOf(info.FieldType, false, info.TypeName);
			ArrayItems.CollectionChanged += (_, _) => OnUserEdited();
		}
	}

	internal FieldVM(
		WidgetKind elementKind, string displayName, bool readOnly, string? unit, string? refType, double min,
		double max) {
		ParameterName = "";
		Kind = elementKind;
		DisplayName = displayName;
		ReadOnly = readOnly;
		Unit = unit;
		RefType = refType;
		Min = min;
		Max = max;
		m_default = InspectorFormat.TypeZero(elementKind);
		Segments.Add(new TextSegment(DisplayName, false));
		if (m_default != null) ApplyEngineString(m_default);
	}

	public string ParameterName { get; }
	public string DisplayName { get; }
	public WidgetKind Kind { get; }
	public bool ReadOnly { get; }
	public double Min { get; }
	public double Max { get; }
	public string? Unit { get; }
	public string? RefType { get; }

	public DateTime LastEdit { get; private set; } = DateTime.MinValue;

	public bool ShowReset => Editable && !IsDefault;

	public ObservableCollection<TextSegment> Segments { get; } = [];

	public WidgetKind ArrayElementKind { get; }
	public bool IsArray => Kind == WidgetKind.Array;
	public ObservableCollection<FieldVM> ArrayItems { get; } = [];

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

	public bool Editable =>
		!ReadOnly && Kind != WidgetKind.ReadOnly && Kind != WidgetKind.Color3Array && Kind != WidgetKind.Color4Array;

	public bool IsColor3 => Kind == WidgetKind.Color3;
	public bool IsColor4 => Kind == WidgetKind.Color4;
	public bool IsColorArray => Kind is WidgetKind.Color3Array or WidgetKind.Color4Array;
	public ObservableCollection<IBrush> ColorBrushes { get; } = [];

	public event Action<FieldVM, string>? Edited;

	partial void OnIsDefaultChanged(bool value) {
		OnPropertyChanged(nameof(ShowReset));
	}

	partial void OnFloatChanged(float value) {
		OnUserEdited();
	}

	partial void OnIntChanged(int value) {
		OnUserEdited();
	}

	partial void OnBoolChanged(bool value) {
		OnUserEdited();
	}

	partial void OnStringChanged(string? value) {
		OnUserEdited();
	}

	partial void OnXChanged(float value) {
		OnUserEdited();
	}

	partial void OnYChanged(float value) {
		OnUserEdited();
	}

	partial void OnZChanged(float value) {
		OnUserEdited();
	}

	partial void OnWChanged(float value) {
		OnUserEdited();
	}

	partial void OnRefChanged(string? value) {
		OnUserEdited();
	}

	[RelayCommand]
	private void AddArrayItem() {
		var child = CreateArrayElement();
		WireChild(child);
		ArrayItems.Add(child);
		OnUserEdited();
	}

	private FieldVM CreateArrayElement() {
		return new FieldVM(ArrayElementKind, "", ReadOnly, Unit, RefType, Min, Max);
	}

	private void WireChild(FieldVM child) {
		child.Edited += (_, _) => OnUserEdited();
	}

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
			WidgetKind.Vec3 or WidgetKind.Color3 =>
				$"{InspectorFormat.Float(X)} {InspectorFormat.Float(Y)} {InspectorFormat.Float(Z)}",
			WidgetKind.Vec4 or WidgetKind.Color4 =>
				$"{InspectorFormat.Float(X)} {InspectorFormat.Float(Y)} {InspectorFormat.Float(Z)} {InspectorFormat.Float(W)}",
			WidgetKind.AssetRef or WidgetKind.NodeRef => Ref ?? InspectorFormat.NullUid,
			WidgetKind.Array => string.Join(
				ArrayElementKind == WidgetKind.String ? "\x1f" : " ",
				ArrayItems.Select(c => c.ToEngineString())),
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
					if (p.Length >= 2) {
						X = p[0];
						Y = p[1];
					}

					break;
				}
				case WidgetKind.Vec3:
				case WidgetKind.Color3: {
					var p = InspectorFormat.Floats(s);
					if (p.Length >= 3) {
						X = p[0];
						Y = p[1];
						Z = p[2];
					}

					break;
				}
				case WidgetKind.Vec4:
				case WidgetKind.Color4: {
					var p = InspectorFormat.Floats(s);
					if (p.Length >= 4) {
						X = p[0];
						Y = p[1];
						Z = p[2];
						W = p[3];
					}

					break;
				}
				case WidgetKind.AssetRef:
				case WidgetKind.NodeRef:
					Ref = string.IsNullOrEmpty(s) || s == InspectorFormat.NullUid ? null : s;
					break;
				case WidgetKind.Array: {
					if (string.IsNullOrEmpty(s)) {
						ArrayItems.Clear();
						break;
					}

					if (string.Equals(s, ToEngineString(), StringComparison.Ordinal)) break;
					ArrayItems.Clear();
					var isStr = ArrayElementKind == WidgetKind.String;
					var tokens = isStr
						? s.Split('\x1f')
						: s.Split(' ', StringSplitOptions.RemoveEmptyEntries);
					if (tokens.Length == 0) break;
					var stride = InspectorFormat.ArrayElementStride(ArrayElementKind);
					for (var idx = 0; idx + stride <= tokens.Length; idx += stride) {
						var elementStr = isStr
							? tokens[idx]
							: string.Join(' ', tokens.Skip(idx).Take(stride));
						var child = new FieldVM(ArrayElementKind, "", ReadOnly, Unit, RefType, Min, Max);
						child.ApplyEngineString(elementStr);
						WireChild(child);
						ArrayItems.Add(child);
					}

					break;
				}
				default:
					ReadOnlyText = s;
					break;
			}
		} finally {
			m_suppress = false;
		}

		UpdateIsDefault(s);

		if (Kind == WidgetKind.Color3Array || Kind == WidgetKind.Color4Array) {
			ColorBrushes.Clear();
			var floats = InspectorFormat.Floats(s);
			var stride = Kind == WidgetKind.Color3Array ? 3 : 4;
			for (var idx = 0; idx + stride <= floats.Length; idx += stride) {
				var r = floats[idx];
				var g = floats[idx + 1];
				var b = floats[idx + 2];
				var a = stride == 4 ? floats[idx + 3] : 1f;

				var max = MathF.Max(r, MathF.Max(g, b));
				var scale = max > 1f ? 1f / max : 1f;
				var color = Color.FromArgb(
					(byte)Math.Clamp(MathF.Round(a * 255f), 0f, 255f),
					(byte)Math.Clamp(MathF.Round(r * scale * 255f), 0f, 255f),
					(byte)Math.Clamp(MathF.Round(g * scale * 255f), 0f, 255f),
					(byte)Math.Clamp(MathF.Round(b * scale * 255f), 0f, 255f)
				);
				ColorBrushes.Add(new SolidColorBrush(color));
			}
		}
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
		} else {
			Segments.Add(new TextSegment(DisplayName, false));
			// space-insensitive fallback
			Visible = DisplayName.Replace(" ", "").Contains(q.Replace(" ", ""), StringComparison.OrdinalIgnoreCase);
		}

		return Visible;
	}
}

public partial class ButtonVM : ObservableObject {
	private readonly Action<string> m_invoke;

	public ButtonVM(string label, string function, Action<string> invoke) {
		Label = label;
		Function = function;
		m_invoke = invoke;
	}

	public string Label { get; }
	public string Function { get; }

	[RelayCommand]
	private void Click() {
		m_invoke(Function);
	}
}

public partial class SubgroupVM : ObservableObject {
	private readonly string m_key;
	private readonly bool m_ready;
	private readonly InspectorState m_state;

	[ObservableProperty] private bool m_collapsed;
	[ObservableProperty] private bool m_visible = true;

	public SubgroupVM(string name, string key, InspectorState state) {
		Name = name;
		m_key = key;
		m_state = state;
		Collapsed = state.Get(key, true);
		m_ready = true;
	}

	public string Name { get; }
	public ObservableCollection<FieldVM> Fields { get; } = [];

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
	private readonly string m_key;
	private readonly bool m_ready;
	private readonly InspectorState m_state;

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

	public string Name { get; }
	public string ColorKey { get; }
	public ObservableCollection<FieldVM> Fields { get; } = [];
	public ObservableCollection<SubgroupVM> Subgroups { get; } = [];

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
	private readonly string m_key;
	private readonly bool m_ready;
	private readonly InspectorState m_state;

	[ObservableProperty] private bool m_expanded = true;
	[ObservableProperty] private bool m_visible = true;

	public ClassCardVM(string typeName, string colorKey, string iconName, string key, InspectorState state) {
		TypeName = typeName;
		ColorKey = colorKey;
		try {
			SmallIcon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/1x/{iconName}.png")));
			LargeIcon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/1.5x/{iconName}.png")));
		} catch (Exception ex) {
			Log.Warn($"Failed to load icon for {typeName} ({iconName}): {ex.Message}");
			SmallIcon = new Bitmap(AssetLoader.Open(new Uri("avares://editor/Resources/node_icons/1x/Circle.png")));
			LargeIcon = new Bitmap(AssetLoader.Open(new Uri("avares://editor/Resources/node_icons/1.5x/Circle.png")));
		}

		m_key = key;
		m_state = state;
		Expanded = !state.Get(key, false); // class cards default expanded
		m_ready = true;
	}

	public string TypeName { get; }
	public string ColorKey { get; }
	public Bitmap? SmallIcon { get; }
	public Bitmap? LargeIcon { get; }
	public ObservableCollection<FieldVM> Fields { get; } = [];
	public ObservableCollection<GroupVM> Groups { get; } = [];
	public ObservableCollection<ButtonVM> Buttons { get; } = [];
	public bool HasFields => Fields.Any();
	public bool HasGroups => Groups.Any();
	public bool HasButtons => Buttons.Any();

	partial void OnExpandedChanged(bool value) {
		if (m_ready) m_state.Set(m_key, !value);
	}

	public void ApplyFilter(string query) {
		foreach (var f in Fields) f.ApplyFilter(query);
		foreach (var g in Groups) g.ApplyFilter(query);

		// When there's a filter, hide the card if it has no visible content
		if (string.IsNullOrEmpty(query)) {
			Visible = true;
		} else {
			var hasVisibleContent = Fields.Any(f => f.Visible) || Groups.Any(g => g.Visible);
			Visible = hasVisibleContent;
		}
	}
}
