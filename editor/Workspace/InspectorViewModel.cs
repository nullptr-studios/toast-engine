using CommunityToolkit.Mvvm.ComponentModel;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public partial class InspectorViewModel : Tool {
	[ObservableProperty] private float m_posX = -12.5f;
	[ObservableProperty] private float m_mass = 3.25f;
	[ObservableProperty] private int m_count = 7;
	[ObservableProperty] private bool m_flag = true;
	[ObservableProperty] private string m_label = "Hello world";

	[ObservableProperty] private float m_vec2X = 1.0f;
	[ObservableProperty] private float m_vec2Y = 2.0f;

	[ObservableProperty] private float m_vec3X = 1.0f;
	[ObservableProperty] private float m_vec3Y = 2.0f;
	[ObservableProperty] private float m_vec3Z = 3.0f;

	[ObservableProperty] private float m_vec4X;
	[ObservableProperty] private float m_vec4Y = 0.5f;
	[ObservableProperty] private float m_vec4Z = 0.75f;
	[ObservableProperty] private float m_vec4W = 1.0f;

	[ObservableProperty] private float m_col3R = 1.0f;
	[ObservableProperty] private float m_col3G = 0.5f;
	[ObservableProperty] private float m_col3B = 0.25f;

	[ObservableProperty] private float m_col4R = 2.5f; // > 1 for HDR
	[ObservableProperty] private float m_col4G = 0.6f;
	[ObservableProperty] private float m_col4B = 0.2f;
	[ObservableProperty] private float m_col4A = 0.5f;
}
