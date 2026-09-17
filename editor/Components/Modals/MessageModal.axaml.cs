using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
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
	LucideIconKind? CancelIcon = null,
	// Optional fourth button that answers No for this prompt and every remaining one in the same batch.
	// Only shown when a label is given
	string? NoAllLabel = null,
	LucideIconKind? NoAllIcon = null
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

	public bool ShowNoAll { get; init; }
	public string NoAllLabel { get; init; } = "";
	public LucideIconKind? NoAllIcon { get; init; }
	public bool HasNoAllIcon => NoAllIcon.HasValue;

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
			CancelIcon = cfg.CancelIcon,
			// The all variant only makes sense alongside the No button it repeats
			ShowNoAll = cfg.Buttons == ModalButtons.OkNoCancel && !string.IsNullOrEmpty(cfg.NoAllLabel),
			NoAllLabel = cfg.NoAllLabel ?? "",
			NoAllIcon = cfg.NoAllIcon
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

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		OkButton.Focus();
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		if (e.Key == Key.Escape && DataContext is MessageModalViewModel { ShowCancel: false }) {
			Close(null);
			e.Handled = true;
			return;
		}

		base.OnKeyDown(e);
	}

	private void OnOk(object? sender, RoutedEventArgs e) {
		Close(true);
	}

	/// <summary>
	/// True when the user answered via the All button, meaning the same answer should be applied to
	/// every remaining prompt in this batch without showing them
	/// </summary>
	public bool AppliedToAll { get; private set; }

	private void OnNo(object? sender, RoutedEventArgs e) {
		Close(false);
	}

	private void OnNoAll(object? sender, RoutedEventArgs e) {
		AppliedToAll = true;
		Close(false);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
