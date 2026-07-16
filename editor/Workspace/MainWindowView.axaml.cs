//
// Workspace.axaml.cs by Xein
// 2 Jun 2026
//

using System;
using System.ComponentModel;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using editor.Engine;

namespace editor.Workspace;

public partial class MainWindowView : Window {
	private readonly ToastEngine? m_toast;
	private readonly Border? m_toastBorder;

	private bool m_isResizing;
	private double m_resizeStartH;
	private double m_resizeStartY;
	private CancellationTokenSource? m_toastCts;

	public MainWindowView() {
		InitializeComponent();
		m_toastBorder = this.FindControl<Border>("ToastZoneBorder");
		WireResizeHandle();
		DataContextChanged += OnDataContextChanged;
	}

	public MainWindowView(ToastEngine toast) {
		InitializeComponent();
		m_toast = toast;
		m_toastBorder = this.FindControl<Border>("ToastZoneBorder");
		WireResizeHandle();
		DataContextChanged += OnDataContextChanged;

		AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel);
		AddHandler(KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel);
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
		var delta = e.GetPosition(this).Y - m_resizeStartY;
		m_toastBorder.Height = Math.Clamp(m_resizeStartH - delta, 100, Bounds.Height - 100);
	}

	private void OnResizePointerReleased(object? sender, PointerReleasedEventArgs e) {
		m_isResizing = false;
		e.Pointer.Capture(null);
	}

	private void OnDataContextChanged(object? sender, EventArgs e) {
		if (DataContext is MainWindowViewModel vm)
			vm.PropertyChanged += OnViewModelPropertyChanged;
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

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);
		m_toast?.Dispose();
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

	// Typing takes priority
	private bool IsTextInputFocused() {
		return FocusManager?.GetFocusedElement() is TextBox;
	}

	private void OnKeyDown(object? sender, KeyEventArgs e) {
		// during play the game owns the keyboard
		// Space must reach the viewport, not the toast zone
		if (e.Key != Key.Space || IsTextInputFocused() || WorkspaceViewModel.AnyPlayActive) return;
		e.Handled = true;

		if (e.KeyModifiers.HasFlag(KeyModifiers.Control))
			(DataContext as MainWindowViewModel)?.PinToastZone();
		else
			(DataContext as MainWindowViewModel)?.ShowToastZone(true);
	}

	private void OnKeyUp(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Space || IsTextInputFocused() || WorkspaceViewModel.AnyPlayActive) return;
		e.Handled = true;

		if (!e.KeyModifiers.HasFlag(KeyModifiers.Control))
			(DataContext as MainWindowViewModel)?.ShowToastZone(false);
	}
}
