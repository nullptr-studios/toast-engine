//
// ProjectSettingsView.axaml.cs
// 22 Sep 2026
//

using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using editor.Settings;

namespace editor.Workspace;

public partial class ProjectSettingsView : UserControl {
	public ProjectSettingsView() {
		InitializeComponent();

		var columns = this.FindControl<Grid>("Columns")!;
		var splitter = this.FindControl<GridSplitter>("NavSplitter")!;

		AttachedToVisualTree += (_, _) => {
			columns.ColumnDefinitions[0].Width = new GridLength(SettingsPanelState.Current.NavWidth);
			(DataContext as ProjectSettingsViewModel)?.StartPolling();
		};
		splitter.DragCompleted += (_, _) => {
			SettingsPanelState.Current.NavWidth = columns.ColumnDefinitions[0].ActualWidth;
			SettingsPanelState.Current.Save();
		};
		DetachedFromVisualTree += (_, _) => (DataContext as ProjectSettingsViewModel)?.StopPolling();
	}

	private void OnNavPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is Control { DataContext: SettingsNavItem item } && DataContext is ProjectSettingsViewModel vm) vm.SelectNav(item);
	}

	private void OnNavChevronPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is Control { DataContext: SettingsNavItem item } && DataContext is ProjectSettingsViewModel vm && item.HasChildren) {
			vm.ToggleNav(item);
			e.Handled = true;
		}
	}

	private void InitializeComponent() {
		AvaloniaXamlLoader.Load(this);
	}
}
