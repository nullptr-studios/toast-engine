//
// SettingRowViewModels.cs
// 16 Aug 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Engine;

namespace editor.Workspace;

/// <summary>
/// Base for one editable row in a settings panel
/// </summary>
/// <remarks>
/// One subclass per widget shape rather than one row with a mode flag, because the view picks its template by
/// row type - which keeps the type test in XAML's DataTemplates where Avalonia already does it, instead of in
/// a converter that has to be kept in step with the engine's Type enum
/// </remarks>
public abstract partial class SettingRow : ObservableObject {
	protected SettingRow(SettingDescriptor descriptor) {
		Descriptor = descriptor;
	}

	/// <summary>Set while Reload() assigns, so the setter does not echo the engine's own value back at it</summary>
	/// <remarks>
	/// Every row writes through on change, which is what makes the viewport respond live. Reload() is the one
	/// path that assigns a value the engine already holds - after a reset, or when the panel re-reads - and
	/// writing that back would mark the settings dirty for a change nobody made
	/// </remarks>
	protected bool SuppressWrite;

	public SettingDescriptor Descriptor { get; }

	public string Key => Descriptor.Key;
	public string Label => Descriptor.Label;
	public string Description => Descriptor.Description;
	public bool HasDescription => !string.IsNullOrWhiteSpace(Descriptor.Description);
	public bool RequiresRestart => Descriptor.RequiresRestart;

	/// <summary>Drops the active layer's override, then shows whatever was underneath it</summary>
	[RelayCommand]
	private void Reset() {
		ToastSettings.Reset(Key);
		Reload();
	}

	/// <summary>Re-reads the engine's value without writing anything back</summary>
	public abstract void Reload();
}

public sealed partial class BoolSettingRow : SettingRow {
	[ObservableProperty] private bool m_value;

	public BoolSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetBool(descriptor.Key);
	}

	partial void OnValueChanged(bool value) {
		if (SuppressWrite) return;
		ToastSettings.SetBool(Key, value);
	}

	public override void Reload() {
		SuppressWrite = true;
		Value = ToastSettings.GetBool(Key);
		SuppressWrite = false;
	}
}

public sealed partial class FloatSettingRow : SettingRow {
	[ObservableProperty] private double m_value;

	public FloatSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetFloat(descriptor.Key);
	}

	public double Minimum => Descriptor.HasRange ? Descriptor.Min : double.MinValue;
	public double Maximum => Descriptor.HasRange ? Descriptor.Max : double.MaxValue;

	/// A slider needs a tick; the engine only supplies one when the natural increment is not obvious
	public double Increment => Descriptor.Step > 0.0
		? Descriptor.Step
		: Descriptor.HasRange
			? (Descriptor.Max - Descriptor.Min) / 200.0
			: 0.01;

	public bool UseSlider => Descriptor.HasRange;

	partial void OnValueChanged(double value) {
		if (SuppressWrite) return;
		ToastSettings.SetFloat(Key, value);
	}

	public override void Reload() {
		SuppressWrite = true;
		Value = ToastSettings.GetFloat(Key);
		SuppressWrite = false;
	}
}

public sealed partial class IntSettingRow : SettingRow {
	[ObservableProperty] private decimal m_value;

	public IntSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetInt(descriptor.Key);
	}

	public decimal Minimum => Descriptor.HasRange ? (decimal)Descriptor.Min : decimal.MinValue;
	public decimal Maximum => Descriptor.HasRange ? (decimal)Descriptor.Max : decimal.MaxValue;
	public decimal Increment => Descriptor.Step > 0.0 ? (decimal)Descriptor.Step : 1m;

	partial void OnValueChanged(decimal value) {
		if (SuppressWrite) return;
		ToastSettings.SetInt(Key, (long)value);
	}

	public override void Reload() {
		SuppressWrite = true;
		Value = ToastSettings.GetInt(Key);
		SuppressWrite = false;
	}
}

/// <summary>An integer setting the engine gave a name list, shown as a combo box over that list</summary>
public sealed partial class EnumSettingRow : SettingRow {
	[ObservableProperty] private int m_selectedIndex;

	public EnumSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		Options = new ObservableCollection<string>(descriptor.Options);
		m_selectedIndex = ClampIndex((int)ToastSettings.GetInt(descriptor.Key));
	}

	public ObservableCollection<string> Options { get; }

	partial void OnSelectedIndexChanged(int value) {
		// The combo reports -1 while it rebuilds its items; writing that through would store an index no option
		// answers to, and the engine would clamp it to something the user never picked
		if (value < 0 || SuppressWrite) return;
		ToastSettings.SetInt(Key, value);
	}

	public override void Reload() {
		SuppressWrite = true;
		SelectedIndex = ClampIndex((int)ToastSettings.GetInt(Key));
		SuppressWrite = false;
	}

	private int ClampIndex(int index) {
		return Options.Count == 0 ? -1 : Math.Clamp(index, 0, Options.Count - 1);
	}
}

public sealed partial class StringSettingRow : SettingRow {
	[ObservableProperty] private string m_value;

	public StringSettingRow(SettingDescriptor descriptor) : base(descriptor) {
		m_value = ToastSettings.GetString(descriptor.Key);
	}

	partial void OnValueChanged(string value) {
		if (SuppressWrite) return;
		ToastSettings.SetString(Key, value);
	}

	public override void Reload() {
		SuppressWrite = true;
		Value = ToastSettings.GetString(Key);
		SuppressWrite = false;
	}
}

/// <summary>One collapsible group of rows, named by the engine's Meta::category</summary>
public sealed class SettingCategory {
	public required string Name { get; init; }
	public required IReadOnlyList<SettingRow> Rows { get; init; }
}

/// <summary>Builds rows from whatever the engine declared</summary>
public static class SettingRowFactory {
	/// <param name="keyPrefix">Restricts the panel to one system's settings, e.g. "renderer."</param>
	public static List<SettingCategory> BuildCategories(string keyPrefix) {
		var byCategory = new Dictionary<string, List<SettingRow>>();
		var order = new List<string>();

		foreach (var descriptor in ToastSettings.Enumerate()) {
			if (descriptor.Hidden) continue;
			if (!string.IsNullOrEmpty(keyPrefix) && !descriptor.Key.StartsWith(keyPrefix, StringComparison.Ordinal))
				continue;

			SettingRow row = descriptor switch {
				{ IsEnum: true } => new EnumSettingRow(descriptor),
				{ Type: SettingType.Bool } => new BoolSettingRow(descriptor),
				{ Type: SettingType.Int } => new IntSettingRow(descriptor),
				{ Type: SettingType.Float } => new FloatSettingRow(descriptor),
				_ => new StringSettingRow(descriptor)
			};

			if (!byCategory.TryGetValue(descriptor.Category, out var rows)) {
				rows = [];
				byCategory[descriptor.Category] = rows;
				order.Add(descriptor.Category);
			}

			rows.Add(row);
		}

		// Declaration order, not alphabetical: the engine declares display and quality before the per-effect
		// detail, which is the order someone actually wants to read them in
		var result = new List<SettingCategory>(order.Count);
		foreach (var name in order) result.Add(new SettingCategory { Name = name, Rows = byCategory[name] });
		return result;
	}
}
