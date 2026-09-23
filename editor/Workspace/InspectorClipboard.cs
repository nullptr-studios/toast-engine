using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using Avalonia.Input;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Elements;
using editor.Components.Modals;
using editor.Engine;

namespace editor.Workspace;

public enum InspectorClipboardScope {
	Value,
	Array,
	Class,
	Group,
	Subgroup
}

public sealed record InspectorClipboardValue(
	WidgetKind Kind,
	string EngineValue,
	WidgetKind? ElementKind = null,
	string? RefType = null,
	string? EnumLabel = null,
	string[]? EnumOptions = null,
	InspectorClipboardValue[]? Elements = null
);

public sealed record InspectorClipboardField(string ParameterName, InspectorClipboardValue Value);

public sealed record InspectorClipboardPayload(
	int Version,
	InspectorClipboardScope Scope,
	string? ScopeId,
	InspectorClipboardValue? Value,
	InspectorClipboardField[]? Fields
) {
	public const int CurrentVersion = 1;
}

internal interface IInspectorClipboardScope {
	InspectorClipboardScope ClipboardScope { get; }
	string ScopeKey { get; }
	string ScopeName { get; }
	IEnumerable<FieldVM> ClipboardFields { get; }
}

internal interface IInspectorClipboardHost {
	Task CopyFieldAsync(FieldVM field, int? component = null);
	Task PasteFieldAsync(FieldVM field, int? component = null);
	Task<bool> CanPasteFieldAsync(FieldVM field, int? component = null);
	Task CopyArrayItemAsync(FieldVM array, object? item);
	Task PasteArrayItemAsync(FieldVM array, object? item);
	Task<bool> CanPasteArrayItemAsync(FieldVM array, object? item);
	Task AppendArrayPasteAsync(FieldVM array);
	Task<bool> CanAppendArrayPasteAsync(FieldVM array);
	Task CopyScopeAsync(IInspectorClipboardScope scope);
	Task PasteScopeAsync(IInspectorClipboardScope scope);
	Task<bool> CanPasteScopeAsync(IInspectorClipboardScope scope);
}

internal sealed class InspectorPasteCommand(
	Func<object?, Task> execute,
	Func<object?, Task<bool>> refresh) : IAsyncCanExecuteCommand {
	private static readonly object s_nullParameter = new();
	private readonly Dictionary<object, bool> m_canExecute = [];

	public event EventHandler? CanExecuteChanged;

	public bool CanExecute(object? parameter) {
		return m_canExecute.GetValueOrDefault(Key(parameter));
	}

	public async void Execute(object? parameter) {
		if (!CanExecute(parameter)) return;
		try {
			await execute(parameter);
		} catch (Exception ex) {
			Log.Error($"Inspector paste failed: {ex.Message}");
		}
	}

	public async Task RefreshCanExecuteAsync(object? parameter) {
		var key = Key(parameter);
		m_canExecute[key] = false;
		CanExecuteChanged?.Invoke(this, EventArgs.Empty);
		try {
			m_canExecute[key] = await refresh(parameter);
		} catch (Exception ex) {
			m_canExecute[key] = false;
			Log.Warn($"Inspector paste availability check failed: {ex.Message}");
		}
		CanExecuteChanged?.Invoke(this, EventArgs.Empty);
	}

	private static object Key(object? parameter) {
		return parameter ?? s_nullParameter;
	}
}

internal static class InspectorClipboardService {
	private const string FormatName = "toast.inspector.clipboard";
	private static readonly DataFormat<string>? s_format = CreateFormat();
	private static readonly JsonSerializerOptions s_jsonOptions = new() {
		Converters = { new JsonStringEnumConverter() }
	};

	public static async Task WriteAsync(InspectorClipboardPayload payload, string text) {
		if (s_format is null || ModalService.FindActiveWindow()?.Clipboard is not { } clipboard) return;
		try {
			var item = new DataTransferItem();
			item.Set(s_format, JsonSerializer.Serialize(payload, s_jsonOptions));
			item.SetText(text);
			var transfer = new DataTransfer();
			transfer.Add(item);
			await clipboard.SetDataAsync(transfer);
		} catch (Exception ex) {
			Log.Warn($"Inspector clipboard copy failed: {ex.Message}");
		}
	}

	public static async Task<InspectorClipboardPayload?> ReadAsync() {
		if (s_format is null || ModalService.FindActiveWindow()?.Clipboard is not { } clipboard) return null;
		using var transfer = await clipboard.TryGetDataAsync();
		if (transfer is null) return null;

		foreach (var item in transfer.Items) {
			if (!item.Formats.Contains(s_format)) continue;
			if (await item.TryGetRawAsync(s_format) is not string json) continue;
			try {
				var payload = JsonSerializer.Deserialize<InspectorClipboardPayload>(json, s_jsonOptions);
				return payload is not null && IsValid(payload) ? payload : null;
			} catch (JsonException) {
				return null;
			}
		}

		return null;
	}

	private static DataFormat<string>? CreateFormat() {
		try {
			return DataFormat.CreateStringApplicationFormat(FormatName);
		} catch (Exception ex) {
			try {
				Log.Error($"Inspector clipboard format could not be registered: {ex.Message}");
			} catch {
				// logging can be unavailable at the start
			}
			return null;
		}
	}

	private static bool IsValid(InspectorClipboardPayload payload) {
		if (payload.Version != InspectorClipboardPayload.CurrentVersion) return false;
		if (payload.Scope is InspectorClipboardScope.Value or InspectorClipboardScope.Array)
			return payload.Value is not null && ValidValue(payload.Value);
		if (string.IsNullOrEmpty(payload.ScopeId) || payload.Fields is null) return false;
		return payload.Fields.All(field => field is not null && field.ParameterName is not null &&
			field.Value is not null && ValidValue(field.Value));
	}

	private static bool ValidValue(InspectorClipboardValue value) {
		if (value.EngineValue is null) return false;
		if (value.Kind != WidgetKind.Array) return true;
		return value.ElementKind is not null && value.Elements is not null &&
		       value.Elements.All(element => element is not null && ValidValue(element));
	}
}

internal static class InspectorClipboardConverter {
	public static InspectorClipboardValue Capture(FieldVM field) {
		if (!field.IsArray)
			return new InspectorClipboardValue(
				field.Kind,
				field.EngineValue,
				RefType: field.RefType,
				EnumLabel: field.IsEnum ? field.EnumValue : null,
				EnumOptions: field.IsEnum ? field.EnumOptions.ToArray() : null);

		return new InspectorClipboardValue(
			WidgetKind.Array,
			field.EngineValue,
			field.ArrayElementKind,
			field.RefType,
			Elements: field.ArrayItems.Select(Capture).ToArray());
	}

	public static InspectorClipboardValue CaptureComponent(FieldVM field, int component) {
		return new InspectorClipboardValue(WidgetKind.Float,
			InspectorFormat.Float(field.Component(component)));
	}

	public static bool SameShape(FieldVM target, InspectorClipboardValue source) {
		if (target.Kind != source.Kind) return false;
		if (!string.Equals(target.RefType, source.RefType, StringComparison.Ordinal)) return false;
		if (target.IsArray && target.ArrayElementKind != source.ElementKind) return false;
		if (target.IsEnum) {
			var options = source.EnumOptions ?? [];
			if (!target.EnumOptions.SequenceEqual(options, StringComparer.Ordinal)) return false;
		}

		return true;
	}

	public static bool TryConvert(FieldVM target, InspectorClipboardValue source, out string engineValue) {
		engineValue = "";
		if (target.IsArray) return TryConvertArray(target, source, out engineValue);

		switch (target.Kind) {
			case WidgetKind.Float:
				return TryNumericFloat(target, source, out engineValue);
			case WidgetKind.Int:
				return TryNumericInt(target, source, out engineValue);
			case WidgetKind.Vec2:
			case WidgetKind.Vec3:
			case WidgetKind.Vec4:
			case WidgetKind.Color3:
			case WidgetKind.Color4:
				return TryVector(target, source, out engineValue);
			case WidgetKind.Enum:
				return source.Kind == WidgetKind.Enum && source.EnumLabel is { } label &&
				       target.TryEnumEngineValue(label, out engineValue);
			case WidgetKind.Bool:
				if (source.Kind != WidgetKind.Bool) return false;
				if (!bool.TryParse(source.EngineValue, out var boolean)) return false;
				engineValue = boolean ? "true" : "false";
				return true;
			case WidgetKind.String:
				if (source.Kind != WidgetKind.String) return false;
				engineValue = source.EngineValue;
				return true;
			case WidgetKind.AssetRef:
			case WidgetKind.NodeRef:
				if (source.Kind != target.Kind || !ReferenceCompatible(target, source)) return false;
				engineValue = string.IsNullOrEmpty(source.EngineValue)
					? InspectorFormat.NullUid
					: source.EngineValue;
				return true;
			default:
				return false;
		}
	}

	public static bool TryConvertComponent(
		FieldVM target, int component, InspectorClipboardValue source, out string engineValue) {
		engineValue = "";
		if (!target.IsVectorOrColor || component < 0 || component >= target.ComponentCount) return false;
		if (!TryNumeric(target, source, out var value)) return false;
		var converted = target.ClampComponent(component, (float)value);
		var components = target.Components();
		components[component] = converted;
		engineValue = string.Join(' ', components.Select(InspectorFormat.Float));
		return true;
	}

	public static bool TryAppend(
		FieldVM array, InspectorClipboardPayload payload, out string engineValue) {
		engineValue = "";
		if (!array.IsArray) return false;

		var additions = payload.Scope switch {
			InspectorClipboardScope.Value when payload.Value is not null => [payload.Value],
			InspectorClipboardScope.Array when payload.Value?.Elements is not null &&
			                                   ArrayTypesCompatible(array, payload.Value) => payload.Value.Elements,
			_ => null
		};
		if (additions is null) return false;

		var values = array.ArrayItems.Select(i => i.EngineValue).ToList();
		foreach (var source in additions) {
			var target = array.CreateArrayElement();
			if (!TryConvert(target, source, out var converted)) return false;
			values.Add(converted);
		}

		engineValue = array.JoinArrayValues(values);
		return true;
	}

	private static bool TryConvertArray(FieldVM target, InspectorClipboardValue source, out string engineValue) {
		engineValue = "";
		if (source.Kind != WidgetKind.Array || source.Elements is null || !ArrayTypesCompatible(target, source))
			return false;
		var values = new List<string>(source.Elements.Length);
		foreach (var sourceElement in source.Elements) {
			var targetElement = target.CreateArrayElement();
			if (!TryConvert(targetElement, sourceElement, out var converted)) return false;
			values.Add(converted);
		}

		engineValue = target.JoinArrayValues(values);
		return true;
	}

	private static bool ArrayTypesCompatible(FieldVM target, InspectorClipboardValue source) {
		if (source.Kind != WidgetKind.Array || source.ElementKind is not { } sourceKind) return false;
		var targetKind = target.ArrayElementKind;
		if ((targetKind is WidgetKind.Float or WidgetKind.Int) &&
		    (sourceKind is WidgetKind.Float or WidgetKind.Int))
			return true;
		if (CompatibleVectorKinds(targetKind, sourceKind)) return true;
		if (targetKind != sourceKind) return false;
		if (targetKind is WidgetKind.AssetRef or WidgetKind.NodeRef)
			return string.Equals(target.RefType, source.RefType, StringComparison.Ordinal);
		return targetKind is not (WidgetKind.Array or WidgetKind.ReadOnly);
	}

	private static bool TryNumericFloat(FieldVM target, InspectorClipboardValue source, out string engineValue) {
		engineValue = "";
		if (!TryNumeric(target, source, out var value)) return false;
		engineValue = InspectorFormat.Float(target.ClampComponent(0, (float)value));
		return true;
	}

	private static bool TryNumericInt(FieldVM target, InspectorClipboardValue source, out string engineValue) {
		engineValue = "";
		if (!TryNumeric(target, source, out var value) || Math.Truncate(value) != value ||
		    value < int.MinValue || value > int.MaxValue)
			return false;
		var min = double.IsNegativeInfinity(target.Min) ? int.MinValue : Math.Ceiling(target.Min);
		var max = double.IsPositiveInfinity(target.Max) ? int.MaxValue : Math.Floor(target.Max);
		if (min > max) return false;
		var clamped = Math.Clamp((double)value, min, max);
		engineValue = ((int)clamped).ToString(CultureInfo.InvariantCulture);
		return true;
	}

	private static bool TryNumeric(FieldVM target, InspectorClipboardValue source, out double value) {
		value = 0;
		if (source.Kind == WidgetKind.Int) {
			if (!InspectorFormat.TryInt(source.EngineValue, out var integer)) return false;
			value = integer;
			return true;
		}
		if (source.Kind != WidgetKind.Float) return false;
		if (!InspectorFormat.TryFloat(source.EngineValue, out var floating) || !float.IsFinite(floating)) return false;
		value = floating;
		return true;
	}

	private static bool TryVector(FieldVM target, InspectorClipboardValue source, out string engineValue) {
		engineValue = "";
		if (!CompatibleVectorKinds(target.Kind, source.Kind)) return false;
		var tokens = source.EngineValue.Replace('[', ' ').Replace(']', ' ').Replace(',', ' ').Replace(';', ' ')
			.Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
		if (tokens.Length != target.ComponentCount) return false;
		var values = new float[tokens.Length];
		for (var i = 0; i < tokens.Length; i++)
			if (!InspectorFormat.TryFloat(tokens[i], out values[i]) || !float.IsFinite(values[i])) return false;
		for (var i = 0; i < values.Length; i++) values[i] = target.ClampComponent(i, values[i]);
		engineValue = string.Join(' ', values.Select(InspectorFormat.Float));
		return true;
	}

	private static bool CompatibleVectorKinds(WidgetKind target, WidgetKind source) {
		return (target, source) switch {
			(WidgetKind.Vec2, WidgetKind.Vec2) => true,
			(WidgetKind.Vec3 or WidgetKind.Color3, WidgetKind.Vec3 or WidgetKind.Color3) => true,
			(WidgetKind.Vec4 or WidgetKind.Color4, WidgetKind.Vec4 or WidgetKind.Color4) => true,
			_ => false
		};
	}

	private static bool ReferenceCompatible(FieldVM target, InspectorClipboardValue source) {
		var uid = source.EngineValue;
		if (string.IsNullOrEmpty(uid) || uid == InspectorFormat.NullUid) return true;
		if (target.Kind == WidgetKind.NodeRef) {
			if (string.IsNullOrEmpty(target.RefType)) return true;
			if (HierarchyViewModel.Current?.Find(uid) is { } node)
				return ReflectionDatabase.IsTypeOrSubtypeOf(node.Type, target.RefType);
			return string.Equals(target.RefType, source.RefType, StringComparison.Ordinal);
		}

		if (string.IsNullOrEmpty(target.RefType)) return true;
		if (AssetDatabase.TryResolve(uid, out _, out var assetType)) {
			var targetType = NormalizeAssetType(target.RefType);
			return string.Equals(targetType, assetType, StringComparison.OrdinalIgnoreCase) ||
			       string.Equals(targetType, "material", StringComparison.OrdinalIgnoreCase) &&
			       string.Equals(assetType, "material_instance", StringComparison.OrdinalIgnoreCase);
		}

		return string.Equals(target.RefType, source.RefType, StringComparison.Ordinal);
	}

	private static string NormalizeAssetType(string type) {
		return AssetTypeRegistry.ByType(type)?.Type ?? AssetTypeRegistry.ByCppTypeName(type)?.Type ?? type;
	}
}
