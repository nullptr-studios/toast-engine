using Avalonia.Controls;

namespace editor.Assets.Importers;

public partial class ImportWindow : Window {
	public ImportWindow() : this(null) { }

	public ImportWindow(string? destination) {
		InitializeComponent();
		DataContext = new ImportWindowViewModel(destination);
		Loaded += (_, _) => ((ImportWindowViewModel)DataContext!).SetWindow(this);
	}
}
