//
// Viewport.axaml.cs by Xein
// 4 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;

namespace editor.MainWindow;

public partial class Viewport : UserControl {
	private ToastEngine? m_engine;
	private WriteableBitmap? m_bitmap;
	private DispatcherTimer? m_timer;
	private ulong m_lastFrameId;

	private int m_surfaceW;
	private int m_surfaceH;

	public Viewport() {
		InitializeComponent();

		Focusable = true;
		AttachedToVisualTree += OnAttached;
		DetachedFromVisualTree += OnDetached;
	}

	private void OnAttached(object? sender, VisualTreeAttachmentEventArgs e) {
		m_engine ??= (DataContext as ViewportViewModel)?.Engine;

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

	// ---- Frame pump --------------------------------------------------------------------------------

	private void OnTick(object? sender, EventArgs e) {
		m_engine ??= (DataContext as ViewportViewModel)?.Engine;
		if (m_engine is null)
			return;

		SendResizeIfChanged();
		EnsureBitmap();
		if (m_bitmap is null)
			return;

		int result;
		ToastViewportFrame info;
		bool changed = false;

		using (var fb = m_bitmap.Lock()) {
			var capacity = (uint) (fb.RowBytes * fb.Size.Height);
			result = m_engine.TryGetViewportFrame(fb.Address, capacity, out info);
			if (result == 1) {
				changed = info.frame_id != m_lastFrameId;
				m_lastFrameId = info.frame_id;
			}
		}

		switch (result) {
			case 1: {
				if (changed)
					Surface.InvalidateVisual();
				break;
			}
			case -1:
				// Engine surface size changed (e.g. after a resize); reallocate to match and pick it up next tick.
				AllocateBitmap((int) info.width, (int) info.height);
				break;
		}
	}

	private void EnsureBitmap() {
		if (m_bitmap is not null || m_engine is null)
			return;

		// Query current dimensions without copying (null dst => -1 with dims filled, when a frame exists).
		var result = m_engine.TryGetViewportFrame(IntPtr.Zero, 0, out var info);
		if (result == -1 && info.width > 0 && info.height > 0)
			AllocateBitmap((int) info.width, (int) info.height);
	}

	private void AllocateBitmap(int width, int height) {
		if (width <= 0 || height <= 0)
			return;

		m_bitmap = new WriteableBitmap(
			new PixelSize(width, height), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
		Surface.Source = m_bitmap;
		m_lastFrameId = 0;
	}

	// ---- Resize ------------------------------------------------------------------------------------

	private double RenderScaling() => TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;

	private void SendResizeIfChanged() {
		if (m_engine is null)
			return;

		var scale = RenderScaling();
		var width = Math.Max(1, (int) Math.Round(Bounds.Width * scale));
		var height = Math.Max(1, (int) Math.Round(Bounds.Height * scale));

		if (width == m_surfaceW && height == m_surfaceH)
			return;

		m_surfaceW = width;
		m_surfaceH = height;
		m_engine.SendResize(width, height);
	}

	// ---- Input forwarding (only while this viewport is selected/focused) ----------------------------

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		if (!IsFocused || m_engine is null)
			return;

		var p = e.GetPosition(this);
		var scale = RenderScaling();
		m_engine.SendMousePosition((float) (p.X * scale), (float) (p.Y * scale));
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		Focus(); // clicking selects this viewport

		if (m_engine is null)
			return;

		var button = ButtonFromUpdateKind(e.GetCurrentPoint(this).Properties.PointerUpdateKind);
		if (button != 0)
			m_engine.SendMouseButton(button, ActionPressed, SdlMods(e.KeyModifiers));
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		if (!IsFocused || m_engine is null)
			return;

		var button = ButtonFromUpdateKind(e.GetCurrentPoint(this).Properties.PointerUpdateKind);
		if (button != 0)
			m_engine.SendMouseButton(button, ActionReleased, SdlMods(e.KeyModifiers));
	}

	protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
		base.OnPointerWheelChanged(e);
		if (!IsFocused || m_engine is null)
			return;

		m_engine.SendMouseScroll((float) e.Delta.X, (float) e.Delta.Y);
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		base.OnKeyDown(e);
		if (!IsFocused || m_engine is null)
			return;

		var (key, scancode) = MapKey(e.Key);
		m_engine.SendKey(key, scancode, ActionPressed, SdlMods(e.KeyModifiers));
		e.Handled = true;
	}

	protected override void OnKeyUp(KeyEventArgs e) {
		base.OnKeyUp(e);
		if (!IsFocused || m_engine is null)
			return;

		var (key, scancode) = MapKey(e.Key);
		m_engine.SendKey(key, scancode, ActionReleased, SdlMods(e.KeyModifiers));
		e.Handled = true;
	}

	protected override void OnTextInput(TextInputEventArgs e) {
		base.OnTextInput(e);
		if (!IsFocused || m_engine is null || string.IsNullOrEmpty(e.Text))
			return;

		foreach (var rune in e.Text.AsSpan().EnumerateRunes())
			m_engine.SendChar((uint) rune.Value);
	}

	// ---- SDL value mapping (must stay in sync with the SDL window path) -----------------------------
	// Action / scancode / keycode / modifier values mirror SDL3 so engine-side consumers see identical
	// data whether input comes from the SDL window or this Avalonia viewport.

	private const int ActionReleased = 0;
	private const int ActionPressed = 1;

	private const int ScancodeMask = 1 << 30; // SDLK_SCANCODE_MASK

	private static int ButtonFromUpdateKind(PointerUpdateKind kind) => kind switch {
		PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased => 1,    // SDL_BUTTON_LEFT
		PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased => 2, // SDL_BUTTON_MIDDLE
		PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased => 3,   // SDL_BUTTON_RIGHT
		_ => 0
	};

	private static int SdlMods(KeyModifiers mods) {
		var result = 0;
		if (mods.HasFlag(KeyModifiers.Shift)) result |= 0x0001;   // SDL_KMOD_LSHIFT
		if (mods.HasFlag(KeyModifiers.Control)) result |= 0x0040; // SDL_KMOD_LCTRL
		if (mods.HasFlag(KeyModifiers.Alt)) result |= 0x0100;     // SDL_KMOD_LALT
		if (mods.HasFlag(KeyModifiers.Meta)) result |= 0x0400;    // SDL_KMOD_LGUI
		return result;
	}

	/// @brief Maps an Avalonia Key to an (SDL keycode, SDL scancode) pair. Covers the common keys;
	/// unmapped keys return (0, 0). This is the single place to extend for fuller parity.
	private static (int key, int scancode) MapKey(Key k) {
		// Letters: SDL keycode = lowercase ascii, scancode SDL_SCANCODE_A(4)..Z(29).
		if (k is >= Key.A and <= Key.Z) {
			var offset = k - Key.A;
			return ('a' + offset, 4 + offset);
		}

		// Top-row digits: keycode ascii '0'..'9'; scancode 1..9 = 30..38, 0 = 39.
		if (k is >= Key.D0 and <= Key.D9) {
			var digit = k - Key.D0;
			var scancode = digit == 0 ? 39 : 29 + digit;
			return ('0' + digit, scancode);
		}

		return k switch {
			Key.Space => (32, 44),
			Key.Enter => (13, 40),       // SDLK_RETURN
			Key.Escape => (27, 41),
			Key.Back => (8, 42),         // Backspace
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
