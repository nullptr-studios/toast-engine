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
using Avalonia.Media;
using Avalonia.Media.Transformation;
using Avalonia.Threading;
using editor.Logger;

namespace editor.Workspace;

public partial class WorkspaceView : Window {
	private readonly ToastEngine? m_toast;
	private Border? m_toastBorder;
	private CancellationTokenSource? m_toastCts;

	public WorkspaceView() {
		InitializeComponent();
		m_toastBorder = this.FindControl<Border>("ToastZoneBorder");
		DataContextChanged += OnDataContextChanged;
	}

	public WorkspaceView(ToastEngine toast) {
		InitializeComponent();
		m_toast = toast;
		m_toastBorder = this.FindControl<Border>("ToastZoneBorder");
		DataContextChanged += OnDataContextChanged;

		AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel);
		AddHandler(KeyUpEvent, OnKeyUp, RoutingStrategies.Tunnel);
	}

	private void OnDataContextChanged(object? sender, EventArgs e) {
		if (DataContext is WorkspaceViewModel vm)
			vm.PropertyChanged += OnViewModelPropertyChanged;
	}

	private async void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName != nameof(WorkspaceViewModel.ToastZoneActive)) return;
		var active = (DataContext as WorkspaceViewModel)?.ToastZoneActive ?? false;
		await AnimateToastZone(active);
	}

	private async Task AnimateToastZone(bool show) {
		if (m_toastBorder is null) return;

		m_toastCts?.Cancel();
		m_toastCts = new CancellationTokenSource();
		var ct = m_toastCts.Token;

		if (show) {
			m_toastBorder.IsVisible = true;
			// Let Avalonia render one frame at the off-screen position before sliding in
			await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Render);
			if (ct.IsCancellationRequested) return;
			m_toastBorder.RenderTransform = TransformOperations.Parse("translateY(0)");
		} else {
			m_toastBorder.RenderTransform = TransformOperations.Parse("translateY(400px)");
			try {
				await Task.Delay(220, ct); // outlast the 200ms transition
				m_toastBorder.IsVisible = false;
			} catch (OperationCanceledException) { }
		}
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);
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

	void OnKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Space) return;
		e.Handled = true;

		if (e.KeyModifiers.HasFlag(KeyModifiers.Control))
			(DataContext as WorkspaceViewModel)?.PinToastZone();
		else
			(DataContext as WorkspaceViewModel)?.ShowToastZone(true);
	}

	void OnKeyUp(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Space) return;
		e.Handled = true;

		if (!e.KeyModifiers.HasFlag(KeyModifiers.Control))
			(DataContext as WorkspaceViewModel)?.ShowToastZone(false);
	}
}
