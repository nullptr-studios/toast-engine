using Avalonia.Controls;

namespace editor.Assets.Importers;

public partial class CompactImportWindow : Window {
	public CompactImportWindow() {
		InitializeComponent();
	}

	public CompactImportWindow(CompactImportWindowViewModel vm) : this() {
		DataContext = vm;
		Loaded += (_, _) => vm.SetWindow(this);
	}
}
