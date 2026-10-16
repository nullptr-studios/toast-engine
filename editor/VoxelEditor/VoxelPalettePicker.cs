using System;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Shapes;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using Avalonia.VisualTree;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using editor.Components.Elements;
using editor.Engine;
using Lucide.Avalonia;

namespace editor.VoxelEditor;

public partial class PaletteEntryVM : ObservableObject {
	private readonly Action m_changed;
	private readonly VoxelPaletteEntry m_entry;
	private readonly VoxelPaletteFile m_palette;

	public PaletteEntryVM(VoxelPaletteFile palette, int id, Action changed) {
		m_palette = palette;
		m_entry = palette.Entries[id];
		m_changed = changed;
	}

	public bool IsEmpty => !m_entry.InUse;

	public bool InUse => m_entry.InUse;

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

	public float MaxEmissive {
		get => m_palette.MaxEmissive;
		set {
			m_palette.MaxEmissive = Math.Max(value, 0f);
			OnPropertyChanged(string.Empty);
			m_changed();
		}
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
	private UniformGrid? m_grid;
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

	private static IBrush Resource(string key) {
		return Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var found) == true && found is IBrush brush
			? brush
			: Brushes.Gray;
	}

	private IBrush BrushOf(int id) {
		if (m_palette is null) return new SolidColorBrush(Color.FromRgb(160, 160, 160));    // the engine default palette

		var entry = m_palette.Entries[id];
		if (!entry.InUse) return Resource("Bg2");

		return new SolidColorBrush(Color.FromRgb(entry.R, entry.G, entry.B), entry.Transparent ? Math.Clamp(entry.Alpha, 0f, 1f) : 1.0);
	}

	private void Refresh() {
		var id = m_editor?.PaletteId ?? 1;
		m_swatch.Background = BrushOf(id);
		m_label.Text = id.ToString();
	}

	private Control BuildCell(int id) {
		var entry = m_palette?.Entries[id];
		var empty = entry is { InUse: false };
		var selected = id == m_editor?.PaletteId;

		var cell = new Button {
			Width = CellSize - 2,
			Height = CellSize - 2,
			Padding = new Thickness(0),
			Margin = new Thickness(1),
			CornerRadius = new CornerRadius(3),
			BorderThickness = new Thickness(selected ? 2 : 0),
			BorderBrush = Brushes.White,
			Background = BrushOf(id)
		};
		ToolTip.SetTip(cell, BuildTip(id));
		cell.Click += (_, _) => {
			if (m_editor is not null) m_editor.PaletteId = id;
			m_flyout.Content = BuildPopup();
		};

		var panel = new Panel();

		// behind the translucent brush so alpha reads against it
		if (entry is { InUse: true, Transparent: true })
			panel.Children.Add(new Border {
				Width = CellSize - 2,
				Height = CellSize - 2,
				Margin = new Thickness(1),
				CornerRadius = new CornerRadius(3),
				IsHitTestVisible = false,
				Background = CheckerBrush.Instance
			});

		panel.Children.Add(cell);

		// marks an id as empty
		if (empty)
			panel.Children.Add(new Path {
				Data = Geometry.Parse("M0,0.5 L1,0.5 M0.5,0 L0.5,1"),
				Stretch = Stretch.Fill,
				Width = 8,
				Height = 8,
				HorizontalAlignment = HorizontalAlignment.Center,
				VerticalAlignment = VerticalAlignment.Center,
				Stroke = Resource("Bg3"),
				StrokeThickness = 2,
				IsHitTestVisible = false
			});

		return panel;
	}

	private object BuildTip(int id) {
		if (m_palette is null) return id.ToString();

		var tip = VoxelPaletteTip.Describe(id, m_palette);
		var panel = new StackPanel { Spacing = 4 };
		panel.Children.Add(new TextBlock { Text = tip.Title, FontWeight = FontWeight.SemiBold });
		if (!tip.InUse) return panel;

		panel.Children.Add(TipRow(tip.Albedo, tip.AlbedoText));
		panel.Children.Add(TipRow(tip.Rms, tip.RmsText));
		panel.Children.Add(new TextBlock { Text = tip.EmissiveText });
		panel.Children.Add(new TextBlock { Text = tip.AlphaText });
		return panel;
	}

	private static Control TipRow(Color dot, string text) {
		var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
		row.Children.Add(new Ellipse {
			Width = 10, Height = 10, VerticalAlignment = VerticalAlignment.Center, Fill = new SolidColorBrush(dot)
		});
		row.Children.Add(new TextBlock { Text = text });
		return row;
	}

	private void RefreshCell(int id) {
		if (m_grid is null || id < 1 || id > m_grid.Children.Count) return;
		m_grid.Children[id - 1] = BuildCell(id);
	}

	private Control BuildPopup() {
		var grid = new UniformGrid { Columns = Columns, Width = Columns * CellSize };
		m_grid = grid;

		// 1 to 255 since 0 is empty
		for (var id = 1; id < VoxelPaletteFile.Size; ++id) grid.Children.Add(BuildCell(id));

		var properties = new StackPanel { Spacing = 8, Width = 320 };
		var selected = m_editor?.PaletteId ?? 1;

		var header = new DockPanel();
		var title = new TextBlock {
			Text = $"Palette ID {selected}", FontWeight = FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center
		};
		header.Children.Add(title);
		properties.Children.Add(header);

		if (m_palette is null) {
			properties.Children.Add(new TextBlock {
				Text = "The shape uses the default palette. Set a Palette Override on it to edit the colours",
				TextWrapping = TextWrapping.Wrap,
				Foreground = Brushes.Gray
			});
		} else {
			var entry = new PaletteEntryVM(m_palette, selected, QueueSave);
			properties.DataContext = entry;

			var clear = new Button {
				Content = new LucideIcon { Kind = LucideIconKind.Trash2, Size = 16, StrokeWidth = 2.5 },
				Background = Brushes.Transparent,
				BorderThickness = new Thickness(0),
				Padding = new Thickness(5),
				CornerRadius = new CornerRadius(6),
				Cursor = new Cursor(StandardCursorType.Hand),
				VerticalAlignment = VerticalAlignment.Center
			};
			ToolTip.SetTip(clear, "Remove this entry from the palette");
			clear.Bind(IsEnabledProperty, new Binding(nameof(PaletteEntryVM.InUse)));
			clear.Click += (_, _) => {
				m_palette.Entries[selected].Clear();
				m_flyout.Content = BuildPopup();
				QueueSave();
			};
			DockPanel.SetDock(clear, Avalonia.Controls.Dock.Right);
			header.Children.Insert(0, clear);

			properties.Children.Add(EmptyBanner());

			var albedo = new Color3Box();
			albedo.Bind(ColorBoxBase.RProperty, new Binding(nameof(PaletteEntryVM.AlbedoR)) { Mode = BindingMode.TwoWay });
			albedo.Bind(ColorBoxBase.GProperty, new Binding(nameof(PaletteEntryVM.AlbedoG)) { Mode = BindingMode.TwoWay });
			albedo.Bind(ColorBoxBase.BProperty, new Binding(nameof(PaletteEntryVM.AlbedoB)) { Mode = BindingMode.TwoWay });
			properties.Children.Add(Row("Albedo", albedo));
			properties.Children.Add(Row("Roughness", Number(nameof(PaletteEntryVM.Roughness))));
			properties.Children.Add(Row("Metallic", Number(nameof(PaletteEntryVM.Metallic))));
			properties.Children.Add(Row("Reflectivity", Number(nameof(PaletteEntryVM.Reflectivity))));
			properties.Children.Add(Row("Emissive", Number(nameof(PaletteEntryVM.Emissive))));
			properties.Children.Add(Row("Max emissive", Number(nameof(PaletteEntryVM.MaxEmissive), false)));
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

	private static Control EmptyBanner() {
		var banner = new Border {
			Background = Resource("Orange"),
			Padding = new Thickness(4),
			CornerRadius = new CornerRadius(8),
			Child = new StackPanel {
				Orientation = Orientation.Horizontal,
				Spacing = 4,
				HorizontalAlignment = HorizontalAlignment.Center,
				Children = {
					new LucideIcon {
						Kind = LucideIconKind.SquareDashedMousePointer, Size = 18, StrokeWidth = 2.5, Foreground = Resource("Bg1")
					},
					new TextBlock { Text = "This ID is empty", FontWeight = FontWeight.Bold, Foreground = Resource("Bg1") }
				}
			}
		};
		banner.Bind(IsVisibleProperty, new Binding(nameof(PaletteEntryVM.IsEmpty)));
		return banner;
	}

	private static DragFloatBox Number(string property, bool unit = true) {
		var box = new DragFloatBox { Minimum = 0 };
		if (unit) box.Maximum = 1;
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
		RefreshCell(m_editor?.PaletteId ?? 1);
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
