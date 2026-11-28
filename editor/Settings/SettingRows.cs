//
// SettingRows.cs by Xein
// 24 Nov 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Engine;

namespace editor.Settings;

public abstract partial class SettingRow : ObservableObject {
	protected SettingRow(SettingDescriptor descriptor) {
		Descriptor = descriptor;
		Label = SettingsTree.LabelOf(descriptor);
		m_isOverridden = ToastSettings.IsOverridden(descriptor.Key);
	}

	protected bool SuppressWrite;

	[ObservableProperty] private string m_error = string.Empty;
	[ObservableProperty] private bool m_isOverridden;

	public SettingDescriptor Descriptor { get; }
	public string Key => Descriptor.Key;
	public string Label { get; }
	public bool RequiresRestart => Descriptor.RequiresRestart;
	public string Unit => Descriptor.Unit;
	public bool HasUnit => Descriptor.Unit.Length > 0;
	public bool HasError => Error.Length > 0;

	public string Tooltip {
		get {
			var text = new StringBuilder();
			if (Descriptor.Description.Length > 0) text.AppendLine(Descriptor.Description);
			if (Descriptor.RequiresRestart) text.AppendLine("Applies on restart");
			text.Append(Key);
			return text.ToString();
		}
	}

	partial void OnErrorChanged(string value) {
		OnPropertyChanged(nameof(HasError));
	}

	[RelayCommand]
	private void Reset() {
		ToastSettings.Reset(Key);
		Reload();
	}

	public void Reload(bool force = true) {
		if (!force && HasError) return;

		SuppressWrite = true;
		try {
			ReadValue();
			Error = string.Empty;
		} finally {
			SuppressWrite = false;
		}

		IsOverridden = ToastSettings.IsOverridden(Key);
	}

	protected abstract void ReadValue();

	protected void Written() {
		IsOverridden = ToastSettings.IsOverridden(Key);
	}

	public bool Matches(string query) {
		return Contains(Label, query) || Contains(Key, query) || Contains(Descriptor.Description, query);
	}

	private static bool Contains(string text, string query) {
		return text.Contains(query, StringComparison.OrdinalIgnoreCase);
	}

	public static SettingRow Create(SettingDescriptor descriptor) {
		if (descriptor.IsArray) return new ArraySettingRow(descriptor);

		return descriptor.ValueType switch {
			SettingType.Vec2 => new VectorSettingRow(descriptor, 2),
			SettingType.Vec3 => new VectorSettingRow(descriptor, 3),
			SettingType.Vec4 => new VectorSettingRow(descriptor, 4),
			SettingType.Quat => new VectorSettingRow(descriptor, 4),
			SettingType.Asset => new AssetSettingRow(descriptor),
			_ when descriptor.IsEnum => new ChoiceSettingRow(descriptor),
			SettingType.Bool => new BoolSettingRow(descriptor),
			SettingType.Int => new IntSettingRow(descriptor),
			SettingType.Float => new FloatSettingRow(descriptor),
			_ => new TextSettingRow(descriptor)
		};
	}
}

public sealed partial class BoolSettingRow : SettingRow {
	[ObservableProperty] private bool m_value;

	public BoolSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetBool(descriptor.Key);
	}

	partial void OnValueChanged(bool value) {
		if (SuppressWrite) return;
		ToastSettings.SetBool(Key, value);
		Written();
	}

	protected override void ReadValue() => Value = ToastSettings.GetBool(Key);
}

public sealed partial class IntSettingRow : SettingRow {
	[ObservableProperty] private int m_value;

	public IntSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = (int)ToastSettings.GetInt(descriptor.Key);
	}

	public double Minimum => Descriptor.HasRange ? Descriptor.Min : int.MinValue;

	public double Maximum => Descriptor.HasRange ? Descriptor.Max : int.MaxValue;

	partial void OnValueChanged(int value) {
		if (SuppressWrite) return;
		ToastSettings.SetInt(Key, value);
		Written();
	}

	protected override void ReadValue() => Value = (int)ToastSettings.GetInt(Key);
}

public sealed partial class FloatSettingRow : SettingRow {
	[ObservableProperty] private float m_value;

	public FloatSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = (float)ToastSettings.GetFloat(descriptor.Key);
	}

	public double Minimum => Descriptor.HasRange ? Descriptor.Min : double.NegativeInfinity;

	public double Maximum => Descriptor.HasRange ? Descriptor.Max : double.PositiveInfinity;

	partial void OnValueChanged(float value) {
		if (SuppressWrite) return;
		ToastSettings.SetFloat(Key, value);
		Written();
	}

	protected override void ReadValue() => Value = (float)ToastSettings.GetFloat(Key);
}

public sealed partial class ChoiceSettingRow : SettingRow {
	[ObservableProperty] private int m_selectedIndex;

	public ChoiceSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		Options = new ObservableCollection<string>(descriptor.Options);
		m_selectedIndex = Clamp((int)ToastSettings.GetInt(descriptor.Key));
	}

	public ObservableCollection<string> Options { get; }

	public string? SelectedName {
		get => SelectedIndex >= 0 && SelectedIndex < Options.Count ? Options[SelectedIndex] : null;
		set {
			var index = value is null ? -1 : Options.IndexOf(value);
			if (index >= 0) SelectedIndex = index;
		}
	}

	partial void OnSelectedIndexChanged(int value) {
		OnPropertyChanged(nameof(SelectedName));
		if (value < 0 || SuppressWrite) return;
		ToastSettings.SetInt(Key, value);
		Written();
	}

	protected override void ReadValue() => SelectedIndex = Clamp((int)ToastSettings.GetInt(Key));

	private int Clamp(int index) => Options.Count == 0 ? -1 : Math.Clamp(index, 0, Options.Count - 1);
}

public sealed partial class TextSettingRow : SettingRow {
	[ObservableProperty] private string m_value;

	public TextSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetString(descriptor.Key);
	}

	partial void OnValueChanged(string value) {
		if (SuppressWrite) return;
		ToastSettings.SetString(Key, value ?? string.Empty);
		Written();
	}

	protected override void ReadValue() => Value = ToastSettings.GetString(Key);
}

public sealed partial class AssetSettingRow : SettingRow {
	[ObservableProperty] private string m_value;

	public AssetSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetString(descriptor.Key);
	}

	public string AssetType => Descriptor.AssetType;

	partial void OnValueChanged(string value) {
		if (SuppressWrite) return;
		ToastSettings.SetString(Key, value ?? string.Empty);
		Written();
	}

	protected override void ReadValue() => Value = ToastSettings.GetString(Key);
}

public sealed partial class VectorSettingRow : SettingRow {
	private readonly int m_components;

	[ObservableProperty] private float m_w;
	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;
	[ObservableProperty] private float m_z;

	public VectorSettingRow(SettingDescriptor descriptor, int components) : base(descriptor) {
		m_components = components;
		Parse(ToastSettings.GetText(descriptor.Key));
	}

	public bool IsVec2 => m_components == 2;
	public bool IsVec3 => m_components == 3;
	public bool IsVec4 => m_components == 4;

	public double Minimum => Descriptor.HasRange ? Descriptor.Min : double.NegativeInfinity;
	public double Maximum => Descriptor.HasRange ? Descriptor.Max : double.PositiveInfinity;

	partial void OnXChanged(float value) => Write();
	partial void OnYChanged(float value) => Write();
	partial void OnZChanged(float value) => Write();
	partial void OnWChanged(float value) => Write();

	private void Write() {
		if (SuppressWrite) return;
		var parts = new[] { X, Y, Z, W }.Take(m_components).Select(v => v.ToString("R", CultureInfo.InvariantCulture));
		if (ToastSettings.SetText(Key, string.Join(' ', parts), out var error)) {
			Error = string.Empty;
			Written();
		} else Error = error;
	}

	protected override void ReadValue() => Parse(ToastSettings.GetText(Key));

	private void Parse(string text) {
		var numbers = text.Split(' ', StringSplitOptions.RemoveEmptyEntries)
			.Select(p => float.TryParse(p, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : 0f)
			.ToArray();

		float At(int i) => i < numbers.Length ? numbers[i] : 0f;

		var previous = SuppressWrite;
		SuppressWrite = true;
		X = At(0);
		Y = At(1);
		Z = At(2);
		W = At(3);
		SuppressWrite = previous;
	}
}

public sealed partial class ArrayItem : ObservableObject {
	[ObservableProperty] private string m_text;

	public ArrayItem(string text) {
		m_text = text;
	}
}

public sealed partial class ArraySettingRow : SettingRow {
	private string? m_lastText;

	public ArraySettingRow(SettingDescriptor descriptor) : base(descriptor) {
		Items.CollectionChanged += OnItemsChanged;
		Fill(ToastSettings.GetText(descriptor.Key));
	}

	public ObservableCollection<ArrayItem> Items { get; } = [];

	private bool IsStrings => Descriptor.ValueType == SettingType.String;

	private string NewElement => Descriptor.ValueType switch {
		SettingType.Bool => "false",
		SettingType.String => string.Empty,
		SettingType.Vec2 => "0 0",
		SettingType.Vec3 => "0 0 0",
		SettingType.Vec4 => "0 0 0 0",
		SettingType.Quat => "0 0 0 1",
		SettingType.Asset => string.Empty,
		_ => "0"
	};

	[RelayCommand]
	private void Add() {
		Items.Add(new ArrayItem(NewElement));
	}

	private void OnItemsChanged(object? sender, NotifyCollectionChangedEventArgs e) {
		if (e.OldItems is not null)
			foreach (ArrayItem item in e.OldItems) item.PropertyChanged -= OnItemChanged;
		if (e.NewItems is not null)
			foreach (ArrayItem item in e.NewItems) item.PropertyChanged += OnItemChanged;
		Commit();
	}

	private void OnItemChanged(object? sender, PropertyChangedEventArgs e) {
		Commit();
	}

	private void Commit() {
		if (SuppressWrite || m_filling) return;
		var text = string.Join(';', Items.Select(i => Encode(i.Text)));
		if (ToastSettings.SetText(Key, text, out var error)) {
			m_lastText = ToastSettings.GetText(Key);
			Error = string.Empty;
			Written();
		} else Error = error;
	}

	protected override void ReadValue() => Fill(ToastSettings.GetText(Key));

	private bool m_filling;

	private void Fill(string text) {
		if (text == m_lastText) return;
		m_lastText = text;

		m_filling = true;
		try {
			Items.Clear();
			foreach (var element in SplitElements(text)) Items.Add(new ArrayItem(Decode(element)));
		} finally {
			m_filling = false;
		}
	}

	private string Decode(string element) {
		if (!IsStrings || element.Length < 2 || element[0] != '"' || element[^1] != '"') return element;
		return element[1..^1].Replace("\\\"", "\"").Replace("\\\\", "\\");
	}

	private string Encode(string shown) {
		return IsStrings ? "\"" + shown.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"" : shown;
	}

	public static List<string> SplitElements(string text) {
		var parts = new List<string>();
		if (string.IsNullOrWhiteSpace(text)) return parts;

		var start = 0;
		var quoted = false;
		for (var i = 0; i < text.Length; i++) {
			if (text[i] == '\\' && quoted) i++;
			else if (text[i] == '"') quoted = !quoted;
			else if (text[i] == ';' && !quoted) {
				parts.Add(text[start..i].Trim());
				start = i + 1;
			}
		}

		parts.Add(text[start..].Trim());
		return parts;
	}
}
