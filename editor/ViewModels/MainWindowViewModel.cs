//
// MainWindowViewModel.cs by Xein
// 12 May 2026
//

using editor.Models;

namespace editor.ViewModels;

public partial class MainWindowViewModel : ViewModelBase
{
    private ToastEngine m_toast;

    public MainWindowViewModel(ToastEngine toast)
    {
        m_toast = toast;
    }

    public string Greeting { get; } = "Welcome to Avalonia!";
}
