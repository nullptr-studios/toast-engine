//
// PhysicsLayersViewModel.cs by Xein
// 12 Oct 2026
//

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Engine;

namespace editor.Workspace;

public sealed partial class PhysicsLayerRow : ObservableObject {
	private bool m_suppressWrite;
	[ObservableProperty] private string m_abbreviation;
	[ObservableProperty] private string m_name;

	public PhysicsLayerRow(int index) {
		Index = index;
		m_name = PhysicsLayers.Name(index);
		m_abbreviation = PhysicsLayers.Abbreviation(index);
	}

	public int Index { get; }
	public bool Editable => Index != 0;

	partial void OnNameChanged(string value) {
		if (m_suppressWrite || !Editable) return;
		ToastSettings.SetString(PhysicsLayers.NameKey(Index), value);
	}

	partial void OnAbbreviationChanged(string value) {
		if (m_suppressWrite || !Editable) return;
		if (value.Length > PhysicsLayers.MaxAbbreviationLength) {
			Abbreviation = value[..PhysicsLayers.MaxAbbreviationLength];
			return;
		}

		ToastSettings.SetString(PhysicsLayers.AbbreviationKey(Index), value);
	}

	public void Reload() {
		m_suppressWrite = true;
		Name = PhysicsLayers.Name(Index);
		Abbreviation = PhysicsLayers.Abbreviation(Index);
		m_suppressWrite = false;
	}
}

// project settings
public sealed class PhysicsLayersViewModel {
	public PhysicsLayersViewModel() {
		for (var i = 0; i < PhysicsLayers.Count; i++) Layers.Add(new PhysicsLayerRow(i));
	}

	public ObservableCollection<PhysicsLayerRow> Layers { get; } = [];

	public void Reload() {
		foreach (var layer in Layers) layer.Reload();
	}
}
