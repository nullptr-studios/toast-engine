using Avalonia;
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
	string CancelLabel = "Cancel",
	LucideIconKind? OkIcon = null,
	LucideIconKind? NoIcon = null,
	LucideIconKind? CancelIcon = null
);

public class MessageModalViewModel : ObservableObject {
	// Fake data for the previewer
	public MessageModalViewModel() {
		if (!Design.IsDesignMode) return;
		Title = "Unsaved Changes";
		Message = "Do you want to save changes to \"My Awesome Game\" before closing?";
		ShowIcon = true;
		IconKind = LucideIconKind.FilePen;
		IconColor = (Application.Current!.TryGetResource("Blue", null, out var r) ? r as SolidColorBrush : Brushes.Blue)!;
		ShowNo = true;
		ShowCancel = true;
		OkIcon = LucideIconKind.Save;
	}

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
	public LucideIconKind? OkIcon { get; init; }
	public LucideIconKind? NoIcon { get; init; }
	public LucideIconKind? CancelIcon { get; init; }
	public bool HasOkIcon => OkIcon.HasValue;
	public bool HasNoIcon => NoIcon.HasValue;
	public bool HasCancelIcon => CancelIcon.HasValue;

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
			CancelLabel = cfg.CancelLabel,
			OkIcon = cfg.OkIcon,
			NoIcon = cfg.NoIcon,
			CancelIcon = cfg.CancelIcon
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
