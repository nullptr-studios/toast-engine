using Avalonia;
using Avalonia.Media;
using Avalonia.Styling;
using Lucide.Avalonia;

namespace editor.Git;

public static class GitStatusStyle {
	private static readonly IBrush s_fallback = new SolidColorBrush(Color.Parse("#a0a0a0"));

	public static LucideIconKind Icon(GitFileStatus status) {
		return GitChange.Strongest(status) switch {
			GitFileStatus.Added or GitFileStatus.Untracked => LucideIconKind.Plus,
			GitFileStatus.Modified => LucideIconKind.Pencil,
			GitFileStatus.Moved => LucideIconKind.ArrowRightLeft,
			GitFileStatus.Deleted => LucideIconKind.Minus,
			_ => LucideIconKind.Plus
		};
	}

	public static string BrushKey(GitFileStatus status) {
		return GitChange.Strongest(status) switch {
			GitFileStatus.Added or GitFileStatus.Untracked => "Green",
			GitFileStatus.Modified => "Orange",
			GitFileStatus.Moved => "Blue",
			GitFileStatus.Deleted => "Red",
			_ => "TextMuted"
		};
	}

	public static string Label(GitFileStatus status) {
		return GitChange.Strongest(status) switch {
			GitFileStatus.Added => "Added",
			GitFileStatus.Modified => "Modified",
			GitFileStatus.Moved => "Moved",
			GitFileStatus.Deleted => "Deleted",
			GitFileStatus.Untracked => "Untracked",
			_ => ""
		};
	}

	public static IBrush Brush(string key) {
		if (Application.Current?.TryGetResource(key, ThemeVariant.Default, out var res) == true && res is IBrush brush)
			return brush;
		return s_fallback;
	}

	public static IBrush Brush(GitFileStatus status) {
		return Brush(BrushKey(status));
	}

	public static IBrush Green => Brush("Green");
	public static IBrush Orange => Brush("Orange");
}
