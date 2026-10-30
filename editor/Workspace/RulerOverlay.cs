using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Numerics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

/// <summary>Turbo shitty ruler overlay</summary>
public sealed class RulerOverlay : UserControl {
	private const float VoxelSize = 0.1f;

	private readonly Canvas m_labels = new() { IsHitTestVisible = false };
	private readonly Border m_card;
	private static readonly FontFamily Font = new("FiraCode Nerd Font,Consolas,monospace");

	private readonly TextBlock m_title = new() { FontFamily = Font, FontWeight = FontWeight.SemiBold };
	private readonly TextBlock m_details = new() { FontFamily = Font };
	private readonly TextBlock m_hint = new() { FontFamily = Font, FontSize = 11, TextWrapping = TextWrapping.Wrap, MaxWidth = 280 };

	private readonly List<RulerSegment> m_segments = [];
	private RulerSegment? m_preview;

	private Listener? m_listener;
	private WorkspaceViewModel? m_vm;
	private bool m_attached;

	public RulerOverlay() {
		var undo = MakeButton("Undo", () => Send(1));
		var clear = MakeButton("Clear", () => Send(0));
		var copy = MakeButton("Copy", CopyLatest);

		m_hint.Foreground = Resource("TextMuted");
		m_hint.Text = "Click to place points. Shift locks an axis, Backspace undoes, Esc clears. Grid snapping (G) applies";

		m_card = new Border {
			HorizontalAlignment = HorizontalAlignment.Left,
			VerticalAlignment = VerticalAlignment.Bottom,
			Margin = new Thickness(12),
			Padding = new Thickness(10, 8),
			CornerRadius = new CornerRadius(8),
			Background = Resource("Bg3"),
			Child = new StackPanel {
				Spacing = 6,
				Children = {
					m_title,
					m_details,
					new StackPanel {
						Orientation = Orientation.Horizontal,
						Spacing = 6,
						Children = { copy, undo, clear }
					},
					m_hint
				}
			}
		};

		Content = new Panel { Children = { m_labels, m_card } };
		IsVisible = false;
		ClipToBounds = true;

		AttachedToVisualTree += (_, _) => Attach();
		DetachedFromVisualTree += (_, _) => Detach();
		DataContextChanged += (_, _) => {
			if (!m_attached) return;
			Detach();
			Attach();
		};
	}

	private static IBrush Resource(string key) {
		return Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var found) == true && found is IBrush brush
			? brush
			: Brushes.Gray;
	}

	private static Button MakeButton(string text, Action click) {
		var button = new Button { Content = text, Padding = new Thickness(8, 2), FontSize = 12, FontFamily = Font };
		button.Click += (_, _) => click();
		return button;
	}

	private void Attach() {
		m_attached = true;
		m_vm = DataContext as WorkspaceViewModel;
		if (m_vm is null) return;

		m_vm.PropertyChanged += OnViewModelChanged;
		m_listener = new Listener();
		m_listener.SubscribeOnUiThread<RulerChanged>(OnChanged);
		Refresh();
	}

	private void Detach() {
		m_attached = false;
		if (m_vm is not null) m_vm.PropertyChanged -= OnViewModelChanged;
		m_vm = null;
		m_listener?.Dispose();
		m_listener = null;
	}

	private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName is nameof(WorkspaceViewModel.ActiveTool) or nameof(WorkspaceViewModel.IsPlayingInViewport)
		    or nameof(WorkspaceViewModel.IsPlayingExternal)) {
			Refresh();
		}
	}

	private void OnChanged(RulerChanged e) {
		if (m_vm is null || e.WorkspaceHandle != m_vm.EffectiveHandle) return;

		m_segments.Clear();
		m_segments.AddRange(e.Segments);
		m_preview = e.PreviewActive ? e.Preview : null;
		Refresh();
	}

	private void Send(uint kind) {
		if (m_vm is null) return;
		Events.Send(new RulerCommand { WorkspaceHandle = m_vm.EffectiveHandle, Kind = kind });
	}

	private static Vector3 Delta(RulerSegment segment) {
		return new Vector3(segment.Bx - segment.Ax, segment.By - segment.Ay, segment.Bz - segment.Az);
	}

	private static string Number(float value, string format = "F2") {
		return value.ToString(format, CultureInfo.InvariantCulture);
	}

	private static string Distance(float length) {
		return $"{Number(length)} m";
	}

	private void CopyLatest() {
		if (Latest() is not { } latest) return;
		var length = Delta(latest).Length();
		var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
		if (clipboard is null) return;
		var data = new DataTransfer();
		data.Add(DataTransferItem.CreateText(Number(length, "F3")));
		_ = clipboard.SetDataAsync(data);
	}

	private RulerSegment? Latest() {
		return m_preview ?? m_segments.LastOrDefault();
	}

	private void Refresh() {
		var toolActive = m_vm?.ActiveTool == GizmoTool.Ruler;
		var playing = m_vm is { IsPlayingInViewport: true } or { IsPlayingExternal: true };
		var any = m_segments.Count > 0 || m_preview is not null;
		IsVisible = m_vm is not null && !playing && (toolActive || any);
		if (!IsVisible) return;

		RebuildLabels();

		m_hint.IsVisible = toolActive;
		m_title.Text = m_segments.Count == 0 ? "Ruler" : $"Ruler  {m_segments.Count} measured";

		if (Latest() is not { } latest) {
			m_details.Text = "Pick a start point";
			return;
		}

		var delta = Delta(latest);
		var length = delta.Length();
		var horizontal = MathF.Sqrt((delta.X * delta.X) + (delta.Y * delta.Y));
		var slope = MathF.Atan2(MathF.Abs(delta.Z), horizontal) * 180f / MathF.PI;

		var lines = new List<string> {
			$"Distance    {Distance(length)}  ({Number(length / VoxelSize, "F1")} voxels)",
			$"X {Number(delta.X)}   Y {Number(delta.Y)}   Z {Number(delta.Z)}",
			$"Horizontal  {Distance(horizontal)}",
			$"Vertical    {Distance(MathF.Abs(delta.Z))}   Slope {Number(slope, "F1")}°"
		};
		if (m_segments.Count > 1) lines.Add($"Total       {Distance(m_segments.Sum(s => Delta(s).Length()))}");
		m_details.Text = string.Join('\n', lines);
	}

	private void RebuildLabels() {
		m_labels.Children.Clear();
		var scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;

		foreach (var segment in m_segments) AddLabel(segment, scale, false);
		if (m_preview is { } preview) AddLabel(preview, scale, true);
	}

	private void AddLabel(RulerSegment segment, double scale, bool preview) {
		if (!segment.LabelVisible) return;

		var label = new Border {
			Background = Resource("Bg3"),
			BorderBrush = Resource("Orange"),
			BorderThickness = new Thickness(1),
			CornerRadius = new CornerRadius(4),
			Padding = new Thickness(6, 2),
			Opacity = preview ? 0.85 : 1.0,
			IsHitTestVisible = false,
			Child = new TextBlock { Text = Distance(Delta(segment).Length()), FontSize = 12, FontFamily = Font }
		};
		label.Measure(Size.Infinity);

		Canvas.SetLeft(label, (segment.LabelX / scale) - (label.DesiredSize.Width / 2));
		Canvas.SetTop(label, (segment.LabelY / scale) - label.DesiredSize.Height - 8);
		m_labels.Children.Add(label);
	}
}
