using System;
using System.Collections.Generic;
using System.ComponentModel;
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
	private PointerPressedEventArgs? m_pressArgs;

	private AssetFile? m_pressFile;
	private Point m_pressPoint;

	public AssetBrowserView() {
		InitializeComponent();

		AssetRepeater.AddHandler(PointerPressedEvent, OnCardPointerPressed, RoutingStrategies.Tunnel);
		AssetRepeater.AddHandler(PointerMovedEvent, OnCardPointerMoved, RoutingStrategies.Tunnel);
		AssetRepeater.AddHandler(PointerReleasedEvent, OnCardPointerReleased, RoutingStrategies.Tunnel);
		AddHandler(KeyDownEvent, OnShortcut, RoutingStrategies.Tunnel);

		var bg = this.FindControl<Border>("AssetViewBackground");
		if (bg?.ContextMenu is { } menu) menu.Opening += (_, _) => RebuildContextMenu(menu);

		AssetBrowserViewModel? subscribed = null;
		DataContextChanged += (_, _) => {
			if (subscribed is not null) subscribed.PropertyChanged -= OnViewModelPropertyChanged;
			subscribed = DataContext as AssetBrowserViewModel;
			if (subscribed is not null) subscribed.PropertyChanged += OnViewModelPropertyChanged;
			ApplyCardSize();
		};
	}

	private AssetBrowserViewModel Vm => (AssetBrowserViewModel)DataContext!;

	private void OnSelectTemplateKey(object? sender, SelectTemplateEventArgs e) {
		e.TemplateKey = e.DataContext is AssetFolder ? "folder" : "file";
	}

	private void OnCardPointerPressed(object? sender, PointerPressedEventArgs e) {
		var point = e.GetCurrentPoint(this);
		if (point.Properties.IsRightButtonPressed) {
			if (GetCardItem(e.Source) is AssetFile rightClickedFile)
				PrepareFileContextMenu(e.Source, rightClickedFile);
			return;
		}

		if (!point.Properties.IsLeftButtonPressed) return;
		if (IsTagRemoveButton(e.Source)) return;
		var item = GetCardItem(e.Source);
		if (item is null) return;
		Vm.SelectItem(item, e.KeyModifiers);
		Focus();
		if (item is AssetFile file) {
			m_pressFile = file;
			m_pressArgs = e;
			m_pressPoint = e.GetPosition(this);
		}

		e.Handled = true;
	}

	private async void OnCardPointerMoved(object? sender, PointerEventArgs e) {
		if (m_pressFile is null || m_pressArgs is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;

		var file = m_pressFile;
		var args = m_pressArgs;
		m_pressFile = null;
		m_pressArgs = null;

		if (file.Uid is not { } uid) return;

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(AssetDragData.Format,
			new AssetDragRef(uid, file.Definition?.Type ?? "", file.Name)));

		var selectedFiles = Vm.SelectedItems
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

	private void OnCardPointerReleased(object? sender, PointerReleasedEventArgs e) {
		m_pressFile = null;
		m_pressArgs = null;
	}

	private void OnCardDoubleTapped(object? sender, TappedEventArgs e) {
		var item = GetCardItem(e.Source);
		switch (item) {
			case AssetFolder folder:
				Vm.SearchText = "";
				Vm.SelectedFolder = folder;
				e.Handled = true;
				break;
			case AssetFile file when Vm.CanOpenForEditing(file):
				EditorManager.RequestOpen(file);
				e.Handled = true;
				break;
		}
	}

	private void OnCardDragOver(object? sender, DragEventArgs e) {
		if (GetCardItem(e.Source) is not AssetFolder target) {
			e.DragEffects = DragDropEffects.None;
			e.Handled = true;
			return;
		}

		var canMove = e.DataTransfer.TryGetValue(AssetDragData.MultiFormat) is { } refs
			? refs.Count > 0 && refs.All(r => Vm.CanMoveAsset(r.Uid, target))
			: e.DataTransfer.TryGetValue(AssetDragData.Format) is { } dragRef && Vm.CanMoveAsset(dragRef.Uid, target);
		e.DragEffects = canMove ? DragDropEffects.Move : DragDropEffects.None;
		e.Handled = true;
	}

	private void OnCardDrop(object? sender, DragEventArgs e) {
		if (GetCardItem(e.Source) is not AssetFolder target) return;

		if (e.DataTransfer.TryGetValue(AssetDragData.MultiFormat) is { } refs) {
			if (refs.Count == 0 || refs.Any(r => !Vm.CanMoveAsset(r.Uid, target))) return;
			foreach (var r in refs)
				Vm.MoveAsset(r.Uid, target);
			e.Handled = true;
			return;
		}

		if (e.DataTransfer.TryGetValue(AssetDragData.Format) is not { } dragRef) return;
		if (!Vm.CanMoveAsset(dragRef.Uid, target)) return;
		Vm.MoveAsset(dragRef.Uid, target);
		e.Handled = true;
	}

	private static bool IsTagRemoveButton(object? source) {
		for (var ctrl = source as Control; ctrl is not null and not ItemsRepeater; ctrl = ctrl.Parent as Control)
			if (ctrl.Classes.Contains(TagStrip.RemoveClass))
				return true;
		return false;
	}

	private void OnFileContextMenuOpening(object? sender, CancelEventArgs e) {
		if (sender is not ContextMenu menu) return;
		if (menu.Items.OfType<MenuItem>().FirstOrDefault(m => m.Name == "TagsMenuItem") is not { } tagsItem) return;
		var file = menu.PlacementTarget?.DataContext as AssetFile ?? menu.DataContext as AssetFile;
		if (file is null) {
			tagsItem.Items.Clear();
			tagsItem.IsEnabled = false;
			ToolTip.SetTip(tagsItem, "Tags are unavailable for this item");
			return;
		}

		menu.DataContext = file;
		RebuildTagsMenu(tagsItem, file);
	}

	private void OnFileContextRequested(object? sender, ContextRequestedEventArgs e) {
		if (sender is Border { DataContext: AssetFile file } card)
			PrepareFileContextMenu(card, file);
	}

	private void PrepareFileContextMenu(object? source, AssetFile file) {
		for (var control = source as Control; control is not null and not ItemsRepeater;
		     control = control.Parent as Control) {
			if (control.ContextMenu is not { } menu) continue;
			menu.DataContext = file;
			if (menu.Items.OfType<MenuItem>().FirstOrDefault(item => item.Name == "TagsMenuItem") is { } tagsItem)
				RebuildTagsMenu(tagsItem, file);
			return;
		}
	}

	private void RebuildTagsMenu(MenuItem tagsItem, AssetFile file) {
		tagsItem.Items.Clear();

		var tags = AssetBrowserSettings.Tags;
		var targets = Vm.TagTargets(file);
		if (tags.Count == 0 || targets.Count == 0) {
			tagsItem.IsEnabled = false;
			ToolTip.SetTip(tagsItem, tags.Count == 0
				? "No tags yet. Create them in Project Settings > Editor > Asset Browser"
				: "Only assets in the project databases can be tagged");
			return;
		}

		tagsItem.IsEnabled = true;
		ToolTip.SetTip(tagsItem, targets.Count > 1 ? $"Applies to the {targets.Count} selected assets" : null);

		foreach (var tag in tags) {
			var dot = new Border {
				Width = 8,
				Height = 8,
				CornerRadius = new CornerRadius(100),
				Background = tag.Brush,
				VerticalAlignment = VerticalAlignment.Center
			};
			var header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
			header.Children.Add(dot);
			header.Children.Add(new TextBlock { Text = tag.Name, VerticalAlignment = VerticalAlignment.Center });

			var item = new MenuItem {
				Header = header,
				ToggleType = MenuItemToggleType.CheckBox,
				StaysOpenOnClick = true,
				IsChecked = targets.All(t => t.TagIds.Contains(tag.Id))
			};
			item.Click += (_, _) => {
			    var current = Vm.TagTargets(file);
				var enable = !current.All(t => t.TagIds.Contains(tag.Id));
				Vm.SetTag(current, tag, enable);
				item.IsChecked = enable;
			};
			tagsItem.Items.Add(item);
		}
	}

	private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName is nameof(AssetBrowserViewModel.CardWidth) or nameof(AssetBrowserViewModel.CardHeight))
			ApplyCardSize();
	}

	private void ApplyCardSize() {
		if (DataContext is not AssetBrowserViewModel vm || AssetRepeater.Layout is not UniformGridLayout layout) return;
		layout.MinItemWidth = vm.CardWidth;
		layout.MinItemHeight = vm.CardHeight;
	}

	private static object? GetCardItem(object? source) {
		var ctrl = source as Control;
		while (ctrl is not null and not ItemsRepeater) {
			if (ctrl.DataContext is AssetFolder or AssetFile)
				return ctrl.DataContext;
			ctrl = ctrl.Parent as Control;
		}

		return null;
	}

	private void OnAssetAreaPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (!e.Handled) {
			Vm.ClearSelection();
			Focus();
		}
	}

	private void OnNewButtonClick(object? sender, RoutedEventArgs e) {
		var bg = this.FindControl<Border>("AssetViewBackground");
		if (bg?.ContextMenu is not { } menu) return;
		menu.DataContext = DataContext;
		menu.PlacementTarget = sender as Control ?? bg;
		RebuildContextMenu(menu);
		menu.Open();
	}

	private async void Import_OnClick(object? sender, RoutedEventArgs e) {
		if (!Vm.CanWriteToSelectedFolder) return;
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

		e.DragEffects = Vm.CanWriteToSelectedFolder && e.DataTransfer.Contains(DataFormat.File)
			? DragDropEffects.Copy
			: DragDropEffects.None;
		e.Handled = true;
	}

	private async void OnAssetAreaDrop(object? sender, DragEventArgs e) {
		if (!Vm.CanWriteToSelectedFolder || !e.DataTransfer.Contains(DataFormat.File)) return;

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

	private void OnShortcut(object? sender, KeyEventArgs e) {
		if (e.Source is TextBox) return;

		var command = (e.Key, e.KeyModifiers) switch {
			(Key.F2, KeyModifiers.None) => Vm.RenameCommand,
			(Key.Delete, KeyModifiers.None) => Vm.DeleteCommand,
			(Key.C, KeyModifiers.Control) => Vm.CopyCommand,
			(Key.X, KeyModifiers.Control) => Vm.CutCommand,
			(Key.V, KeyModifiers.Control) => Vm.PasteCommand,
			(Key.D, KeyModifiers.Control) => Vm.DuplicateCommand,
			_ => null
		};

		if (command is null || !command.CanExecute(null)) return;
		command.Execute(null);
		e.Handled = true;
	}

	private void RebuildContextMenu(ContextMenu menu) {
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
		foreach (var (category, types) in AssetTypeRegistry.CreatableByCategory
			         .OrderBy(entry => entry.Category, StringComparer.OrdinalIgnoreCase)) {
			var sub = new MenuItem { Header = category, IsEnabled = vm.CanWriteToSelectedFolder };
			foreach (var def in types.OrderBy(def => def.DisplayName, StringComparer.OrdinalIgnoreCase))
				sub.Items.Add(new MenuItem
					{ Header = def.DisplayName, Command = vm.NewAssetCommand, CommandParameter = def });
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
			BorderThickness = new Thickness(2)
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

	private MenuItem MakeCommandParameterItem(
		string label, ICommand command, object? parameter, string colorKey, LucideIconKind icon) {
		var chip = new Border {
			Width = 38, Height = 38,
			CornerRadius = new CornerRadius(4),
			Background = CheckerBrush.Instance,
			ClipToBounds = true,
			BorderBrush = GetBrush("Bg2"),
			BorderThickness = new Thickness(2)
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
