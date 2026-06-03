//
// MainWindowViewModel.cs by Xein
// 12 May 2026
//

using System;
using Dock.Model.Controls;
using Dock.Model.Core;

namespace editor.MainWindow;

public partial class MainWindowViewModel : ViewModelBase {
	private ToastEngine m_toast;
	public IFactory factory { get; }
	public IRootDock layout { get; }

	public MainWindowViewModel(ToastEngine toast, IRootDock? layout = null) {
		m_toast = toast;
		factory = layout?.Factory ?? new EditorDockFactory(toast);

		if (layout is null) {
			this.layout = factory.CreateLayout();
			factory.InitLayout(this.layout);
		}
		else {
			this.layout = layout;
			if (this.layout.Factory is null) {
				this.layout.Factory = factory;
				factory.InitLayout(this.layout);
			}
		}
	}

	public void closeLayout() {
		if (layout is IDisposable disposable) {
			disposable.Dispose();
		}
	}

	public string Greeting { get; } = "Welcome to Toast Engine!";
}
