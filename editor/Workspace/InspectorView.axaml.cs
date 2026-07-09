//
// Inspector.axaml.cs by Xein
// 4 Jun 2026
//

using System;
using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;

namespace editor.Workspace;

public partial class InspectorView : UserControl {
	private InspectorViewModel? m_vm;

	public InspectorView() {
		InitializeComponent();

		NameEditor.KeyDown += OnNameKeyDown;
		NameEditor.LostFocus += OnNameLostFocus;
		DataContextChanged += OnDataContextChanged;
	}

	private void OnDataContextChanged(object? sender, EventArgs e) {
		if (m_vm is not null) m_vm.PropertyChanged -= OnVmPropertyChanged;
		m_vm = DataContext as InspectorViewModel;
		if (m_vm is not null) m_vm.PropertyChanged += OnVmPropertyChanged;
	}

	// focus the rename box as soon as editing starts
	private void OnVmPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName != nameof(InspectorViewModel.IsEditingName) || m_vm?.IsEditingName != true) return;
		Dispatcher.UIThread.Post(() => {
			NameEditor.Focus();
			NameEditor.SelectAll();
		});
	}

	private void OnNameKeyDown(object? sender, KeyEventArgs e) {
		if (m_vm is null) return;
		switch (e.Key) {
			case Key.Enter:
				m_vm.CommitRename();
				e.Handled = true;
				break;
			case Key.Escape:
				m_vm.CancelRename();
				e.Handled = true;
				break;
		}
	}

	private void OnNameLostFocus(object? sender, RoutedEventArgs e) {
		m_vm?.CommitRename();
	}
}
