using System;
using System.Linq;
using System.Numerics;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using editor.Engine;

namespace editor.Workspace;

internal sealed class SharedViewportPresenter : IDisposable {
	private readonly Visual m_host;
	private readonly ToastEngine m_engine;

	private Compositor? m_compositor;
	private ICompositionGpuInterop? m_interop;
	private CompositionDrawingSurface? m_surface;
	private CompositionSurfaceVisual? m_visual;
	private string m_imageType = "";

	private ulong m_generation;
	private uint m_width, m_height;
	private Slot[] m_slots = [];
	private ulong m_lastFrameId;
	private bool m_updating;
	private bool m_starting;
	private bool m_engineSharing;

	private sealed class Slot {
		public ICompositionImportedGpuImage? Image;
		public ICompositionImportedGpuSemaphore? Ready;
		public ICompositionImportedGpuSemaphore? Done;
	}

	public SharedViewportPresenter(Visual host, ToastEngine engine) {
		m_host = host;
		m_engine = engine;
	}

	public bool Failed { get; private set; }
	public bool Active { get; private set; }
	private static bool UsesKeyedMutex => OperatingSystem.IsWindows();
	public void Start() {
		if (m_starting || Active || Failed) return;
		m_starting = true;
		_ = StartAsync();
	}

	private async Task StartAsync() {
		try {
			m_compositor = ElementComposition.GetElementVisual(m_host)?.Compositor;
			m_interop = m_compositor is null ? null : await m_compositor.TryGetCompositionGpuInterop();
			if (m_compositor is null || m_interop is null || !PickHandleTypes(m_interop)) {
				Fail("the compositor has no GPU interop for shared textures");
				return;
			}

			var luid = m_interop.DeviceLuid is { Length: 8 } l ? BitConverter.ToUInt64(l) : 0;
			if (!m_engine.EnableSharedViewport(luid, m_interop.DeviceUuid)) {
				Fail("the renderer can't share textures with the compositor's device");
				return;
			}
			m_engineSharing = true;

			m_surface = m_compositor.CreateDrawingSurface();
			m_visual = m_compositor.CreateSurfaceVisual();
			m_visual.Surface = m_surface;
			ElementComposition.SetElementChildVisual(m_host, m_visual);
			Active = true;
		} catch (Exception e) {
			Fail(e.Message);
		} finally {
			m_starting = false;
		}
	}

	private bool PickHandleTypes(ICompositionGpuInterop interop) {
		if (UsesKeyedMutex) {
			m_imageType = KnownPlatformGraphicsExternalImageHandleTypes.D3D11TextureNtHandle;
			return interop.SupportedImageHandleTypes.Contains(m_imageType) &&
			       interop.GetSynchronizationCapabilities(m_imageType)
				       .HasFlag(CompositionGpuImportedImageSynchronizationCapabilities.KeyedMutex);
		}

		m_imageType = KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaquePosixFileDescriptor;
		return interop.SupportedImageHandleTypes.Contains(m_imageType) &&
		       interop.SupportedSemaphoreTypes.Contains(KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaquePosixFileDescriptor) &&
		       interop.GetSynchronizationCapabilities(m_imageType)
			       .HasFlag(CompositionGpuImportedImageSynchronizationCapabilities.Semaphores);
	}

	private void Fail(string reason) {
		Log.Warn($"Viewport falls back to reading frames back: {reason}");
		if (m_engineSharing) m_engine.DisableSharedViewport();
		m_engineSharing = false;
		Failed = true;
		Active = false;
		Teardown();
	}

	public bool Tick() {
		if (!Active || m_interop is null || m_surface is null || m_visual is null) return false;
		if (m_interop.IsLost) {
			Fail("the compositor lost its GPU device");
			return false;
		}

		Layout();

		var result = m_engine.TryGetSharedViewportFrame(out var frame);
		if (result < 0) {
			Fail("the renderer stopped sharing textures");
			return false;
		}
		if (result == 0 || m_updating || frame.frame_id == m_lastFrameId) return false;

		if (frame.generation != m_generation && !Import(frame)) return false;

		var slot = m_slots[frame.slot];
		if (slot.Image is null || !m_engine.HoldSharedViewportSlot(m_generation, frame.slot)) return false;

		m_lastFrameId = frame.frame_id;
		m_updating = true;
		_ = PresentAsync(m_generation, frame.slot, slot);
		return true;
	}

	private async Task PresentAsync(ulong generation, uint index, Slot slot) {
		try {
			if (UsesKeyedMutex) {
				await m_surface!.UpdateWithKeyedMutexAsync(slot.Image!, 0, 0);
			} else {
				await m_surface!.UpdateWithSemaphoresAsync(slot.Image!, slot.Ready!, slot.Done!);
			}
		} catch (Exception e) {
			m_engine.ReleaseSharedViewportSlot(generation, index, false);
			m_updating = false;
			Fail($"the compositor could not show a shared frame ({e.Message})");
			return;
		}
		m_engine.ReleaseSharedViewportSlot(generation, index, true);
		m_updating = false;
	}

	private bool Import(ToastViewportSharedFrame frame) {
		DisposeSlots();
		var slots = new Slot[frame.slot_count];
		for (uint i = 0; i < frame.slot_count; i++) {
			if (!m_engine.ExportSharedViewportSlot(frame.generation, i, out var handles)) return false;

			var properties = new PlatformGraphicsExternalImageProperties {
				Width = (int)frame.width,
				Height = (int)frame.height,
				Format = PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm,
				MemorySize = frame.memory_size,
				TopLeftOrigin = true,
			};
			var slot = new Slot {
				Image = m_interop!.ImportImage(new PlatformHandle(handles.image, m_imageType), properties),
			};
			if (!UsesKeyedMutex) {
				const string semaphoreType = KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaquePosixFileDescriptor;
				slot.Ready = m_interop.ImportSemaphore(new PlatformHandle(handles.ready, semaphoreType));
				slot.Done = m_interop.ImportSemaphore(new PlatformHandle(handles.done, semaphoreType));
			}
			slots[i] = slot;
		}

		m_slots = slots;
		m_generation = frame.generation;
		m_width = frame.width;
		m_height = frame.height;
		return true;
	}

	private void Layout() {
		if (m_visual is null || m_width == 0 || m_height == 0) return;
		var bounds = m_host.Bounds.Size;
		var scale = Math.Min(bounds.Width / m_width, bounds.Height / m_height);
		if (!double.IsFinite(scale) || scale <= 0) return;
		var size = new Vector2((float)(m_width * scale), (float)(m_height * scale));
		m_visual.Size = size;
		m_visual.Offset = new Vector3((float)((bounds.Width - size.X) / 2), (float)((bounds.Height - size.Y) / 2), 0);
	}

	private void DisposeSlots() {
		foreach (var slot in m_slots) {
			_ = slot.Image?.DisposeAsync();
			_ = slot.Ready?.DisposeAsync();
			_ = slot.Done?.DisposeAsync();
		}
		m_slots = [];
		m_generation = 0;
	}

	private void Teardown() {
		DisposeSlots();
		if (m_visual is not null) ElementComposition.SetElementChildVisual(m_host, null);
		m_visual = null;
		m_surface?.Dispose();
		m_surface = null;
	}

	public void Dispose() {
		Active = false;
		Teardown();
	}
}
