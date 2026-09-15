using Avalonia.Controls;
using Avalonia.Input;

namespace editor.Workspace;

public partial class SignalsView : UserControl {
	public SignalsView() {
		InitializeComponent();
	}

	private void OnSignalDoubleTapped(object? sender, TappedEventArgs e) {
		if (sender is Control { DataContext: SignalItemViewModel signal }) signal.AddConnectionCommand.Execute(null);
	}
}
