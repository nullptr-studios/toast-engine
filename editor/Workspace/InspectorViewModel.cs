using CommunityToolkit.Mvvm.ComponentModel;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public partial class InspectorViewModel : Tool {
	[ObservableProperty] private float m_posX = -12.5f;
	[ObservableProperty] private float m_mass = 3.25f;
	[ObservableProperty] private int m_count = 7;

	[ObservableProperty] private float m_vec2X = 1.0f;
	[ObservableProperty] private float m_vec2Y = 2.0f;

	[ObservableProperty] private float m_vec3X = 1.0f;
	[ObservableProperty] private float m_vec3Y = 2.0f;
	[ObservableProperty] private float m_vec3Z = 3.0f;

	[ObservableProperty] private float m_vec4X;
	[ObservableProperty] private float m_vec4Y = 0.5f;
	[ObservableProperty] private float m_vec4Z = 0.75f;
	[ObservableProperty] private float m_vec4W = 1.0f;
}
