using System;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class ViewportControl : UserControl {
	private const int ActionReleased = 0;
	private const int ActionPressed = 1;

	private const int ScancodeMask = 1 << 30; // SDLK_SCANCODE_MASK

	public static readonly StyledProperty<bool> PlayModeProperty =
		AvaloniaProperty.Register<ViewportControl, bool>(nameof(PlayMode));

	private WriteableBitmap? m_bitmap;
	private bool m_captured;
	private ToastEngine? m_engine;
	private CancellationTokenSource? m_hintCts;
	private Transitions? m_hintTransitions;
	private ulong m_lastFrameId;
	private int m_surfaceH;

	private int m_surfaceW;
	private DispatcherTimer? m_timer;

	public ViewportControl() {
		InitializeComponent();

		Focusable = true;
		AttachedToVisualTree += OnAttached;
		DetachedFromVisualTree += OnDetached;
	}

	public bool PlayMode {
		get => GetValue(PlayModeProperty);
		set => SetValue(PlayModeProperty, value);
	}

	// in play mode forwarding follows the capture, otherwise plain keyboard focus
	private bool ShouldForward => PlayMode ? m_captured : IsFocused;

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property != PlayModeProperty) return;

		if (change.GetNewValue<bool>()) BeginCapture();
		else ReleaseCapture();
	}

	private void BeginCapture() {
		Focus();
		Cursor = new Cursor(StandardCursorType.None);
		m_captured = true;
		_ = ShowFocusHintAsync();
	}

	private void ReleaseCapture() {
		m_captured = false;
		Cursor = Cursor.Default;
	}

	// visible for 2s total: 1s solid, then a 1s opacity fade
	private async Task ShowFocusHintAsync() {
		m_hintCts?.Cancel();
		var cts = m_hintCts = new CancellationTokenSource();

		m_hintTransitions ??= [
			new DoubleTransition { Property = OpacityProperty, Duration = TimeSpan.FromSeconds(1) }
		];

		FocusHint.Transitions = null; // appear instantly, only the fade-out animates
		FocusHint.Opacity = 1;
		FocusHint.IsVisible = true;

		try {
			await Task.Delay(2000, cts.Token);
			FocusHint.Transitions = m_hintTransitions;
			FocusHint.Opacity = 0;
			await Task.Delay(1000, cts.Token);
			FocusHint.IsVisible = false;
		} catch (TaskCanceledException) {
			// a newer capture restarted the hint
		}
	}

	private void OnAttached(object? sender, VisualTreeAttachmentEventArgs e) {
		m_engine ??= (DataContext as WorkspaceViewModel)?.Engine;

		m_timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
		m_timer.Tick += OnTick;
		m_timer.Start();
	}

	private void OnDetached(object? sender, VisualTreeAttachmentEventArgs e) {
		if (m_timer is null) return;
		m_timer.Stop();
		m_timer.Tick -= OnTick;
		m_timer = null;
	}

	private void OnTick(object? sender, EventArgs e) {
		if (!IsEffectivelyVisible)
			return;

		m_engine ??= (DataContext as WorkspaceViewModel)?.Engine;
		if (m_engine is null)
			return;

		SendResizeIfChanged();

		var peek = m_engine.TryGetViewportFrame(IntPtr.Zero, 0, out var dims);
		if (peek == 0 || dims.width == 0 || dims.height == 0)
			return;

		if (m_bitmap is null
		    || m_bitmap.PixelSize.Width != (int)dims.width
		    || m_bitmap.PixelSize.Height != (int)dims.height)
			AllocateBitmap((int)dims.width, (int)dims.height);

		if (m_bitmap is null)
			return;

		int result;
		var changed = false;

		using (var fb = m_bitmap.Lock()) {
			var capacity = (uint)(fb.RowBytes * fb.Size.Height);
			result = m_engine.TryGetViewportFrame(fb.Address, capacity, out var info);
			if (result == 1) {
				changed = info.frame_id != m_lastFrameId;
				m_lastFrameId = info.frame_id;
			}
		}

		if (result == 1 && changed)
			Surface.InvalidateVisual();
	}

	private void AllocateBitmap(int width, int height) {
		if (width <= 0 || height <= 0)
			return;

		m_bitmap = new WriteableBitmap(
			new PixelSize(width, height), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
		Surface.Source = m_bitmap;
		m_lastFrameId = 0;
	}

	private double RenderScaling() {
		return TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
	}

	private void SendResizeIfChanged() {
		if (m_engine is null)
			return;

		var scale = RenderScaling();
		var width = Math.Max(1, (int)Math.Round(Bounds.Width * scale));
		var height = Math.Max(1, (int)Math.Round(Bounds.Height * scale));

		if (width == m_surfaceW && height == m_surfaceH)
			return;

		m_surfaceW = width;
		m_surfaceH = height;
		Events.Send(new WindowResize {
			Width = width,
			Height = height
		});
	}

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		if (!ShouldForward || m_engine is null) return;

		var p = e.GetPosition(this);
		var scale = RenderScaling();
		Events.Send(new WindowMousePosition {
			X = (float)(p.X * scale),
			Y = (float)(p.Y * scale)
		});
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);

		// clicking the viewport during play recaptures (and re-hides) the mouse
		if (PlayMode && !m_captured) BeginCapture();
		else Focus();

		if (m_engine is null) return;
		if (m_captured) e.Pointer.Capture(this); // keep drags streaming outside our bounds

		var button = ButtonFromUpdateKind(e.GetCurrentPoint(this).Properties.PointerUpdateKind);
		if (button != 0)
			Events.Send(new WindowMouseButton {
				Button = button,
				Action = ActionPressed,
				Mods = SdlMods(e.KeyModifiers)
			});
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		if (!ShouldForward || m_engine is null) return;

		var button = ButtonFromUpdateKind(e.GetCurrentPoint(this).Properties.PointerUpdateKind);
		if (button != 0)
			Events.Send(new WindowMouseButton {
				Button = button,
				Action = ActionReleased,
				Mods = SdlMods(e.KeyModifiers)
			});
	}

	protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
		base.OnPointerWheelChanged(e);
		if (!ShouldForward || m_engine is null) return;

		Events.Send(new WindowMouseScroll {
			X = (float)e.Delta.X,
			Y = (float)e.Delta.Y
		});
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		base.OnKeyDown(e);

		// backtick frees the mouse during play; never forwarded to the game
		if (PlayMode && e.Key == Key.OemTilde) {
			ReleaseCapture();
			e.Handled = true;
			return;
		}

		if (!ShouldForward || m_engine is null) return;

		var (key, _) = MapKey(e.Key);
		Events.Send(new WindowKey {
			Key = key,
			Actions = ActionPressed,
			Mods = SdlMods(e.KeyModifiers)
		});
		e.Handled = true;
	}

	protected override void OnKeyUp(KeyEventArgs e) {
		base.OnKeyUp(e);
		if (!ShouldForward || m_engine is null) return;

		var (key, _) = MapKey(e.Key);
		Events.Send(new WindowKey {
			Key = key,
			Actions = ActionReleased,
			Mods = SdlMods(e.KeyModifiers)
		});
		e.Handled = true;
	}

	protected override void OnTextInput(TextInputEventArgs e) {
		base.OnTextInput(e);
		if (!ShouldForward || m_engine is null || string.IsNullOrEmpty(e.Text)) return;

		foreach (var rune in e.Text.AsSpan().EnumerateRunes()) Events.Send(new WindowChar { Key = (uint)rune.Value });
	}

	private static int ButtonFromUpdateKind(PointerUpdateKind kind) {
		return kind switch {
			PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased => 1,
			PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased => 2,
			PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased => 3,
			_ => 0
		};
	}

	private static int SdlMods(KeyModifiers mods) {
		var result = 0;
		if (mods.HasFlag(KeyModifiers.Shift)) result |= 0x0001;
		if (mods.HasFlag(KeyModifiers.Control)) result |= 0x0040;
		if (mods.HasFlag(KeyModifiers.Alt)) result |= 0x0100;
		if (mods.HasFlag(KeyModifiers.Meta)) result |= 0x0400;
		return result;
	}

	private static (int key, int scancode) MapKey(Key k) {
		if (k is >= Key.A and <= Key.Z) {
			var offset = k - Key.A;
			return ('a' + offset, 4 + offset);
		}

		if (k is >= Key.D0 and <= Key.D9) {
			var digit = k - Key.D0;
			var scancode = digit == 0 ? 39 : 29 + digit;
			return ('0' + digit, scancode);
		}

		return k switch {
			Key.Space => (32, 44),
			Key.Enter => (13, 40),
			Key.Escape => (27, 41),
			Key.Back => (8, 42),
			Key.Tab => (9, 43),
			Key.Right => (ScancodeMask | 79, 79),
			Key.Left => (ScancodeMask | 80, 80),
			Key.Down => (ScancodeMask | 81, 81),
			Key.Up => (ScancodeMask | 82, 82),
			Key.Delete => (127, 76),
			Key.LeftShift => (ScancodeMask | 225, 225),
			Key.RightShift => (ScancodeMask | 229, 229),
			Key.LeftCtrl => (ScancodeMask | 224, 224),
			Key.RightCtrl => (ScancodeMask | 228, 228),
			Key.LeftAlt => (ScancodeMask | 226, 226),
			Key.RightAlt => (ScancodeMask | 230, 230),
			Key.F1 => (ScancodeMask | 58, 58),
			Key.F2 => (ScancodeMask | 59, 59),
			Key.F3 => (ScancodeMask | 60, 60),
			Key.F4 => (ScancodeMask | 61, 61),
			Key.F5 => (ScancodeMask | 62, 62),
			Key.F6 => (ScancodeMask | 63, 63),
			Key.F7 => (ScancodeMask | 64, 64),
			Key.F8 => (ScancodeMask | 65, 65),
			Key.F9 => (ScancodeMask | 66, 66),
			Key.F10 => (ScancodeMask | 67, 67),
			Key.F11 => (ScancodeMask | 68, 68),
			Key.F12 => (ScancodeMask | 69, 69),
			_ => (0, 0)
		};
	}
}
