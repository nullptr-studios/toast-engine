//
// Inspector.axaml.cs by Xein
// 4 Jun 2026
//

using System.Collections.ObjectModel;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Workspace;

public partial class InspectorView : UserControl {
	public InspectorView() {
		InitializeComponent();

		// Demo data for the ArrayBox showcase rows.
		Waypoints.Items = new ObservableCollection<Vec3Item> {
			new() { X = 0f, Y = 1.5f, Z = -2f },
			new() { X = 3.25f, Y = 0f, Z = 4f },
			new() { X = -1f, Y = 2f, Z = 0.5f }
		};
		Waypoints.ItemFactory = () => new Vec3Item();

		Tags.Items = new ObservableCollection<StringItem> {
			new() { Value = "alpha" },
			new() { Value = "bravo" }
		};
		Tags.ItemFactory = () => new StringItem { Value = "new tag" };
	}
}

/// <summary>Demo list element for the Waypoints ArrayBox.</summary>
public sealed partial class Vec3Item : ObservableObject {
	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;
	[ObservableProperty] private float m_z;
}

/// <summary>Demo list element for the Tags ArrayBox.</summary>
public sealed partial class StringItem : ObservableObject {
	[ObservableProperty] private string m_value = "";
}
