using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace editor.Import;

public partial class ImportWindow : Window {
	public ImportWindow() {
		InitializeComponent();
		DataContext = new ImportWindowViewModel();
	}
}

