using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using editor.Components.Modals;
using Lucide.Avalonia;

namespace editor.Git;

public enum LockedSaveChoice {
	Cancel,
	Proceed,
	Discard,
	SaveAs
}

public sealed class LockedFileModal : Window {
	private LockedFileModal(string fileName, string owner) {
		Title = "File locked";
		Width = 480;
		SizeToContent = SizeToContent.Height;
		CanResize = false;
		ShowInTaskbar = false;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		Background = GitStatusStyle.Brush("Bg2");

		var header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
		header.Children.Add(new LucideIcon {
			Kind = LucideIconKind.Lock, Size = 28, StrokeWidth = 2.5,
			Foreground = GitStatusStyle.Orange, VerticalAlignment = VerticalAlignment.Center
		});
		header.Children.Add(new TextBlock {
			Text = $"This file is locked by {owner}",
			FontSize = 16,
			FontWeight = FontWeight.Bold,
			VerticalAlignment = VerticalAlignment.Center,
			TextWrapping = TextWrapping.Wrap,
			MaxWidth = 380
		});

		var body = new TextBlock {
			Text = $"{fileName} can't be saved while {owner} holds the lock. " +
			       "Save your work as a new file, throw your changes away, or cancel and keep editing.",
			Foreground = GitStatusStyle.Brush("TextMuted"),
			TextWrapping = TextWrapping.Wrap
		};

		var buttons = new StackPanel {
			Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right
		};
		buttons.Children.Add(MakeButton("Cancel", null, null, LockedSaveChoice.Cancel));
		buttons.Children.Add(MakeButton("Discard", LucideIconKind.Trash2, GitStatusStyle.Brush("Red"),
			LockedSaveChoice.Discard));
		buttons.Children.Add(MakeButton("Save As...", LucideIconKind.Save, null, LockedSaveChoice.SaveAs));

		var root = new StackPanel { Spacing = 16, Margin = new Thickness(20) };
		root.Children.Add(header);
		root.Children.Add(body);
		root.Children.Add(buttons);
		Content = root;
	}

	public static Task<LockedSaveChoice> ShowAsync(string fileName, string owner) {
		var parent = ModalService.FindActiveWindow();
		if (parent is null) return Task.FromResult(LockedSaveChoice.Cancel);
		return new LockedFileModal(fileName, owner).ShowDialog<LockedSaveChoice>(parent);
	}

	private Button MakeButton(string text, LucideIconKind? icon, IBrush? iconBrush, LockedSaveChoice choice) {
		var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
		if (icon is { } kind)
			content.Children.Add(new LucideIcon { Kind = kind, Size = 14, StrokeWidth = 2.5, Foreground = iconBrush });
		content.Children.Add(new TextBlock { Text = text });

		var button = new Button {
			Content = content,
			Background = GitStatusStyle.Brush("Bg4"),
			CornerRadius = new CornerRadius(6),
			Padding = new Thickness(14, 6)
		};
		button.Click += (_, _) => Close(choice);
		return button;
	}
}
