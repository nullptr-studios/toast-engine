//
// Workspace.axaml.cs by Xein
// 2 Jun 2026
//

using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using editor.Assets;
using editor.Components.Modals;
using editor.Engine;
using StartWindowNS = editor.StartWindow;

namespace editor.Workspace;

public partial class MainWindowView : Window {
	private const int StaticWindowMenuItems = 4;
	private readonly ToastEngine? m_toast;
	private readonly Border? m_toastBorder;

	private bool m_isResizing;
	private double m_resizeStartH;
	private double m_resizeStartY;
	private bool m_returningToStart;
	private bool m_closeConfirmed; // the unsaved changes were already dealt with, so closing goes straight through
	private CancellationTokenSource? m_toastCts;
	private IDisposable? m_activeWatch;
	private IDisposable? m_stateWatch;

	public MainWindowView() {
		InitializeComponent();
		m_toastBorder = this.FindControl<Border>("ToastZoneBorder");
		WireResizeHandle();
		WireWindowMenu();
		DataContextChanged += OnDataContextChanged;
	}

	public MainWindowView(ToastEngine toast) {
		InitializeComponent();
		m_toast = toast;
		m_toastBorder = this.FindControl<Border>("ToastZoneBorder");
		WireResizeHandle();
		WireWindowMenu();
		DataContextChanged += OnDataContextChanged;

		AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel);
		AddHandler(KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel);

		Activated += (_, _) => (DataContext as MainWindowViewModel)?.ReclaimEngine();

		// Every editor window counts as focus: a floating dock or a modal in use is still the editor in use
		m_activeWatch = IsActiveProperty.Changed.AddClassHandler<Window>((_, _) => QueueWindowStatePush());
		m_stateWatch = WindowStateProperty.Changed.AddClassHandler<Window>((_, _) => QueueWindowStatePush());
	}

	private void WireResizeHandle() {
		var handle = this.FindControl<Border>("ToastResizeHandle");
		if (handle is null) return;
		handle.PointerPressed += OnResizePointerPressed;
		handle.PointerMoved += OnResizePointerMoved;
		handle.PointerReleased += OnResizePointerReleased;
		handle.PointerCaptureLost += (_, _) => m_isResizing = false;
	}

	private void OnResizePointerPressed(object? sender, PointerPressedEventArgs e) {
		if (m_toastBorder is null) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_isResizing = true;
		m_resizeStartY = e.GetPosition(this).Y;
		m_resizeStartH = m_toastBorder.Height;
		e.Pointer.Capture(sender as Border);
		e.Handled = true;
	}

	private void OnResizePointerMoved(object? sender, PointerEventArgs e) {
		if (!m_isResizing || m_toastBorder is null) return;
		if (DataContext is not MainWindowViewModel vm) return;
		var delta = e.GetPosition(this).Y - m_resizeStartY;
		vm.ToastZoneHeight = Math.Clamp(m_resizeStartH - delta, 100, Bounds.Height - 100);
	}

	private void OnResizePointerReleased(object? sender, PointerReleasedEventArgs e) {
		m_isResizing = false;
		e.Pointer.Capture(null);
	}

	private void OnDataContextChanged(object? sender, EventArgs e) {
		if (DataContext is not MainWindowViewModel vm) return;
		vm.PropertyChanged += OnViewModelPropertyChanged;

		if (m_toastBorder is not null)
			m_toastBorder.RenderTransform = TransformOperations.Parse($"translateY({vm.ToastZoneHeight}px)");
	}

	private async void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName != nameof(MainWindowViewModel.ToastZoneActive)) return;
		var active = (DataContext as MainWindowViewModel)?.ToastZoneActive ?? false;
		await AnimateToastZone(active);
	}

	private async Task AnimateToastZone(bool show) {
		if (m_toastBorder is null) return;

		m_toastCts?.Cancel();
		m_toastCts = new CancellationTokenSource();
		var ct = m_toastCts.Token;

		var h = m_toastBorder.Height;

		if (show) {
			m_toastBorder.IsVisible = true;
			m_toastBorder.RenderTransform = TransformOperations.Parse($"translateY({h}px)");
			await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Render);
			if (ct.IsCancellationRequested) return;
			m_toastBorder.RenderTransform = TransformOperations.Parse("translateY(0)");
		} else {
			m_toastBorder.RenderTransform = TransformOperations.Parse($"translateY({h}px)");
			try {
				await Task.Delay(220, ct);
				m_toastBorder.IsVisible = false;
			} catch (OperationCanceledException) { }
		}
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		(DataContext as MainWindowViewModel)?.RestoreSession();
	}

	protected override void OnClosing(WindowClosingEventArgs e) {
		base.OnClosing(e);
		if (e.Cancel) return;

		/ The dialog is async, so this close is cancelled and repeated once the user has answered
		if (!m_closeConfirmed && DataContext is MainWindowViewModel vm && vm.HasUnsavedWork()) {
			e.Cancel = true;
			_ = ConfirmThenCloseAsync(vm);
			return;
		}

		(DataContext as MainWindowViewModel)?.SaveSessionLayout();
	}

	private async Task ConfirmThenCloseAsync(MainWindowViewModel vm) {
		if (!await vm.ConfirmCloseAllAsync(this)) return;
		m_closeConfirmed = true;
		Close();
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);
		m_activeWatch?.Dispose();
		m_stateWatch?.Dispose();
		(DataContext as MainWindowViewModel)?.Dispose();
		m_toast?.Dispose();
		if (m_returningToStart) ProjectContext.Reset();
	}

	// Posted: moving focus between two editor windows deactivates one before activating the other, and pushing in
	// between would drop the renderer to the background cap for a moment
	private void QueueWindowStatePush() {
		Dispatcher.UIThread.Post(PushWindowState, DispatcherPriority.Background);
	}

	private void PushWindowState() {
		var focused = Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop
			? desktop.Windows.Any(w => w.IsActive)
			: IsActive;
		// Only the main window hides the viewport; a minimized floating panel leaves the scene on screen
		ToastEngine.SetWindowState(focused, WindowState == WindowState.Minimized);
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == WindowDecorationMarginProperty && MenuBorder is not null) {
			var margin = (Thickness)change.NewValue!;
			MenuBorder.Margin = new Thickness(margin.Left, 0, 0, 0);
		}
		
		// Window state change
		if (change.Property == WindowStateProperty) {
			if (DataContext is not MainWindowViewModel vm) return;
			// vm.WindowState = WindowState;
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

	private void OnMenuMaximize(object? sender, RoutedEventArgs e) {
		WindowState = WindowState.Maximized;
	}

	private void OnMenuRestore(object? sender, RoutedEventArgs e) {
		WindowState = WindowState.Normal;
	}

	private void WireWindowMenu() {
		if (this.FindControl<MenuItem>("WindowMenu") is { } menu)
			menu.SubmenuOpened += (_, _) => RebuildWindowMenu();
	}

	private void RebuildWindowMenu() {
		MaximizeItem.IsEnabled = WindowState != WindowState.Maximized;
		RestoreItem.IsEnabled = WindowState != WindowState.Normal;

		if (DataContext is not MainWindowViewModel vm) return;

		var items = WindowMenu.Items;
		while (items.Count > StaticWindowMenuItems)
			items.RemoveAt(StaticWindowMenuItems);

		var canSwitch = !WorkspaceViewModel.AnyPlayActive;

		items.Add(MakeLayoutItem(vm, LayoutStore.DefaultName, canSwitch));
		foreach (var name in vm.LayoutNames) items.Add(MakeLayoutItem(vm, name, canSwitch));

		items.Add(new Separator());
		items.Add(new MenuItem {
			Header = "_Save Layout As...",
			Command = vm.SaveLayoutAsCommand
		});
		items.Add(new MenuItem {
			Header = "Save _Layout",
			Command = vm.SaveLayoutCommand,
			IsEnabled = vm.CanModifyActiveLayout
		});
		items.Add(new MenuItem {
			Header = "_Delete Layout...",
			Command = vm.DeleteLayoutCommand,
			CommandParameter = vm.ActiveLayoutName,
			IsEnabled = vm.CanModifyActiveLayout
		});
		items.Add(new Separator());
		items.Add(new MenuItem {
			Header = "_Reset to Default",
			Command = vm.ResetLayoutCommand,
			IsEnabled = canSwitch
		});
	}

	private static MenuItem MakeLayoutItem(MainWindowViewModel vm, string name, bool enabled) {
		return new MenuItem {
			Header = name,
			ToggleType = MenuItemToggleType.Radio,
			IsChecked = string.Equals(name, vm.ActiveLayoutName, StringComparison.Ordinal),
			IsEnabled = enabled,
			Command = vm.ApplyNamedLayoutCommand,
			CommandParameter = name
		};
	}

	private static void OpenUrl(string url) {
		Process.Start(new ProcessStartInfo { FileName = url, UseShellExecute = true });
	}

	private void OnOpenDocumentation(object? sender, RoutedEventArgs e) {
		OpenUrl("https://docs.nullptr.es");
	}

	private void OnOpenGithubRepository(object? sender, RoutedEventArgs e) {
		OpenUrl("https://github.com/nullptr-studios/toast-engine");
	}

	private void OnOpenGithubProject(object? sender, RoutedEventArgs e) {
		OpenUrl("https://github.com/orgs/nullptr-studios/projects/6");
	}

	private void OnOpenReportBug(object? sender, RoutedEventArgs e) {
		OpenUrl("https://github.com/orgs/nullptr-studios/projects/7");
	}

	private void OnOpenAbout(object? sender, RoutedEventArgs e) {
		new AboutWindow().Show(this);
	}

	private async void OnCloseProject(object? sender, RoutedEventArgs e) {
		if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop) return;

		// Asked before the start window opens
		if (DataContext is MainWindowViewModel vm) {
			if (!await vm.ConfirmCloseAllAsync(this)) return;
			m_closeConfirmed = true;
		}

		m_returningToStart = true;
		var startWindow = new StartWindowNS.StartWindow { DataContext = new StartWindowNS.StartWindowViewModel() };
		desktop.MainWindow = startWindow;
		startWindow.Show();
		Close();
	}

	private void OnQuitEditor(object? sender, RoutedEventArgs e) {
		Close();
	}

	// Typing takes priority
	private bool IsTextInputFocused() {
		return FocusManager?.GetFocusedElement() is TextBox;
	}

	private void OnKeyDown(object? sender, KeyEventArgs e) {
		if (!IsTextInputFocused() && !PlayModeShortcuts.Blocked) {
			if (e.Key == Key.Q && e.KeyModifiers == (KeyModifiers.Control | KeyModifiers.Shift)) {
				e.Handled = true;
				OnQuitEditor(null, e);
				return;
			}

			if (e.Key == Key.Q && e.KeyModifiers == KeyModifiers.Control) {
				e.Handled = true;
				OnCloseProject(null, e);
				return;
			}

			if (RunEditShortcut(e)) return;
		}

		// during play the game owns the keyboard
		// Space must reach the viewport, not the toast zone
		if (e.Key != Key.Space || IsTextInputFocused() || WorkspaceViewModel.AnyPlayActive) return;
		e.Handled = true;

		if (e.KeyModifiers.HasFlag(KeyModifiers.Control))
			(DataContext as MainWindowViewModel)?.PinToastZone();
		else
			(DataContext as MainWindowViewModel)?.ShowToastZone(true);
	}

	private bool RunEditShortcut(KeyEventArgs e) {
		if (DataContext is not MainWindowViewModel vm) return false;
		return EditShortcuts.Run(e, vm.History?.UndoCommand, vm.History?.RedoCommand, vm.Hierarchy);
	}

	private void OnKeyUp(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Space || IsTextInputFocused() || WorkspaceViewModel.AnyPlayActive) return;
		e.Handled = true;

		if (!e.KeyModifiers.HasFlag(KeyModifiers.Control))
			(DataContext as MainWindowViewModel)?.ShowToastZone(false);
	}
}
