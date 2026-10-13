using System;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using editor.Components.Modals;
using Lucide.Avalonia;

namespace editor.Git;

public sealed class GitPassphraseModal : Window {
	private readonly TextBox m_input;

	private GitPassphraseModal(string prompt, bool rejected) {
		Title = "Git authentication";
		Width = 480;
		SizeToContent = SizeToContent.Height;
		CanResize = false;
		ShowInTaskbar = false;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		Background = GitStatusStyle.Brush("Bg2");

		var header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
		header.Children.Add(new LucideIcon {
			Kind = LucideIconKind.KeyRound, Size = 28, StrokeWidth = 2.5,
			Foreground = GitStatusStyle.Orange, VerticalAlignment = VerticalAlignment.Center
		});
		header.Children.Add(new TextBlock {
			Text = "Git needs your credentials",
			FontSize = 16,
			FontWeight = FontWeight.Bold,
			VerticalAlignment = VerticalAlignment.Center
		});

		var root = new StackPanel { Spacing = 12, Margin = new Thickness(20) };
		root.Children.Add(header);
		root.Children.Add(new TextBlock {
			Text = prompt,
			Foreground = GitStatusStyle.Brush("TextMuted"),
			TextWrapping = TextWrapping.Wrap
		});

		m_input = new TextBox();
		if (IsSecret(prompt)) m_input.PasswordChar = '•';
		m_input.KeyDown += (_, e) => {
			if (e.Key != Key.Enter) return;
			e.Handled = true;
			Close(m_input.Text ?? "");
		};
		root.Children.Add(m_input);

		if (rejected) {
			root.Children.Add(new TextBlock {
				Text = "That answer was rejected, try again",
				Foreground = GitStatusStyle.Brush("Red"),
				TextWrapping = TextWrapping.Wrap
			});
		}

		root.Children.Add(new TextBlock {
			Text = "Kept in memory until the editor closes",
			FontSize = 11,
			Foreground = GitStatusStyle.Brush("TextMuted")
		});

		var buttons = new StackPanel {
			Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right
		};
		buttons.Children.Add(MakeButton("Cancel", null, isCancel: true));
		buttons.Children.Add(MakeButton("OK", LucideIconKind.Check, isCancel: false));
		root.Children.Add(buttons);
		Content = root;
	}

	public static Task<string?> ShowAsync(string prompt, bool rejected) {
		var parent = ModalService.FindActiveWindow();
		if (parent is null) return Task.FromResult<string?>(null);
		return new GitPassphraseModal(prompt, rejected).ShowDialog<string?>(parent);
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		m_input.Focus();
	}

	// Usernames and host key confirmations stay readable
	private static bool IsSecret(string prompt) {
		return prompt.Contains("pass", StringComparison.OrdinalIgnoreCase) ||
		       prompt.Contains("PIN", StringComparison.Ordinal);
	}

	private Button MakeButton(string text, LucideIconKind? icon, bool isCancel) {
		var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
		if (icon is { } kind) content.Children.Add(new LucideIcon { Kind = kind, Size = 14, StrokeWidth = 2.5 });
		content.Children.Add(new TextBlock { Text = text });

		var button = new Button {
			Content = content,
			IsCancel = isCancel,
			Background = GitStatusStyle.Brush("Bg4"),
			CornerRadius = new CornerRadius(6),
			Padding = new Thickness(14, 6)
		};
		button.Click += (_, _) => Close(isCancel ? null : m_input.Text ?? "");
		return button;
	}
}
