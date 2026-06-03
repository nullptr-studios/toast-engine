using System;
using Avalonia;
using Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;

namespace editor.MainWindow;

internal sealed class ToastHostWindow : IHostWindow {
	private readonly ToastEngine m_toast;
	private MainWindow? m_window;
	private IRootDock? m_layout;
	private string? m_title;
	private double m_x;
	private double m_y;
	private double m_width = 800;
	private double m_height = 600;
	private DockWindowState m_state = DockWindowState.Normal;
	private bool m_pendingPresent;

	public ToastHostWindow(ToastEngine toast) {
		m_toast = toast;
	}

	public IHostWindowState? HostWindowState => null;

	public bool IsTracked { get; set; }

	public IDockWindow? Window { get; set; }

	public void Present(bool isDialog) {
		if (m_layout is null) {
			m_pendingPresent = true;
			return;
		}

		EnsureWindow();
		ShowWindow();
	}

	public void Exit() {
		m_window?.Close();
		m_window = null;
	}

	public void SetPosition(double x, double y) {
		m_x = x;
		m_y = y;
		if (m_window is { }) {
			m_window.Position = new PixelPoint((int)Math.Round(x), (int)Math.Round(y));
		}
	}

	public void GetPosition(out double x, out double y) {
		x = m_x;
		y = m_y;
	}

	public void SetSize(double width, double height) {
		m_width = width;
		m_height = height;
		if (m_window is { }) {
			m_window.Width = width;
			m_window.Height = height;
		}
	}

	public void GetSize(out double width, out double height) {
		width = m_width;
		height = m_height;
	}

	public void SetWindowState(DockWindowState windowState) {
		m_state = windowState;
		if (m_window is { }) {
			m_window.WindowState = ToAvaloniaWindowState(windowState);
		}
	}

	public DockWindowState GetWindowState() {
		if (m_window is { }) {
			m_state = ToDockWindowState(m_window.WindowState);
		}

		return m_state;
	}

	public void SetTitle(string? title) {
		m_title = title;
		if (m_window is { }) {
			m_window.Title = title;
		}
	}

	public void SetLayout(IDock layout) {
		if (layout is not IRootDock root) {
			return;
		}

		m_layout = root;
		ApplyWindowDataContext();

		if (m_pendingPresent) {
			m_pendingPresent = false;
			EnsureWindow();
			ShowWindow();
		}
	}

	public void SetActive() {
		m_window?.Activate();
	}

	private void EnsureWindow() {
		if (m_window is not null) {
			return;
		}

		if (m_layout is null) {
			return;
		}

		m_window = m_toast.createWindow(false, m_layout);
		ApplyWindowDataContext();
		ApplyPendingState();
	}

	private void ShowWindow() {
		if (m_window is null) {
			return;
		}

		if (Window is { } dockWindow) {
			dockWindow.Host = this;
		}

		m_window.Show();
		m_window.Activate();
	}

	private void ApplyWindowDataContext() {
		if (m_window is null) {
			return;
		}

		var layout = m_layout;
		m_window.DataContext = new MainWindowViewModel(m_toast, layout);
	}

	private void ApplyPendingState() {
		if (m_window is null) {
			return;
		}

		m_window.Title = m_title;
		m_window.Width = m_width;
		m_window.Height = m_height;
		m_window.Position = new PixelPoint((int)Math.Round(m_x), (int)Math.Round(m_y));
		m_window.WindowState = ToAvaloniaWindowState(m_state);
	}

	private static WindowState ToAvaloniaWindowState(DockWindowState state) {
		return state switch {
			DockWindowState.Maximized => WindowState.Maximized,
			DockWindowState.Minimized => WindowState.Minimized,
			_ => WindowState.Normal
		};
	}

	private static DockWindowState ToDockWindowState(WindowState state) {
		return state switch {
			WindowState.Maximized => DockWindowState.Maximized,
			WindowState.Minimized => DockWindowState.Minimized,
			_ => DockWindowState.Normal
		};
	}
}
