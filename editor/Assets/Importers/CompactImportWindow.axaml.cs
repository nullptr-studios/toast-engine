using Avalonia.Controls;

namespace editor.Assets.Importers;

public partial class CompactImportWindow : Window {
	public CompactImportWindow(CompactImportWindowViewModel vm) {
		InitializeComponent();
		DataContext = vm;
		Loaded += (_, _) => vm.SetWindow(this);
	}
}
