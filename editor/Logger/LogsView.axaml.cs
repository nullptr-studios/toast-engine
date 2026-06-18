using Avalonia;
using Avalonia.Controls;

namespace editor.Logger;

public partial class LogsView : UserControl {
	public LogsView() {
		InitializeComponent();
	}

	protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnAttachedToVisualTree(e);
		if (DataContext is LogsViewModel vm) vm.Start();
	}

	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnDetachedFromVisualTree(e);
		if (DataContext is LogsViewModel vm) vm.Stop();
	}
}
