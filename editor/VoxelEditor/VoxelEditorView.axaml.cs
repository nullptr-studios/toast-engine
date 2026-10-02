using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using CommunityToolkit.Mvvm.Input;
using editor.Workspace;

namespace editor.VoxelEditor;

public partial class VoxelEditorView : UserControl {
	private VoxelEditorViewModel? m_vm;

	public VoxelEditorView() {
		InitializeComponent();
		AddHandler(KeyDownEvent, OnShortcut, RoutingStrategies.Tunnel, true);
		AddHandler(PointerPressedEvent, (_, _) => m_vm?.EnsureEngine(), RoutingStrategies.Tunnel, true);
		DataContextChanged += (_, _) => Bind(DataContext as VoxelEditorViewModel);
	}

	private void Bind(VoxelEditorViewModel? vm) {
		if (m_vm is not null) m_vm.PropertyChanged -= OnViewModelChanged;
		m_vm = vm;
		if (m_vm is not null) m_vm.PropertyChanged += OnViewModelChanged;
		foreach (var view in new[] { FrontView, SideView, TopView }) view.Editor = vm;
		PalettePicker.Editor = vm;
		ApplyLayout();
	}

	private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName == nameof(VoxelEditorViewModel.FourUp)) ApplyLayout();
	}

	private void ApplyLayout() {
		var fourUp = m_vm?.FourUp == true;
		FrontViewHost.IsVisible = fourUp;
		SideViewHost.IsVisible = fourUp;
		TopViewHost.IsVisible = fourUp;
		Grid.SetRow(ViewportHost, fourUp ? 1 : 0);
		Grid.SetColumn(ViewportHost, fourUp ? 1 : 0);
		Grid.SetRowSpan(ViewportHost, fourUp ? 1 : 2);
		Grid.SetColumnSpan(ViewportHost, fourUp ? 1 : 2);
	}

	private static bool IsTyping(KeyEventArgs e) {
		return e.Source is TextBox;
	}

	private void OnShortcut(object? sender, KeyEventArgs e) {
		if (m_vm is not { } vm || IsTyping(e) || Viewport.IsEditorFlying) return;
		vm.EnsureEngine();

		if (e.Key == Key.S && e.KeyModifiers == KeyModifiers.Control) {
			vm.SaveCommand.Execute(null);
			e.Handled = true;
			return;
		}

		if (EditShortcuts.Run(e, new RelayCommand(vm.Undo), new RelayCommand(vm.Redo), vm.Hierarchy)) return;

		var mods = e.KeyModifiers;
		switch (e.Key) {
			case Key.Q when mods == KeyModifiers.None: vm.SetToolCommand.Execute("Select"); break;
			case Key.E when mods == KeyModifiers.None: vm.SetToolCommand.Execute("Move"); break;
			case Key.E when mods == KeyModifiers.Shift: vm.SetToolCommand.Execute("Extrude"); break;
			case Key.R when mods == KeyModifiers.None: vm.SetToolCommand.Execute("Buildup"); break;
			case Key.R when mods == KeyModifiers.Shift: vm.SetToolCommand.Execute("Carve"); break;
			case Key.T when mods == KeyModifiers.None: vm.SetToolCommand.Execute("Paint"); break;
			case Key.T when mods == KeyModifiers.Shift: vm.SetToolCommand.Execute("Bucket"); break;
			case Key.Y when mods == KeyModifiers.None: vm.SetToolCommand.Execute("Split"); break;
			case Key.Y when mods == KeyModifiers.Shift: vm.SetToolCommand.Execute("Slice"); break;
			case Key.G when mods == KeyModifiers.None: vm.ShowUnitGrid = !vm.ShowUnitGrid; break;
			case Key.G when mods == KeyModifiers.Shift: vm.ShowVoxelGrid = !vm.ShowVoxelGrid; break;
			case Key.G when mods == KeyModifiers.Alt: vm.ShowVoxelEdges = !vm.ShowVoxelEdges; break;
			case Key.Space when mods == KeyModifiers.None: vm.ShowEdges = !vm.ShowEdges; break;
			case Key.Z when mods == KeyModifiers.None: vm.FourUp = !vm.FourUp; break;
			case Key.F when mods == KeyModifiers.None: vm.ShowOthers = !vm.ShowOthers; break;
			case Key.X when mods == KeyModifiers.None: vm.RotateSelection(HoveredAxis(), 1); break;
			case Key.X when mods == KeyModifiers.Shift: vm.RotateSelection(HoveredAxis(), -1); break;
			default: return;
		}

		e.Handled = true;
	}

	private int HoveredAxis() {
		if (FrontViewHost.IsVisible && FrontView.IsPointerOver) return FrontView.DepthAxis;
		if (SideViewHost.IsVisible && SideView.IsPointerOver) return SideView.DepthAxis;
		if (TopViewHost.IsVisible && TopView.IsPointerOver) return TopView.DepthAxis;
		return 2;
	}
}
