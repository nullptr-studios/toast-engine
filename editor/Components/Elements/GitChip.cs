using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using editor.Git;
using Lucide.Avalonia;

namespace editor.Components.Elements;

public enum GitChipKind {
	Status,
	LockMine,
	LockOther
}

public sealed class GitChip : Border {
	public static readonly StyledProperty<GitFileStatus> StatusProperty =
		AvaloniaProperty.Register<GitChip, GitFileStatus>(nameof(Status));

	public static readonly StyledProperty<GitChipKind> KindProperty =
		AvaloniaProperty.Register<GitChip, GitChipKind>(nameof(Kind));

	public static readonly StyledProperty<double> IconSizeProperty =
		AvaloniaProperty.Register<GitChip, double>(nameof(IconSize), 12);

	private readonly LucideIcon m_icon;

	static GitChip() {
		StatusProperty.Changed.AddClassHandler<GitChip>((chip, _) => chip.Update());
		KindProperty.Changed.AddClassHandler<GitChip>((chip, _) => chip.Update());
		IconSizeProperty.Changed.AddClassHandler<GitChip>((chip, _) => chip.m_icon.Size = chip.IconSize);
	}

	public GitChip() {
		CornerRadius = new CornerRadius(4);
		Padding = new Thickness(4, 3);
		VerticalAlignment = VerticalAlignment.Bottom;
		m_icon = new LucideIcon { Size = IconSize, StrokeWidth = 3, Foreground = Brushes.Black };
		Child = m_icon;
		Update();
	}

	public GitFileStatus Status {
		get => GetValue(StatusProperty);
		set => SetValue(StatusProperty, value);
	}

	public GitChipKind Kind {
		get => GetValue(KindProperty);
		set => SetValue(KindProperty, value);
	}

	public double IconSize {
		get => GetValue(IconSizeProperty);
		set => SetValue(IconSizeProperty, value);
	}

	private void Update() {
		switch (Kind) {
			case GitChipKind.LockMine:
				m_icon.Kind = LucideIconKind.Lock;
				Background = GitStatusStyle.Green;
				break;
			case GitChipKind.LockOther:
				m_icon.Kind = LucideIconKind.Lock;
				Background = GitStatusStyle.Orange;
				break;
			default:
				// Only a status chip manages its own visibility
				var status = GitChange.Strongest(Status);
				IsVisible = status != GitFileStatus.None;
				if (!IsVisible) return;
				m_icon.Kind = GitStatusStyle.Icon(status);
				Background = GitStatusStyle.Brush(status);
				break;
		}
	}
}
