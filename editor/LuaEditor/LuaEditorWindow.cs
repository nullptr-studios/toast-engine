using System;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Media;

namespace editor.LuaEditor;

public sealed class LuaEditorWindow : Window {
	private static LuaEditorWindow? s_open;

	private readonly LuaGraphEditorViewModel m_vm;
	private Action? m_closed;
	private bool m_confirmed;

	public static bool IsOpen => s_open is not null;

	public static void ShowFor(LuaGraphEditorViewModel vm, Window? owner, Action? closed = null) {
		if (s_open is { } open) {
			open.m_closed = closed ?? open.m_closed;
			if (open.WindowState == WindowState.Minimized) open.WindowState = WindowState.Normal;
			open.Activate();
			return;
		}
		var window = new LuaEditorWindow(vm) { m_closed = closed };
		s_open = window;
		if (owner is not null) window.Show(owner);
		else window.Show();
	}

	public static void CloseOpen() => s_open?.Close();

	private LuaEditorWindow(LuaGraphEditorViewModel vm) {
		m_vm = vm;
		Width = 1500;
		Height = 920;
		MinWidth = 960;
		MinHeight = 560;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		if (Application.Current?.TryFindResource("Bg2", Application.Current.ActualThemeVariant, out var bg) == true && bg is IBrush brush) Background = brush;
		DataContext = vm;
		Bind(TitleProperty, new Binding(nameof(LuaGraphEditorViewModel.Title)) { StringFormat = "Lua Graph - {0}" });
		Content = new LuaGraphEditorView { DataContext = vm };
		Closing += OnClosing;
		Closed += (_, _) => {
			if (ReferenceEquals(s_open, this)) s_open = null;
			m_closed?.Invoke();
		};
	}

	private void OnClosing(object? sender, WindowClosingEventArgs e) {
		if (m_confirmed || !m_vm.IsDirty) return;
		e.Cancel = true;
		_ = ConfirmThenClose();
	}

	private async Task ConfirmThenClose() {
		if (!await m_vm.ConfirmCloseCurrentAsync()) return;
		m_confirmed = true;
		Close();
	}
}
