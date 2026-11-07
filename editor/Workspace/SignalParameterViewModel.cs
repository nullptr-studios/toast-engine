using System;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Proto.Events;

namespace editor.Workspace;

public enum SignalParamKind {
	Bool,
	Integer,
	Number,
	String,
	Vec2,
	Vec3,
	Vec4,
	Any,
	Other
}

public static class SignalTypes {
	public static SignalParamKind Classify(string type) {
		var t = Normalize(type);
		if (t is "bool" or "boolean") return SignalParamKind.Bool;
		if (t is "float" or "double" or "number") return SignalParamKind.Number;
		if (t is "int" or "integer" or "short" or "long" or "unsigned" or "size_t" or "char" ||
		    t.StartsWith("int", StringComparison.Ordinal) && t.EndsWith("_t", StringComparison.Ordinal) ||
		    t.StartsWith("uint", StringComparison.Ordinal) && t.EndsWith("_t", StringComparison.Ordinal))
			return SignalParamKind.Integer;
		if (t is "string" or "string_view") return SignalParamKind.String;
		if (t.EndsWith("vec2", StringComparison.Ordinal)) return SignalParamKind.Vec2;
		if (t.EndsWith("vec3", StringComparison.Ordinal)) return SignalParamKind.Vec3;
		if (t.EndsWith("vec4", StringComparison.Ordinal)) return SignalParamKind.Vec4;
		if (t == "any") return SignalParamKind.Any;
		return SignalParamKind.Other;
	}

	public static bool IsBindable(string type) {
		var t = type.Replace(" ", "", StringComparison.Ordinal);
		foreach (var container in new[] { "vector<", "array<", "span<", "list<", "map<", "set<", "deque<", "[]", "table" })
			if (t.Contains(container, StringComparison.Ordinal))
				return false;
		return true;
	}

	public static bool CanFeed(string signalType, string parameterType) {
		var from = Classify(signalType);
		var to = Classify(parameterType);
		if (to == SignalParamKind.Any || from == SignalParamKind.Any) return true;
		if (from is SignalParamKind.Integer or SignalParamKind.Number or SignalParamKind.Bool &&
		    to is SignalParamKind.Integer or SignalParamKind.Number or SignalParamKind.Bool)
			return true;
		if (from != to) return false;
		return from != SignalParamKind.Other || Normalize(signalType) == Normalize(parameterType);
	}

	private static string Normalize(string type) {
		var t = type.Replace("const", "", StringComparison.Ordinal)
			.Replace("&", "", StringComparison.Ordinal)
			.Replace("*", "", StringComparison.Ordinal)
			.Replace(" ", "", StringComparison.Ordinal);
		if (t.StartsWith("std::", StringComparison.Ordinal)) t = t[5..];
		var scope = t.LastIndexOf(':');
		return scope < 0 ? t : t[(scope + 1)..];
	}
}

public sealed record SignalArgChip(int Index, string Type) {
	public string Text => $"{Index + 1}: {Type}";
}

public sealed partial class SignalParameterViewModel : ObservableObject {
	private static readonly string[] s_anyTypes = ["number", "integer", "string", "boolean"];
	private readonly System.Collections.Generic.IReadOnlyList<SignalArgChip>? m_args;

	[ObservableProperty] private int? m_boundArg;
	[ObservableProperty] private bool m_hasLiteral;

	[ObservableProperty] private bool m_boolValue;
	[ObservableProperty] private int m_integerValue;
	[ObservableProperty] private float m_numberValue;
	[ObservableProperty] private string? m_stringValue = "";
	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;
	[ObservableProperty] private float m_z;
	[ObservableProperty] private float m_w;
	[ObservableProperty] private string m_anyType = s_anyTypes[0];

	public SignalParameterViewModel(string name, string type, System.Collections.Generic.IReadOnlyList<SignalArgChip>? args = null) {
		m_args = args;
		Name = name;
		Type = type;
		Kind = SignalTypes.Classify(type);
	}

	public string Name { get; }
	public string Type { get; }
	public SignalParamKind Kind { get; }
	public string[] AnyTypes => s_anyTypes;
	public string Header => $"{Name}: {Type}";

	public bool IsBoundToSignal => BoundArg is not null;
	public string BoundText => BoundArg is { } index ? m_args is not null && index < m_args.Count ? m_args[index].Text : $"{index + 1}" : "";
	public bool ShowLiteral => BoundArg is null && Kind != SignalParamKind.Other;
	public bool ShowSignalOnlyHint => BoundArg is null && Kind == SignalParamKind.Other;
	public bool IsBound => BoundArg is not null || HasLiteral && Kind != SignalParamKind.Other;
	public bool ShowUseValue => !IsBound && ShowLiteral;
	public bool IsLiteralBound => IsBound && !IsBoundToSignal;

	public bool IsVector => Kind is SignalParamKind.Vec2 or SignalParamKind.Vec3 or SignalParamKind.Vec4;
	public int ColumnSpan => IsVector ? 2 : 1;
	public bool IsBool => Kind == SignalParamKind.Bool;
	public bool IsInteger => Kind == SignalParamKind.Integer;
	public bool IsNumber => Kind == SignalParamKind.Number;
	public bool IsString => Kind == SignalParamKind.String;
	public bool IsVec2 => Kind == SignalParamKind.Vec2;
	public bool IsVec3 => Kind == SignalParamKind.Vec3;
	public bool IsVec4 => Kind == SignalParamKind.Vec4;
	public bool IsAny => Kind == SignalParamKind.Any;
	public bool IsAnyBool => IsAny && AnyType == "boolean";
	public bool IsAnyInteger => IsAny && AnyType == "integer";
	public bool IsAnyNumber => IsAny && AnyType == "number";
	public bool IsAnyString => IsAny && AnyType == "string";

	partial void OnBoundArgChanged(int? value) {
		OnPropertyChanged(nameof(IsBoundToSignal));
		OnPropertyChanged(nameof(BoundText));
		OnPropertyChanged(nameof(ShowLiteral));
		OnPropertyChanged(nameof(ShowSignalOnlyHint));
		OnPropertyChanged(nameof(ShowUseValue));
		OnPropertyChanged(nameof(IsLiteralBound));
		OnPropertyChanged(nameof(IsBound));
		if (value is not null) HasLiteral = false;
	}

	partial void OnHasLiteralChanged(bool value) {
		OnPropertyChanged(nameof(IsBound));
		OnPropertyChanged(nameof(ShowUseValue));
		OnPropertyChanged(nameof(IsLiteralBound));
	}

	partial void OnBoolValueChanged(bool value) => Touch();
	partial void OnIntegerValueChanged(int value) => Touch();
	partial void OnNumberValueChanged(float value) => Touch();
	partial void OnStringValueChanged(string? value) => Touch();
	partial void OnXChanged(float value) => Touch();
	partial void OnYChanged(float value) => Touch();
	partial void OnZChanged(float value) => Touch();
	partial void OnWChanged(float value) => Touch();

	partial void OnAnyTypeChanged(string value) {
		OnPropertyChanged(nameof(IsAnyBool));
		OnPropertyChanged(nameof(IsAnyInteger));
		OnPropertyChanged(nameof(IsAnyNumber));
		OnPropertyChanged(nameof(IsAnyString));
		Touch();
	}

	private void Touch() {
		if (BoundArg is null) HasLiteral = true;
	}

	[RelayCommand]
	private void UseValue() => HasLiteral = true;

	[RelayCommand]
	private void ClearBinding() {
		BoundArg = null;
		HasLiteral = false;
	}

	public ParamBinding ToBinding() {
		if (BoundArg is { } index) return new ParamBinding { SignalArg = (uint)index };
		return new ParamBinding { Literal = Literal() };
	}

	private string Literal() {
		var c = CultureInfo.InvariantCulture;
		return Kind switch {
			SignalParamKind.Bool => BoolValue ? "true" : "false",
			SignalParamKind.Integer => IntegerValue.ToString(c),
			SignalParamKind.Number => NumberValue.ToString("R", c),
			SignalParamKind.String => StringValue ?? "",
			SignalParamKind.Vec2 => $"{X.ToString("R", c)} {Y.ToString("R", c)}",
			SignalParamKind.Vec3 => $"{X.ToString("R", c)} {Y.ToString("R", c)} {Z.ToString("R", c)}",
			SignalParamKind.Vec4 => $"{X.ToString("R", c)} {Y.ToString("R", c)} {Z.ToString("R", c)} {W.ToString("R", c)}",
			// the engine reads the type of an untyped parameter from this prefix
			SignalParamKind.Any => AnyType switch {
				"integer" => $"i:{IntegerValue.ToString(c)}",
				"string" => $"s:{StringValue}",
				"boolean" => $"b:{(BoolValue ? "true" : "false")}",
				_ => $"n:{NumberValue.ToString("R", c)}"
			},
			_ => ""
		};
	}
}
