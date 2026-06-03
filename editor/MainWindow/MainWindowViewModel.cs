//
// MainWindowViewModel.cs by Xein
// 12 May 2026
//

using System;
using Dock.Model.Controls;
using Dock.Model.Core;

namespace editor.MainWindow;

public class MainWindowViewModel : ViewModelBase {
	private ToastEngine m_toast;
	public IFactory Factory { get; }
	public IRootDock? Layout { get; }

	public MainWindowViewModel(ToastEngine toast, IRootDock? layout = null) {
		m_toast = toast;
		Factory = layout?.Factory ?? new EditorDockFactory(toast);

		if (layout is null) {
			this.Layout = Factory.CreateLayout();
			Factory.InitLayout(this.Layout ?? throw new InvalidOperationException());
		}
		else {
			this.Layout = layout;

			if (this.Layout.Factory is not null) return;
			this.Layout.Factory = Factory;
			Factory.InitLayout(this.Layout);
		}
	}

	public void CloseLayout() {
		if (Layout is IDisposable disposable) {
			disposable.Dispose();
		}
	}
}
