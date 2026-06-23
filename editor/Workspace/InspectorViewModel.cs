using CommunityToolkit.Mvvm.ComponentModel;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public partial class InspectorViewModel : Tool {
	[ObservableProperty] private float m_posX = -12.5f;
	[ObservableProperty] private float m_mass = 3.25f;
	[ObservableProperty] private int m_count = 7;
}
