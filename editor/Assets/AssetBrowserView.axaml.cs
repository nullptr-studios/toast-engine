using System.Collections.Generic;
using System.Linq;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Styling;
using editor.Assets.Importers;
using editor.Assets.Types;
using editor.Components.Elements;
using editor.Workspace;
using Lucide.Avalonia;

namespace editor.Assets;

public partial class AssetBrowserView : UserControl {
	private const double DragThreshold = 4;

	private AssetFile? m_pressFile;
	private Point m_pressPoint;
	private PointerPressedEventArgs? m_pressArgs;

	public AssetBrowserView() {
		InitializeComponent();

		var bg = this.FindControl<Border>("AssetViewBackground");
		if (bg?.ContextMenu is { } menu) {
			menu.Opening += (_, _) => RebuildContextMenu(menu);
		}
	}

	private AssetBrowserViewModel Vm => (AssetBrowserViewModel)DataContext!;

	private void OnFilePointerPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is not Control { DataContext: AssetFile file }) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_pressFile = file;
		m_pressArgs = e;
		m_pressPoint = e.GetPosition(this);
	}

	private async void OnFilePointerMoved(object? sender, PointerEventArgs e) {
		if (m_pressFile is null || m_pressArgs is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;

		var file = m_pressFile;
		var args = m_pressArgs;
		m_pressFile = null;
		m_pressArgs = null;

		if (file.Uid is not { } uid) return;

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(AssetDragData.Format, new AssetDragRef(uid, file.Definition?.Type ?? "", file.Name)));

		// collect all selected files for multi-item drag
		var selectedFiles = AssetList.SelectedItems?
			.OfType<AssetFile>()
			.Where(f => f.Uid is not null)
			.ToList();
		if (selectedFiles is { Count: > 1 } && selectedFiles.Any(f => f.Uid == uid)) {
			var refs = selectedFiles
				.Select(f => new AssetDragRef(f.Uid!, f.Definition?.Type ?? "", f.Name))
				.ToList();
			data.Add(DataTransferItem.Create(AssetDragData.MultiFormat, refs));
		}

		await DragDrop.DoDragDropAsync(args, data, DragDropEffects.Copy | DragDropEffects.Move);
	}

	private void OnFilePointerReleased(object? sender, PointerReleasedEventArgs e) {
		m_pressFile = null;
		m_pressArgs = null;
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		if (sender is ListBox lb)
			Vm.UpdateSelection(lb.SelectedItems);
	}

	private void OnFolderDoubleTapped(object? sender, TappedEventArgs e) {
		if (sender is Border { DataContext: AssetFolder folder }) {
			Vm.SearchText = "";
			Vm.SelectedFolder = folder;
			e.Handled = true;
		}
	}

	private void OnFileDoubleTapped(object? sender, TappedEventArgs e) {
		if (sender is not Control { DataContext: AssetFile file }) return;
		if (file.Definition?.CanBeEdited != true) return;
		EditorManager.RequestOpen(file);
		e.Handled = true;
	}

	private void OnFolderDragOver(object? sender, DragEventArgs e) {
		var hasAsset = e.DataTransfer.TryGetValue(AssetDragData.MultiFormat) is not null
		               || e.DataTransfer.TryGetValue(AssetDragData.Format) is not null;
		e.DragEffects = hasAsset ? DragDropEffects.Move : DragDropEffects.None;
		e.Handled = true;
	}

	private void OnFolderDrop(object? sender, DragEventArgs e) {
		if (sender is not Control { DataContext: AssetFolder target }) return;

		if (e.DataTransfer.TryGetValue(AssetDragData.MultiFormat) is { } refs) {
			foreach (var r in refs)
				Vm.MoveAsset(r.Uid, target);
			e.Handled = true;
			return;
		}

		if (e.DataTransfer.TryGetValue(AssetDragData.Format) is not { } dragRef) return;
		Vm.MoveAsset(dragRef.Uid, target);
		e.Handled = true;
	}

	private void OnNewButtonClick(object? sender, RoutedEventArgs e) {
		var bg = this.FindControl<Border>("AssetViewBackground");
		if (bg?.ContextMenu is not { } menu) return;
		menu.DataContext = DataContext;
		menu.PlacementTarget = sender as Control ?? bg;
		menu.Open();
	}

	private async void Import_OnClick(object? sender, RoutedEventArgs e) {
		var owner = TopLevel.GetTopLevel(this) as Window;
		if (owner is null) return;
		var importWindow = new ImportWindow();
		await importWindow.ShowDialog(owner);
	}

	private void OnAssetAreaDragOver(object? sender, DragEventArgs e) {
		if (e.DataTransfer.TryGetValue(AssetDragData.Format) is not null ||
		    e.DataTransfer.TryGetValue(AssetDragData.MultiFormat) is not null) {
			e.DragEffects = DragDropEffects.None;
			return;
		}

		e.DragEffects = e.DataTransfer.Contains(DataFormat.File)
			? DragDropEffects.Copy
			: DragDropEffects.None;
		e.Handled = true;
	}

	private async void OnAssetAreaDrop(object? sender, DragEventArgs e) {
		if (!e.DataTransfer.Contains(DataFormat.File)) return;

		var storageItems = e.DataTransfer.TryGetFiles()?.ToList();
		if (storageItems is not { Count: > 0 }) return;

		var paths = new List<string>();
		foreach (var item in storageItems) {
			var local = item.TryGetLocalPath();
			if (local is not null) paths.Add(local);
		}

		if (paths.Count > 0)
			await Vm.HandleDroppedFilesAsync(paths);

		e.Handled = true;
	}

	private void RebuildContextMenu(ContextMenu menu) {
		// Keep only the first 2 static items
		while (menu.Items.Count > 2)
			menu.Items.RemoveAt(2);

		var vm = Vm;

		menu.Items.Add(new Separator());
		menu.Items.Add(MakeCommandItem("Node", vm.NewNodeCommand, "TextMuted", LucideIconKind.Circle));
		menu.Items.Add(MakeCommandItem("Node 3D", vm.NewNode3DCommand, "Red", LucideIconKind.Circle));
		menu.Items.Add(new MenuItem { Header = "Other nodes...", Command = vm.NewNodeGenericCommand });

		menu.Items.Add(new Separator());
		var materialDefinition = AssetTypeRegistry.ByExtension(".tmat");
		var luaDefinition = AssetTypeRegistry.ByExtension(".lua");
		var tomlDefinition = AssetTypeRegistry.ByExtension(".toml");
		menu.Items.Add(MakeCommandParameterItem(
			"Material",
			vm.NewAssetCommand,
			materialDefinition,
			materialDefinition?.ChipColor ?? "Green",
			materialDefinition?.Icon ?? LucideIconKind.Eclipse
			));
		menu.Items.Add(MakeCommandParameterItem(
			"Script",
			vm.NewAssetCommand,
			luaDefinition,
			luaDefinition?.ChipColor ?? "Magenta",
			luaDefinition?.Icon ?? LucideIconKind.CodeXml
			));
		menu.Items.Add(MakeCommandParameterItem(
			"Data",
			vm.NewAssetCommand,
			tomlDefinition,
			tomlDefinition?.ChipColor ?? "Cyan",
			tomlDefinition?.Icon ?? LucideIconKind.Database
			));
		foreach (var (category, types) in AssetTypeRegistry.CreatableByCategory) {
			var sub = new MenuItem { Header = category };
			foreach (var def in types)
				sub.Items.Add(new MenuItem { Header = def.DisplayName, Command = vm.NewAssetCommand, CommandParameter = def });
			menu.Items.Add(sub);
		}

		menu.Items.Add(new Separator());
		menu.Items.Add(new MenuItem { Header = "Refresh", Command = vm.RefreshCommand });
	}

	private IBrush? GetBrush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}

	private MenuItem MakeCommandItem(string label, ICommand command, string colorKey, LucideIconKind icon) {
		var chip = new Border {
			Width = 38, Height = 38,
			CornerRadius = new CornerRadius(4),
			Background = CheckerBrush.Instance,
			ClipToBounds = true,
			BorderBrush = GetBrush("Bg2"),
			BorderThickness = new Thickness(2),
		};
		chip.Child = new LucideIcon {
			Kind = icon,
			StrokeWidth = 2.5,
			Size = 20,
			Foreground = GetBrush(colorKey)
		};
		var panel = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
		panel.Children.Add(chip);
		panel.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
		return new MenuItem { Header = panel, Command = command };
	}

	private MenuItem MakeCommandParameterItem(string label, ICommand command, object? parameter,string colorKey, LucideIconKind icon) {
		var chip = new Border {
			Width = 38, Height = 38,
			CornerRadius = new CornerRadius(4),
			Background = CheckerBrush.Instance,
			ClipToBounds = true,
			BorderBrush = GetBrush("Bg2"),
			BorderThickness = new Thickness(2),
		};
		chip.Child = new LucideIcon {
			Kind = icon,
			StrokeWidth = 2.5,
			Size = 20,
			Foreground = GetBrush(colorKey)
		};
		var panel = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
		panel.Children.Add(chip);
		panel.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
		return new MenuItem { Header = panel, Command = command, CommandParameter = parameter };
	}

	private static IBrush ResolveColor(string key) {
		if (Application.Current?.TryGetResource(key, ThemeVariant.Default, out var res) == true && res is IBrush b)
			return b;
		return Brushes.Gray;
	}
}
