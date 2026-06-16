using System.Collections.ObjectModel;
using Avalonia.Threading;
using Dock.Model.Mvvm.Controls;
using editor.Services;

namespace editor.Workspace;

public class HierarchyElement {
	public string Name {get; set;}
	public bool Enabled {get; set;}
	public string? Icon {get; set;}
	public string? Color {get; set;}
	public ObservableCollection<HierarchyElement> Children { get; set; } = [];

	public HierarchyElement(Proto.Events.HierarchyElement e) {
		Name = e.Name;
		Enabled = e.Enabled;
		foreach (var c in e.Children) {
			if (c is null) continue;
			Children.Add(new HierarchyElement(c));
		}
	}
}

public class HierarchyViewModel : Tool {
	private Listener m_listener;
	public ObservableCollection<HierarchyElement> Root { get; } = [];

	public HierarchyViewModel() {
		m_listener = new Listener();
		m_listener.Subscribe<Proto.Events.UpdateHierarchyData>(e => {
			Dispatcher.UIThread.Post(() => {
				Root.Clear();
				Root.Add(new HierarchyElement(e.Root));
			});
		});
	}
}
