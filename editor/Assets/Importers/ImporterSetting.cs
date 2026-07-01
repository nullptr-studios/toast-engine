using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public enum SettingKind { Bool, Enum, Int, Float, String, Choice }

public sealed record ImporterSetting(
	string Label,
	SettingKind Kind,
	Func<object?> Get,
	Action<object?> Set,
	string? Tooltip = null,
	IReadOnlyList<string>? Options = null);

public partial class SettingFieldVM : ObservableObject {
	private readonly ImporterSetting m_setting;
	private bool m_initializing = true;

	[ObservableProperty] private bool m_boolValue;
	[ObservableProperty] private string m_stringValue = string.Empty;
	[ObservableProperty] private int m_intValue;
	[ObservableProperty] private float m_floatValue;

	public string Label => m_setting.Label;
	public string? Tooltip => m_setting.Tooltip;
	public IReadOnlyList<string>? Options => m_setting.Options;

	public bool IsBool => m_setting.Kind == SettingKind.Bool;
	public bool IsEnum => m_setting.Kind is SettingKind.Enum or SettingKind.Choice;
	public bool IsInt => m_setting.Kind == SettingKind.Int;
	public bool IsFloat => m_setting.Kind == SettingKind.Float;
	public bool IsString => m_setting.Kind == SettingKind.String;

	public SettingFieldVM(ImporterSetting setting) {
		m_setting = setting;
		var current = setting.Get();
		switch (current) {
			case bool b:   m_boolValue   = b;          break;
			case int i:    m_intValue    = i;          break;
			case float f:  m_floatValue  = f;          break;
			case string s: m_stringValue = s;          break;
			case Enum e:   m_stringValue = e.ToString(); break;
		}
		m_initializing = false;
	}

	partial void OnBoolValueChanged(bool value) {
		if (!m_initializing) m_setting.Set(value);
	}

	partial void OnStringValueChanged(string value) {
		if (!m_initializing && m_setting.Kind is SettingKind.Enum or SettingKind.Choice or SettingKind.String)
			m_setting.Set(value);
	}

	partial void OnIntValueChanged(int value) {
		if (!m_initializing) m_setting.Set(value);
	}

	partial void OnFloatValueChanged(float value) {
		if (!m_initializing) m_setting.Set(value);
	}
}

public partial class ImporterSettingsCardVM : ObservableObject {
	private readonly ImportTreeState m_state;
	private readonly string m_persistKey;

	[ObservableProperty] private bool m_expanded;

	public string DisplayName { get; }
	public LucideIconKind Icon { get; }
	public IReadOnlyList<SettingFieldVM> Fields { get; }

	public ImporterSettingsCardVM(IAssetImporter importer, ImportTreeState state) {
		DisplayName = importer.DisplayName;
		Icon = importer.Icon;
		m_state = state;
		m_persistKey = importer.DisplayName;
		m_expanded = !state.GetCardCollapsed(m_persistKey);
		Fields = importer.GetSettings().Select(s => new SettingFieldVM(s)).ToList();
	}

	partial void OnExpandedChanged(bool value) => m_state.SetCardCollapsed(m_persistKey, !value);
}
