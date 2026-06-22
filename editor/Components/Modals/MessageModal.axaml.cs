using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public enum ModalButtons { OkOnly, OkCancel, OkNoCancel }

public record ModalConfig(
	string Title,
	string Message,
	ModalButtons Buttons = ModalButtons.OkOnly,
	LucideIconKind? Icon = null,
	IBrush? IconColor = null,
	string OkLabel = "OK",
	string NoLabel = "Don't Save",
	string CancelLabel = "Cancel"
);

public class MessageModalViewModel : ObservableObject {
	public string Title { get; init; } = "";
	public string Message { get; init; } = "";
	public bool ShowIcon { get; init; }
	public LucideIconKind IconKind { get; init; }
	public IBrush IconColor { get; init; } = Brushes.White;
	public bool ShowNo { get; init; }
	public bool ShowCancel { get; init; }
	public string OkLabel { get; init; } = "OK";
	public string NoLabel { get; init; } = "Don't Save";
	public string CancelLabel { get; init; } = "Cancel";

	public static MessageModalViewModel From(ModalConfig cfg) {
		return new MessageModalViewModel {
			Title = cfg.Title,
			Message = cfg.Message,
			ShowIcon = cfg.Icon.HasValue,
			IconKind = cfg.Icon ?? LucideIconKind.Info,
			IconColor = cfg.IconColor ?? Brushes.White,
			ShowNo = cfg.Buttons == ModalButtons.OkNoCancel,
			ShowCancel = cfg.Buttons is ModalButtons.OkCancel or ModalButtons.OkNoCancel,
			OkLabel = cfg.OkLabel,
			NoLabel = cfg.NoLabel,
			CancelLabel = cfg.CancelLabel
		};
	}
}

public partial class MessageModal : Window {
	public MessageModal() {
		InitializeComponent();
	}

	public MessageModal(ModalConfig cfg) {
		InitializeComponent();
		DataContext = MessageModalViewModel.From(cfg);
	}

	private void OnOk(object? sender, RoutedEventArgs e) {
		Close(true);
	}

	private void OnNo(object? sender, RoutedEventArgs e) {
		Close(false);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
