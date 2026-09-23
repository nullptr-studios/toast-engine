using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;

namespace editor.Logger;

public partial class LogsView : UserControl {
	private const double RowFontSize = 13;

	public LogsView() {
		InitializeComponent();
	}

	protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnAttachedToVisualTree(e);
		if (DataContext is LogsViewModel vm) {
			vm.Start();
			vm.ScrollToNewestRequested += OnScrollToNewest;
			vm.SetGlyphAdvance(MeasureGlyphAdvance());
		}
	}

	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnDetachedFromVisualTree(e);
		if (DataContext is LogsViewModel vm) vm.ScrollToNewestRequested -= OnScrollToNewest;
	}

	private static double MeasureGlyphAdvance() {
		var typeface = new Typeface(new FontFamily("FiraCode Nerd Font,Consolas,monospace"));
		var formatted = new FormattedText("0", CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
			typeface, RowFontSize, Brushes.White);
		return formatted.Width;
	}

	private void OnScrollToNewest() {
		Dispatcher.UIThread.Post(() => {
			if (DataContext is LogsViewModel { AutoScroll: true } && LogList.Scroll is { } scroll)
				scroll.Offset = scroll.Offset.WithY(0);
		}, DispatcherPriority.Background);
	}

	private void OnListPointerWheelChanged(object? sender, PointerWheelEventArgs e) {
		if (DataContext is not LogsViewModel vm) return;

		if (e.Delta.Y < 0) {
			vm.OnUserScrolled(false);
			return;
		}

		Dispatcher.UIThread.Post(() => {
			var isAtTop = LogList.Scroll is not { } scroll || scroll.Offset.Y <= 0;
			if (isAtTop) vm.OnUserScrolled(true);
		}, DispatcherPriority.Input);
	}

	private void OnSearchKeyDown(object? sender, KeyEventArgs e) {
		if (DataContext is not LogsViewModel vm) return;

		if (e.Key == Key.Escape) {
			e.Handled = true;
			if (string.IsNullOrEmpty(vm.SearchText)) TopLevel.GetTopLevel(this)?.FocusManager?.Focus(null);
			else vm.SearchText = "";
		} else if (e.Key == Key.Enter) {
			e.Handled = true;
			TopLevel.GetTopLevel(this)?.FocusManager?.Focus(null);
		}
	}

	private void OnRowDoubleTapped(object? sender, TappedEventArgs e) {
		if (sender is Control { DataContext: LogEntry entry } && DataContext is LogsViewModel vm)
			vm.OpenEntryCommand.Execute(entry);
	}
}
