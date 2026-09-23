using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Avalonia.VisualTree;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class ViewportControl : UserControl {
	private const int ActionReleased = 0;
	private const int ActionPressed = 1;

	private const int ScancodeMask = 1 << 30; // SDLK_SCANCODE_MASK

	private const double TrackpadNotchEpsilon = 1e-3;

	private const double PanScale = 40.0;
	private const float PinchZoomScale = 100f;
	private const float WheelZoomScale = 20f;

	public static readonly StyledProperty<bool> PlayModeProperty =
		AvaloniaProperty.Register<ViewportControl, bool>(nameof(PlayMode));

	private static readonly PropertyInfo? s_cursorImpl =
		typeof(Cursor).GetProperty("PlatformImpl", BindingFlags.NonPublic | BindingFlags.Instance);

	private static readonly MethodInfo? s_setCursor =
		typeof(ITopLevelImpl).GetMethod(
			"SetCursor", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);

	private ulong m_trackpadHandle;
	private int m_trackpadX = int.MinValue, m_trackpadY, m_trackpadW, m_trackpadH;

	private WriteableBitmap? m_bitmap;
	private ToastEngine? m_engine;
	private Listener? m_listener;

	private bool m_captured;
	private bool m_gameInput;
	private bool m_mouseLockRequested;
	private Point m_lockPoint;
	private Point m_lockVirtual;
	private Point m_lastPointerPoint;

	private CancellationTokenSource? m_hintCts;
	private Transitions? m_hintTransitions;
	private ulong m_lastFrameId;
	private double m_lastScale;

	// editor fly camera: RMB-drag in edit mode
	private bool m_editorFlyActive;
	private Point m_lastFlyPoint;
	private bool m_flyForward, m_flyBack, m_flyLeft, m_flyRight, m_flyUp, m_flyDown, m_flyBoost;

	private IPointer? m_pointer;
	private int m_surfaceH;

	private int m_surfaceW;

	/// <summary>
	/// Whether the per-frame callback should keep rescheduling itself
	/// </summary>
	/// <remarks>
	/// RequestAnimationFrame, not a <c>DispatcherTimer</c>. A timer made the viewport a third unsynchronized
	/// clock against the renderer and the compositor, and three rates that never divide evenly beat against
	/// each other - a frame reaching the screen after two composites, then three, then two. Capping the
	/// renderer made it worse, because a regular beat is more visible than an irregular one
	/// </remarks>
	private bool m_frameLoopActive;

	private TopLevel? m_topLevel;
	private bool m_wasVisible;

	public ViewportControl() {
		InitializeComponent();

		Focusable = true;
		AttachedToVisualTree += OnAttached;
		DetachedFromVisualTree += OnDetached;
		LostFocus += OnLostFocus;
		PointerTouchPadGestureMagnify += OnTouchpadMagnify;
	}

	public bool PlayMode {
		get => GetValue(PlayModeProperty);
		set => SetValue(PlayModeProperty, value);
	}

	public bool IsEditorFlying => m_editorFlyActive;
	private bool GameOwnsInput => PlayMode && !CanControlEditorCamera; 
	private bool ShouldForward => GameOwnsInput ? m_gameInput : IsFocused;
	private bool ShouldForwardPointer => !GameOwnsInput || m_gameInput;
	private bool MouseLocked => m_captured && !m_editorFlyActive;
	
	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property != PlayModeProperty) return;

		if (!change.GetNewValue<bool>()) {
			m_gameInput = false;
			m_mouseLockRequested = false;
			UpdateCapture();
		}
	}

	private void OnMouseLock(WindowMouseLock e) {
		if (!PlayMode) return;
		m_mouseLockRequested = e.Locked;
		if (e.Locked && GameOwnsInput && IsEffectivelyVisible) m_gameInput = true;
		UpdateCapture();
	}

	private void UpdateCapture() {
		var capture = m_editorFlyActive || (GameOwnsInput && m_gameInput && m_mouseLockRequested);
		if (capture == m_captured) return;
		if (capture) BeginCapture();
		else ReleaseCapture();
	}

	private void BeginCapture() {
		Focus();
		// hide the cursor over the whole window
		Cursor = new Cursor(StandardCursorType.None);
		if (m_topLevel is not null) m_topLevel.Cursor = Cursor;
		m_captured = true;
		// while captured every pointer event in the app routes here, so the rest of the UI
		// can't steal clicks or focus
		m_pointer?.Capture(Surface);
		ForceHiddenCursor();
		if (m_editorFlyActive) return;

		m_lockPoint = m_lastPointerPoint;
		m_lockVirtual = m_lastPointerPoint * RenderScaling();
		RecenterLockedPointer();
		_ = ShowFocusHintAsync();
	}

	private void RecenterLockedPointer() {
		if (!OperatingSystem.IsWindows()) return;
		var center = new Point(Bounds.Width / 2, Bounds.Height / 2);
		if (Math.Abs(m_lockPoint.X - center.X) < Bounds.Width / 4 &&
		    Math.Abs(m_lockPoint.Y - center.Y) < Bounds.Height / 4) return;

		var screen = this.PointToScreen(center);
		if (SetCursorPos(screen.X, screen.Y)) m_lockPoint = center;
	}

	[LibraryImport("user32.dll", EntryPoint = "SetCursorPos")]
	[return: MarshalAs(UnmanagedType.Bool)]
	private static partial bool SetCursorPos(int x, int y);

	private void ReleaseCapture() {
		m_captured = false;
		if (m_pointer?.Captured == Surface) m_pointer.Capture(null);
		Cursor = Cursor.Default;
		if (m_topLevel is not null) {
			m_topLevel.Cursor = null;
			if (m_topLevel.PlatformImpl is { } impl) s_setCursor?.Invoke(impl, [null]); // back to the arrow right away
		}
	}

	// re-asserts the hidden cursor behind the property system's back; see s_cursorImpl
	private void ForceHiddenCursor() {
		if (!m_captured || m_topLevel?.PlatformImpl is not { } impl || Cursor is not { } cursor) return;
		s_setCursor?.Invoke(impl, [s_cursorImpl?.GetValue(cursor)]);
	}

	// keeps the pointer grabbed during play; called from every pointer event
	private void TrackPointer(IPointer pointer) {
		m_pointer = pointer;
		if (m_captured && pointer.Captured != Surface) pointer.Capture(Surface);
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

		m_listener ??= new Listener();
		m_listener.SubscribeOnUiThread<WindowMouseLock>(OnMouseLock);

		// capture auto-clears on every mouse-up and events outside our bounds never reach us
		m_topLevel = TopLevel.GetTopLevel(this);
		m_topLevel?.AddHandler(PointerMovedEvent, OnTopLevelPointerMoved, RoutingStrategies.Tunnel, true);
		if (OperatingSystem.IsWindows() && TrackpadBridge.Supported &&
		    m_topLevel?.TryGetPlatformHandle() is { } platformHandle &&
		    platformHandle.HandleDescriptor == "HWND")
			m_trackpadHandle = TrackpadBridge.Create(platformHandle.Handle);

		m_frameLoopActive = true;
		ScheduleFrame();
	}

	/// <summary>Queues the next per-composite frame pickup; re-arms itself until detached.</summary>
	private void ScheduleFrame() {
		if (!m_frameLoopActive || m_topLevel is null)
			return;

		m_topLevel.RequestAnimationFrame(_ => {
			if (!m_frameLoopActive)
				return;

			OnTick(this, EventArgs.Empty);

			// Re-armed from inside the callback rather than kept running by a timer: if compositing stalls,
			// the viewport stops asking for frames instead of queueing up work nobody will display
			ScheduleFrame();
		});
	}

	private void OnDetached(object? sender, VisualTreeAttachmentEventArgs e) {
		if (m_editorFlyActive) EndEditorFly();
		ReleaseFlyKeys();
		m_gameInput = false;
		UpdateCapture();
		m_listener?.Dispose();
		m_listener = null;

		m_topLevel?.RemoveHandler(PointerMovedEvent, OnTopLevelPointerMoved);
		if (m_trackpadHandle != 0) {
			TrackpadBridge.Destroy(m_trackpadHandle);
			m_trackpadHandle = 0;
		}
		m_topLevel = null;

		// Stops the callback re-arming; any already-queued one returns immediately
		m_frameLoopActive = false;
	}

	private void OnTopLevelPointerMoved(object? sender, PointerEventArgs e) {
		TrackPointer(e.Pointer);
		ForceHiddenCursor();
	}

	private void OnTick(object? sender, EventArgs e) {
		// re-grab the pointer even while the mouse sits still
		if (m_captured && m_pointer is { } pointer && pointer.Captured != Surface) {
			pointer.Capture(Surface);
			ForceHiddenCursor();
		}

		if (!IsEffectivelyVisible) {
			HideTrackpadViewport();
			m_wasVisible = false;
			return;
		}

		UpdateTrackpadViewport();
		PollTrackpadGestures();

		if (!m_wasVisible) {
			m_wasVisible = true;
			m_surfaceW = 0;
			m_surfaceH = 0;
		}

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

	private bool CanControlEditorCamera => DataContext is WorkspaceViewModel { GameCamera: false };

	private void SendEditorCameraGesture(float dx, float dy, float zoom) {
		if (!CanControlEditorCamera || m_engine is null) return;
		Events.Send(new EditorCameraGesture { Dx = dx, Dy = dy, Zoom = zoom });
	}

	private void PollTrackpadGestures() {
		if (m_trackpadHandle == 0) return;
		TrackpadBridge.Update(m_trackpadHandle);

		if (!CanControlEditorCamera) return;
		if (!TrackpadBridge.Drain(m_trackpadHandle, out var gesture)) return;

		var sign = gesture.Inverted != 0 ? 1f : -1f;
		SendEditorCameraGesture(gesture.PanX * sign, gesture.PanY * sign, gesture.Zoom);
	}

	private void UpdateTrackpadViewport() {
		if (m_trackpadHandle == 0 || m_topLevel is null) return;
		var origin = this.TranslatePoint(default, m_topLevel);
		if (origin is null) return;

		var scale = RenderScaling();
		var x = (int)Math.Round(origin.Value.X * scale);
		var y = (int)Math.Round(origin.Value.Y * scale);
		var width = Math.Max(1, (int)Math.Round(Bounds.Width * scale));
		var height = Math.Max(1, (int)Math.Round(Bounds.Height * scale));
		if (x == m_trackpadX && y == m_trackpadY && width == m_trackpadW && height == m_trackpadH) return;

		m_trackpadX = x;
		m_trackpadY = y;
		m_trackpadW = width;
		m_trackpadH = height;
		TrackpadBridge.SetRect(m_trackpadHandle, x, y, width, height);
	}

	private void HideTrackpadViewport() {
		if (m_trackpadHandle == 0 || m_trackpadX == int.MinValue) return;
		m_trackpadX = int.MinValue;
		TrackpadBridge.SetRect(m_trackpadHandle, -32000, -32000, 1, 1);
	}

	private void OnTouchpadMagnify(object? sender, PointerDeltaEventArgs e) {
		if (!CanControlEditorCamera) return;
		SendEditorCameraGesture(0f, 0f, (float)(e.Delta.X != 0 ? e.Delta.X : e.Delta.Y) * PinchZoomScale);
		e.Handled = true;
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

		if (Math.Abs(scale - m_lastScale) > 1e-6) {
			m_lastScale = scale;
			Events.Send(new WindowDisplayScale { Scale = (float)scale });
		}

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

	protected override void OnPointerEntered(PointerEventArgs e) {
		base.OnPointerEntered(e);
		TrackPointer(e.Pointer);
	}

	private void OnLostFocus(object? sender, RoutedEventArgs e) {
		if (MouseLocked)
			Dispatcher.UIThread.Post(() => {
				if (MouseLocked) Focus();
			});
		else if (GameOwnsInput) m_gameInput = false;

		if (m_editorFlyActive) EndEditorFly();
		ReleaseFlyKeys();
	}

	// RMB released, focus lost, or the control detached mid-drag
	private void EndEditorFly() {
		m_editorFlyActive = false;
		UpdateCapture();
		if (m_engine is not null) Events.Send(new EditorCameraFlyMode { Active = false });

		if (!m_flyUp && !m_flyDown) return;
		m_flyUp = m_flyDown = false;
		SendFlyMoveState();
	}

	private void ReleaseFlyKeys() {
		if (!m_flyForward && !m_flyBack && !m_flyLeft && !m_flyRight && !m_flyUp && !m_flyDown && !m_flyBoost) return;
		m_flyForward = m_flyBack = m_flyLeft = m_flyRight = m_flyUp = m_flyDown = m_flyBoost = false;
		SendFlyMoveState();
	}

	private void SendFlyMoveState() {
		if (m_engine is null) return;
		Events.Send(new EditorCameraMoveState {
			Forward = m_flyForward,
			Back = m_flyBack,
			Left = m_flyLeft,
			Right = m_flyRight,
			Up = m_flyUp,
			Down = m_flyDown,
			Boost = m_flyBoost
		});
	}

	private bool HandleFlyKey(KeyEventArgs e, bool pressed) {
		if (pressed && !CanMoveEditorCamera(e)) return false;

		bool held;
		switch (e.Key) {
			case Key.W: held = m_flyForward; m_flyForward = pressed; break;
			case Key.S: held = m_flyBack; m_flyBack = pressed; break;
			case Key.A: held = m_flyLeft; m_flyLeft = pressed; break;
			case Key.D: held = m_flyRight; m_flyRight = pressed; break;
			case Key.E when m_editorFlyActive || m_flyUp: held = m_flyUp; m_flyUp = pressed; break;
			case Key.Q when m_editorFlyActive || m_flyDown: held = m_flyDown; m_flyDown = pressed; break;
			case Key.LeftShift or Key.RightShift: held = m_flyBoost; m_flyBoost = pressed; break;
			default: return false;
		}

		if (!pressed && !held) return false;
		SendFlyMoveState();
		return true;
	}

	private bool CanMoveEditorCamera(KeyEventArgs e) {
		if (!CanControlEditorCamera) return false;
		return m_editorFlyActive || (e.KeyModifiers & (KeyModifiers.Control | KeyModifiers.Alt | KeyModifiers.Meta)) == 0;
	}

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		TrackPointer(e.Pointer);

		var scale = RenderScaling();
		var point = e.GetPosition(this);

		if (m_editorFlyActive) {
			var dx = (float)((point.X - m_lastFlyPoint.X) * scale);
			var dy = (float)((point.Y - m_lastFlyPoint.Y) * scale);
			m_lastFlyPoint = point;
			if (m_engine is not null && (dx != 0f || dy != 0f))
				Events.Send(new EditorCameraLook { Dx = dx, Dy = dy });
			return;
		}

		if (MouseLocked) {
			var delta = point - m_lockPoint;
			m_lockPoint = point;
			if (delta == default) return; // the recenter warp landing
			m_lockVirtual += delta * scale;
			if (m_engine is not null)
				Events.Send(new WindowMousePosition { X = (float)m_lockVirtual.X, Y = (float)m_lockVirtual.Y });
			RecenterLockedPointer();
			return;
		}

		m_lastPointerPoint = point;
		if (!ShouldForwardPointer || m_engine is null) return;
		SendMousePosition(point);
	}

	private void SendMousePosition(Point point) {
		var scale = RenderScaling();
		Events.Send(new WindowMousePosition {
			X = (float)(Math.Clamp(point.X, 0, Bounds.Width) * scale),
			Y = (float)(Math.Clamp(point.Y, 0, Bounds.Height) * scale)
		});
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);

		var button = ButtonFromUpdateKind(e.GetCurrentPoint(this).Properties.PointerUpdateKind);

		// RMB in edit mode starts the fly camera
		if (CanControlEditorCamera && button == 3) {
			m_editorFlyActive = true;
			m_lastFlyPoint = e.GetPosition(this);
			UpdateCapture();
			TrackPointer(e.Pointer);
			if (m_engine is not null) Events.Send(new EditorCameraFlyMode { Active = true });
			return;
		}

		if (GameOwnsInput) m_gameInput = true;
		if (!MouseLocked) m_lastPointerPoint = e.GetPosition(this);
		Focus();
		UpdateCapture();

		TrackPointer(e.Pointer);
		if (m_engine is null) return;

		if (ShouldForwardPointer && !MouseLocked) SendMousePosition(e.GetPosition(this));
		if (button != 0)
			Events.Send(new WindowMouseButton {
				Button = button,
				Action = ActionPressed,
				Mods = SdlMods(e.KeyModifiers)
			});
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		TrackPointer(e.Pointer);

		var button = ButtonFromUpdateKind(e.GetCurrentPoint(this).Properties.PointerUpdateKind);

		if (m_editorFlyActive && button == 3) {
			EndEditorFly();
			return;
		}

		if (!ShouldForward || m_engine is null) return;

		if (button != 0)
			Events.Send(new WindowMouseButton {
				Button = button,
				Action = ActionReleased,
				Mods = SdlMods(e.KeyModifiers)
			});
	}

	protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
		base.OnPointerWheelChanged(e);

		if (m_trackpadHandle != 0) {
			if (TrackpadBridge.Active(m_trackpadHandle)) {
				e.Handled = true;
				return;
			}
		} else if (CanControlEditorCamera && e.KeyModifiers.HasFlag(KeyModifiers.Control)) {
			SendEditorCameraGesture(0f, 0f, (float)e.Delta.Y * PinchZoomScale);
			e.Handled = true;
			return;
		} else if (CanControlEditorCamera && (e.Delta.X != 0 || Math.Abs(e.Delta.Y % 1.0) > TrackpadNotchEpsilon)) {
			SendEditorCameraGesture((float)(e.Delta.X * PanScale), (float)(e.Delta.Y * PanScale), 0f);
			e.Handled = true;
			return;
		}

		if (CanControlEditorCamera && DataContext is WorkspaceViewModel { CameraMode: CameraMode.Orbit }) {
			SendEditorCameraGesture(0f, 0f, (float)e.Delta.Y * WheelZoomScale);
			e.Handled = true;
			return;
		}

		// scrolling while flying adjusts fly speed
		if (m_editorFlyActive) {
			if (DataContext is WorkspaceViewModel vm) vm.StepCameraSpeedFromWheel(e.Delta.Y);
			e.Handled = true;
			return;
		}

		if (!ShouldForward || m_engine is null) return;

		Events.Send(new WindowMouseScroll {
			X = (float)e.Delta.X,
			Y = (float)e.Delta.Y
		});
	}

	// Ctrl/Meta combos in edit mode are editor shortcuts
	private bool IsEditorShortcut(KeyEventArgs e) {
		return !PlayMode && (e.KeyModifiers.HasFlag(KeyModifiers.Control) || e.KeyModifiers.HasFlag(KeyModifiers.Meta));
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		base.OnKeyDown(e);

		// GPU capture (RenderDoc or Nsight Graphics); always intercepted locally, never forwarded to the
		// game. Not gated on RenderDocDetector.IsAttached - the native side already no-ops if neither
		// RenderDoc nor an injected Nsight Graphics Capture activity is present, and gating here on a
		// RenderDoc-only check meant F12 silently did nothing for a Nsight-only session
		if (e.Key == Key.F12) {
			if (m_engine is not null) Events.Send(new CaptureFrame());
			e.Handled = true;
			return;
		}

		// backtick frees the mouse during play; never forwarded to the game
		if (PlayMode && e.Key == Key.OemTilde) {
			m_gameInput = false;
			UpdateCapture();
			e.Handled = true;
			return;
		}

		if (HandleFlyKey(e, true)) {
			e.Handled = true;
			return;
		}

		if (PlayMode && ShouldForward && e.Key == Key.V &&
		    (e.KeyModifiers.HasFlag(KeyModifiers.Control) || e.KeyModifiers.HasFlag(KeyModifiers.Meta))) {
			e.Handled = true;
			_ = PasteClipboardAsync();
			return;
		}

		if (IsEditorShortcut(e)) return;
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

		// matches the OnKeyDown intercept - F12 down is never forwarded, so its key-up shouldn't be either
		if (e.Key == Key.F12) {
			e.Handled = true;
			return;
		}

		if (HandleFlyKey(e, false)) {
			e.Handled = true;
			return;
		}

		if (IsEditorShortcut(e)) return;
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

	private async Task PasteClipboardAsync() {
		if (!ShouldForward || m_engine is null) return;
		var clipboard = m_topLevel?.Clipboard;
		if (clipboard is null) return;
		var data = await clipboard.TryGetDataAsync();
		if (data is null) return;

		foreach (var item in data.Items) {
			if (!item.Formats.Contains(DataFormat.Text)) continue;
			if (await item.TryGetRawAsync(DataFormat.Text) is not string text || string.IsNullOrEmpty(text)) continue;

			foreach (var rune in text.AsSpan().EnumerateRunes())
				Events.Send(new WindowChar { Key = (uint)rune.Value });
			return;
		}
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
