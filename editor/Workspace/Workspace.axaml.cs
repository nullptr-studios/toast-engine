//
// Workspace.axaml.cs by Xein
// 2 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Dock.Model.Core;
using editor.Logger;

namespace editor.Workspace;

public partial class Workspace : Window {
	private Window? m_logsWindow;
	private readonly ToastEngine? m_toast;

	public Workspace() {
		InitializeComponent();
	}

	public Workspace(ToastEngine toast) {
		InitializeComponent();
		m_toast = toast;

		DataContextChanged += OnDataContextChanged;
	}

	private void OnDataContextChanged(object? sender, EventArgs e) {
		if (DataContext is WorkspaceViewModel vm && vm.Factory is EditorDockFactory factory) {
			factory.DockableHidden += OnDockableHidden;
			factory.DockableShown += OnDockableShown;

			// Initialize menu state
			HierarchyMenuItem.IsChecked = !factory.IsHidden(factory.Hierarchy);
			InspectorMenuItem.IsChecked = !factory.IsHidden(factory.Inspector);
		}
	}

	private void OnDockableHidden(IDockable tool) {
		if (tool is HierarchyViewModel) HierarchyMenuItem.IsChecked = false;
		if (tool is InspectorViewModel) InspectorMenuItem.IsChecked = false;
	}

	private void OnDockableShown(IDockable tool) {
		if (tool is HierarchyViewModel) HierarchyMenuItem.IsChecked = true;
		if (tool is InspectorViewModel) InspectorMenuItem.IsChecked = true;
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);

		if (DataContext is WorkspaceViewModel vm) {
			vm.CloseLayout();
			if (vm.Factory is EditorDockFactory factory) {
				factory.DockableHidden -= OnDockableHidden;
				factory.DockableShown -= OnDockableShown;
			}
		}

		m_toast?.SignalClose();
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == WindowDecorationMarginProperty && MenuBorder is not null) {
			var margin = (Thickness)change.NewValue!;
			MenuBorder.Margin = new Thickness(margin.Left, 0, 0, 0);
		}
	}

	private void OnTitleBarPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
			BeginMoveDrag(e);
	}

	private void OnMinimize(object? sender, RoutedEventArgs e) {
		WindowState = WindowState.Minimized;
	}

	private void OnMaximize(object? sender, RoutedEventArgs e) {
		WindowState = WindowState == WindowState.Maximized
			? WindowState.Normal
			: WindowState.Maximized;
	}

	private void OnClose(object? sender, RoutedEventArgs e) {
		Close();
	}

	private void OnLogWindowButton(object? sender, RoutedEventArgs e) {
		if (LogWindowButton.IsChecked) {
			if (m_logsWindow is null) {
				m_logsWindow = new LogsWindow {
					DataContext = new LoggerViewModel()
				};
				m_logsWindow.Closed += (s, args) => {
					LogWindowButton.IsChecked = false;
					m_logsWindow = null;
				};
			}

			m_logsWindow.Show();
		} else {
			m_logsWindow?.Close();
		}
	}

	private void OnHierarchyMenuItem(object? sender, RoutedEventArgs e) {
		if (DataContext is WorkspaceViewModel vm && vm.Factory is EditorDockFactory factory) {
			if (HierarchyMenuItem.IsChecked)
				factory.RestoreDockable(factory.Hierarchy);
			else
				factory.CloseDockable(factory.Hierarchy);
		}
	}

	private void OnInspectorMenuItem(object? sender, RoutedEventArgs e) {
		if (DataContext is WorkspaceViewModel vm && vm.Factory is EditorDockFactory factory) {
			if (InspectorMenuItem.IsChecked)
				factory.RestoreDockable(factory.Inspector);
			else
				factory.CloseDockable(factory.Inspector);
		}
	}
}
