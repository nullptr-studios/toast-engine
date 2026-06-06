using Avalonia.Controls;

namespace editor.Import;

public partial class ImportWindow : Window {
	public ImportWindow() {
		InitializeComponent();
		DataContext = new ImportWindowViewModel();
		Loaded += (_, _) => ((ImportWindowViewModel)DataContext!).SetWindow(this);
	}
}
