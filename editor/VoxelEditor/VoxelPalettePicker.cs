using System;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using editor.Components.Elements;
using editor.Engine;

namespace editor.VoxelEditor;

public partial class PaletteEntryVM : ObservableObject {
	private readonly Action m_changed;
	private readonly VoxelPaletteEntry m_entry;

	public PaletteEntryVM(VoxelPaletteEntry entry, Action changed) {
		m_entry = entry;
		m_changed = changed;
	}

	public float AlbedoR {
		get => m_entry.R / 255f;
		set => Set(() => m_entry.R = ToByte(value));
	}

	public float AlbedoG {
		get => m_entry.G / 255f;
		set => Set(() => m_entry.G = ToByte(value));
	}

	public float AlbedoB {
		get => m_entry.B / 255f;
		set => Set(() => m_entry.B = ToByte(value));
	}

	public float Roughness {
		get => m_entry.Roughness;
		set => Set(() => m_entry.Roughness = value);
	}

	public float Metallic {
		get => m_entry.Metallic;
		set => Set(() => m_entry.Metallic = value);
	}

	public float Reflectivity {
		get => m_entry.Reflectivity;
		set => Set(() => m_entry.Reflectivity = value);
	}

	public float Emissive {
		get => m_entry.Emissive;
		set => Set(() => m_entry.Emissive = value);
	}

	public bool Transparent {
		get => m_entry.Transparent;
		set => Set(() => m_entry.Transparent = value);
	}

	public float Alpha {
		get => m_entry.Alpha;
		set => Set(() => m_entry.Alpha = value);
	}

	private static byte ToByte(float value) {
		return (byte)Math.Clamp((int)Math.Round(value * 255f), 0, 255);
	}

	private void Set(Action write) {
		write();
		m_entry.InUse = true;
		OnPropertyChanged(string.Empty);
		m_changed();
	}
}

public sealed class VoxelPalettePicker : UserControl {
	private const int Columns = 16;
	private const double CellSize = 22;

	private readonly Border m_swatch = new() { CornerRadius = new CornerRadius(4) };
	private readonly TextBlock m_label = new() { VerticalAlignment = VerticalAlignment.Center, MinWidth = 22 };
	private readonly Flyout m_flyout = new() { Placement = PlacementMode.BottomEdgeAlignedLeft };

	private VoxelEditorViewModel? m_editor;
	private VoxelPaletteFile? m_palette;
	private string? m_palettePath;
	private DispatcherTimer? m_saveTimer;

	public VoxelPalettePicker() {
		//nnegative margin keeps the bar as tall as the other buttons
		var checker = new Border {
			Width = 20, Height = 20, Margin = new Thickness(-4, -3, 0, -3), CornerRadius = new CornerRadius(4),
			Background = CheckerBrush.Instance, ClipToBounds = true, Child = m_swatch
		};
		var button = new Button {
			Classes = { "chev" },
			CornerRadius = new CornerRadius(6),
			Content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { checker, m_label } },
			Flyout = m_flyout
		};
		ToolTip.SetTip(button, "Palette ID new volumes, Paint and Bucket use, it also recolours the selected volume");
		m_flyout.Opening += (_, _) => m_flyout.Content = BuildPopup();
		Content = button;
	}

	public VoxelEditorViewModel? Editor {
		get => m_editor;
		set {
			if (m_editor is not null) m_editor.PropertyChanged -= OnEditorChanged;
			m_editor = value;
			if (m_editor is not null) m_editor.PropertyChanged += OnEditorChanged;
			LoadPalette();
			Refresh();
		}
	}

	private void OnEditorChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName == nameof(VoxelEditorViewModel.PaletteUid)) LoadPalette();
		if (e.PropertyName is nameof(VoxelEditorViewModel.PaletteUid) or nameof(VoxelEditorViewModel.PaletteId)) Refresh();
	}

	private void LoadPalette() {
		m_palette = null;
		m_palettePath = null;
		if (m_editor is not { PaletteUid.Length: > 0 } editor || !AssetDatabase.TryResolve(editor.PaletteUid, out var virtualPath, out _))
			return;
		try {
			m_palettePath = ProjectContext.Resolve(virtualPath);
			m_palette = VoxelPaletteFile.FromFile(m_palettePath);
		} catch (Exception e) {
			Log.Warn($"Could not read the palette {virtualPath}: {e.Message}");
			m_palette = null;
		}
	}

	private Color ColorOf(int id) {
		if (m_palette is null) return Color.FromArgb(255, 160, 160, 160);    // the engine default palette
		var entry = m_palette.Entries[id];
		return Color.FromArgb((byte)Math.Clamp((int)Math.Round(entry.Alpha * 255), 0, 255), entry.R, entry.G, entry.B);
	}

	private void Refresh() {
		var id = m_editor?.PaletteId ?? 1;
		m_swatch.Background = new SolidColorBrush(ColorOf(id));
		m_label.Text = id.ToString();
	}

	private Control BuildPopup() {
		var grid = new UniformGrid { Columns = Columns, Width = Columns * CellSize };
		for (var id = 0; id < VoxelPaletteFile.Size; ++id) {
			if (id == 0) {
				// 0 is empty and never pickable
				grid.Children.Add(new Border());
				continue;
			}
			var chosen = id;
			var cell = new Button {
				Width = CellSize - 2,
				Height = CellSize - 2,
				Padding = new Thickness(0),
				Margin = new Thickness(1),
				CornerRadius = new CornerRadius(3),
				BorderThickness = new Thickness(chosen == m_editor?.PaletteId ? 2 : 0),
				BorderBrush = Brushes.White,
				Background = new SolidColorBrush(ColorOf(chosen))
			};
			ToolTip.SetTip(cell, chosen.ToString());
			cell.Click += (_, _) => {
				if (m_editor is not null) m_editor.PaletteId = chosen;
				m_flyout.Content = BuildPopup();
			};
			grid.Children.Add(cell);
		}

		var properties = new StackPanel { Spacing = 8, Width = 320 };
		var selected = m_editor?.PaletteId ?? 1;
		properties.Children.Add(new TextBlock { Text = $"Palette ID {selected}", FontWeight = FontWeight.SemiBold });
		if (m_palette is null) {
			properties.Children.Add(new TextBlock {
				Text = "The shape uses the default palette. Set a Palette Override on it to edit the colours",
				TextWrapping = TextWrapping.Wrap,
				Foreground = Brushes.Gray
			});
		} else {
			properties.DataContext = new PaletteEntryVM(m_palette.Entries[selected], QueueSave);
			var albedo = new Color3Box();
			albedo.Bind(ColorBoxBase.RProperty, new Binding(nameof(PaletteEntryVM.AlbedoR)) { Mode = BindingMode.TwoWay });
			albedo.Bind(ColorBoxBase.GProperty, new Binding(nameof(PaletteEntryVM.AlbedoG)) { Mode = BindingMode.TwoWay });
			albedo.Bind(ColorBoxBase.BProperty, new Binding(nameof(PaletteEntryVM.AlbedoB)) { Mode = BindingMode.TwoWay });
			properties.Children.Add(Row("Albedo", albedo));
			properties.Children.Add(Row("Roughness", Number(nameof(PaletteEntryVM.Roughness))));
			properties.Children.Add(Row("Metallic", Number(nameof(PaletteEntryVM.Metallic))));
			properties.Children.Add(Row("Reflectivity", Number(nameof(PaletteEntryVM.Reflectivity))));
			properties.Children.Add(Row("Emissive", Number(nameof(PaletteEntryVM.Emissive))));
			var transparent = new BoolBox();
			transparent.Bind(BoolBox.ValueProperty, new Binding(nameof(PaletteEntryVM.Transparent)) { Mode = BindingMode.TwoWay });
			properties.Children.Add(Row("Transparent", transparent));
			properties.Children.Add(Row("Alpha", Number(nameof(PaletteEntryVM.Alpha))));
		}

		var content = new StackPanel {
			Orientation = Orientation.Horizontal,
			Spacing = 16,
			Margin = new Thickness(4),
			Children = { grid, new Border { Width = 1, Background = Brushes.DimGray }, properties }
		};
		// ;ift the cap once it is shown
		content.AttachedToVisualTree += (_, _) => {
			if (content.FindAncestorOfType<FlyoutPresenter>() is { } presenter) {
				presenter.MaxWidth = double.PositiveInfinity;
				presenter.MaxHeight = double.PositiveInfinity;
			}
		};
		return content;
	}

	private static DragFloatBox Number(string property) {
		var box = new DragFloatBox { Minimum = 0, Maximum = 1 };
		box.Bind(DragFloatBox.ValueProperty, new Binding(property) { Mode = BindingMode.TwoWay });
		return box;
	}

	private static Control Row(string label, Control box) {
		var row = new Grid { ColumnDefinitions = new ColumnDefinitions("100,*") };
		row.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
		Grid.SetColumn(box, 1);
		row.Children.Add(box);
		return row;
	}

	// Drags write many values so only save to the file when stopped
	private void QueueSave() {
		m_saveTimer ??= new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
		m_saveTimer.Tick -= OnSaveTick;
		m_saveTimer.Tick += OnSaveTick;
		m_saveTimer.Stop();
		m_saveTimer.Start();
		Refresh();
	}

	private void OnSaveTick(object? sender, EventArgs e) {
		m_saveTimer?.Stop();
		if (m_palette is null || m_palettePath is null) return;
		try {
			m_palette.Save(m_palettePath);
		} catch (Exception ex) {
			Log.Warn($"Could not save the palette: {ex.Message}");
		}
	}
}
