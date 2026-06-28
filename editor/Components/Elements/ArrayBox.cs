//
// ArrayBox.cs by Xein
// 24 Jun 2026
//

using System;
using System.Collections;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Lucide.Avalonia;

namespace editor.Components.Elements;

public sealed class ArrayBox : TemplatedControl {
	private const double DragThreshold = 4;

	public static readonly StyledProperty<IList?> ItemsProperty =
		AvaloniaProperty.Register<ArrayBox, IList?>(nameof(Items), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<IDataTemplate?> ItemTemplateProperty =
		AvaloniaProperty.Register<ArrayBox, IDataTemplate?>(nameof(ItemTemplate));

	public static readonly StyledProperty<bool> CanReorderProperty =
		AvaloniaProperty.Register<ArrayBox, bool>(nameof(CanReorder), true);

	public static readonly StyledProperty<bool> CanAddRemoveProperty =
		AvaloniaProperty.Register<ArrayBox, bool>(nameof(CanAddRemove), true);

	public static readonly StyledProperty<string> AddLabelProperty =
		AvaloniaProperty.Register<ArrayBox, string>(nameof(AddLabel), "Add");

	public static readonly StyledProperty<System.Windows.Input.ICommand?> AddCommandProperty =
		AvaloniaProperty.Register<ArrayBox, System.Windows.Input.ICommand?>(nameof(AddCommand));

	// When set, each row is wrapped in a card Border with these styles
	public static readonly StyledProperty<IBrush?> RowBackgroundProperty =
		AvaloniaProperty.Register<ArrayBox, IBrush?>(nameof(RowBackground));

	public static readonly StyledProperty<CornerRadius> RowCornerRadiusProperty =
		AvaloniaProperty.Register<ArrayBox, CornerRadius>(nameof(RowCornerRadius));

	public static readonly StyledProperty<Thickness> RowPaddingProperty =
		AvaloniaProperty.Register<ArrayBox, Thickness>(nameof(RowPadding));

	private ItemsControl? m_items;
	private Button? m_add;

	// Active reorder drag
	private object? m_pressItem;
	private PointerPressedEventArgs? m_pressArgs;
	private Point m_pressPoint;

	// Insertion indicator currently shown during a drag
	private Border? m_activeLine;

	public IList? Items {
		get => GetValue(ItemsProperty);
		set => SetValue(ItemsProperty, value);
	}

	public IDataTemplate? ItemTemplate {
		get => GetValue(ItemTemplateProperty);
		set => SetValue(ItemTemplateProperty, value);
	}

	public bool CanReorder {
		get => GetValue(CanReorderProperty);
		set => SetValue(CanReorderProperty, value);
	}

	public bool CanAddRemove {
		get => GetValue(CanAddRemoveProperty);
		set => SetValue(CanAddRemoveProperty, value);
	}

	public string AddLabel {
		get => GetValue(AddLabelProperty);
		set => SetValue(AddLabelProperty, value);
	}

	public System.Windows.Input.ICommand? AddCommand {
		get => GetValue(AddCommandProperty);
		set => SetValue(AddCommandProperty, value);
	}

	public IBrush? RowBackground {
		get => GetValue(RowBackgroundProperty);
		set => SetValue(RowBackgroundProperty, value);
	}

	public CornerRadius RowCornerRadius {
		get => GetValue(RowCornerRadiusProperty);
		set => SetValue(RowCornerRadiusProperty, value);
	}

	public Thickness RowPadding {
		get => GetValue(RowPaddingProperty);
		set => SetValue(RowPaddingProperty, value);
	}

	public Func<object?>? ItemFactory { get; set; }

	protected override Type StyleKeyOverride => typeof(ArrayBox);

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);
		if (m_add != null) m_add.Click -= OnAddClick;

		m_items = e.NameScope.Find<ItemsControl>("PART_Items");
		m_add = e.NameScope.Find<Button>("PART_Add");

		if (m_items != null) {
			m_items.ItemTemplate = new FuncDataTemplate<object>((item, _) => BuildRow(item), false);
			m_items.ItemsSource = Items;
		}

		if (m_add != null) {
			m_add.Click += OnAddClick;
			m_add.IsVisible = CanAddRemove && (ItemFactory != null || AddCommand != null);
		}
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (m_items is null) return;

		if (change.Property == ItemsProperty) {
			m_items.ItemsSource = Items;
		}
		else if (change.Property == ItemTemplateProperty) {
			m_items.ItemTemplate = new FuncDataTemplate<object>((item, _) => BuildRow(item), false);
		}
		else if ((change.Property == CanAddRemoveProperty || change.Property == IsEnabledProperty
		          || change.Property == AddCommandProperty) && m_add != null) {
			m_add.IsVisible = CanAddRemove && (ItemFactory != null || AddCommand != null);
		}
	}

	private void OnAddClick(object? sender, RoutedEventArgs e) {
		if (AddCommand?.CanExecute(null) == true) {
			AddCommand.Execute(null);
			return;
		}
		if (ItemFactory is { } factory && Items is { } list && factory() is { } item) list.Add(item);
	}

	private Control BuildRow(object item) {
		var grid = new Grid {
			ColumnSpacing = 6,
			ColumnDefinitions = new ColumnDefinitions {
				new(GridLength.Auto),
				new(GridLength.Star),
				new(GridLength.Auto)
			}
		};

		// Drag handle
		var grip = new Border {
			Background = Brushes.Transparent,
			Cursor = new Cursor(StandardCursorType.SizeAll),
			Padding = new Thickness(2),
			VerticalAlignment = VerticalAlignment.Top,
			IsVisible = CanReorder,
			Child = new LucideIcon {
				Kind = LucideIconKind.GripVertical,
				Size = 16,
				StrokeWidth = 2,
				Foreground = Brush("TextMuted")
			}
		};
		grip.PointerPressed += (_, e) => OnGripPressed(item, e);
		grip.PointerMoved += OnGripMoved;
		grip.PointerReleased += OnGripReleased;
		Grid.SetColumn(grip, 0);

		var content = new ContentControl {
			Content = item,
			ContentTemplate = ItemTemplate,
			VerticalAlignment = VerticalAlignment.Stretch
		};
		Grid.SetColumn(content, 1);

		var remove = new Button {
			Width = 20,
			Height = 20,
			Padding = new Thickness(0),
			Background = Brushes.Transparent,
			BorderThickness = new Thickness(0),
			CornerRadius = new CornerRadius(6),
			Cursor = new Cursor(StandardCursorType.Hand),
			VerticalAlignment = VerticalAlignment.Top,
			IsVisible = CanAddRemove,
			Content = new LucideIcon {
				Kind = LucideIconKind.X,
				Size = 12,
				StrokeWidth = 2.5,
				Foreground = Brush("TextMuted")
			}
		};
		remove.Click += (_, _) => Items?.Remove(item);
		Grid.SetColumn(remove, 2);

		grid.Children.Add(grip);
		grid.Children.Add(content);
		grid.Children.Add(remove);

		// When RowBackground is set, wrap everything (including grip/X) in a card Border
		Control rowContent = RowBackground is not null
			? new Border {
				Background   = RowBackground,
				CornerRadius = RowCornerRadius,
				Padding      = RowPadding,
				Child        = grid
			}
			: grid;

		// Wrap the row so a red insertion line can overlay its top/bottom edge while dragging
		var topLine = MakeInsertLine(VerticalAlignment.Top);
		var bottomLine = MakeInsertLine(VerticalAlignment.Bottom);
		var row = new Grid();
		row.Children.Add(rowContent);
		row.Children.Add(topLine);
		row.Children.Add(bottomLine);

		// Every row is a drop target so the dragged item can land relative to it
		DragDrop.SetAllowDrop(row, true);
		row.AddHandler(DragDrop.DragOverEvent, (_, e) => OnRowDragOver(row, topLine, bottomLine, e));
		row.AddHandler(DragDrop.DropEvent, (_, e) => OnRowDrop(item, row, e));
		row.AddHandler(DragDrop.DragLeaveEvent, (_, _) => OnRowDragLeave(topLine, bottomLine));

		return row;
	}

	private static Border MakeInsertLine(VerticalAlignment side) => new() {
		Height = 2,
		CornerRadius = new CornerRadius(1),
		Background = Brush("Red"),
		HorizontalAlignment = HorizontalAlignment.Stretch,
		// Sit on the row's top or bottom edge, inside its bounds so it is never clipped
		VerticalAlignment = side,
		IsHitTestVisible = false,
		IsVisible = false
	};

	private void OnGripPressed(object item, PointerPressedEventArgs e) {
		if (!CanReorder || Items is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_pressItem = item;
		m_pressArgs = e;
		m_pressPoint = e.GetPosition(this);
		e.Handled = true;
	}

	private async void OnGripMoved(object? sender, PointerEventArgs e) {
		if (m_pressItem is null || m_pressArgs is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;

		var item = m_pressItem;
		var args = m_pressArgs;
		m_pressItem = null;
		m_pressArgs = null;

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(ArrayItemDragData.Format, new ArrayDragRef(this, item)));
		await DragDrop.DoDragDropAsync(args, data, DragDropEffects.Move);
		ClearLine();
	}

	private void OnGripReleased(object? sender, PointerReleasedEventArgs e) {
		m_pressItem = null;
		m_pressArgs = null;
	}

	private bool Accepts(DragEventArgs e) =>
		Items != null && e.DataTransfer.TryGetValue(ArrayItemDragData.Format) is { } r && r.Owner == this;

	private void OnRowDragOver(Control row, Border topLine, Border bottomLine, DragEventArgs e) {
		e.Handled = true;
		if (!Accepts(e)) {
			e.DragEffects = DragDropEffects.None;
			return;
		}

		e.DragEffects = DragDropEffects.Move;
		var below = e.GetPosition(row).Y > row.Bounds.Height / 2;
		ShowLine(below ? bottomLine : topLine);
	}

	private void OnRowDragLeave(Border topLine, Border bottomLine) {
		// Only clear if the line we own is the one currently shown
		if (ReferenceEquals(m_activeLine, topLine) || ReferenceEquals(m_activeLine, bottomLine)) ClearLine();
	}

	private void OnRowDrop(object target, Control row, DragEventArgs e) {
		ClearLine();
		if (Items is not { } list || e.DataTransfer.TryGetValue(ArrayItemDragData.Format) is not { } r || r.Owner != this) return;
		e.Handled = true;

		var moved = r.Item;
		if (ReferenceEquals(moved, target)) return;

		var from = list.IndexOf(moved);
		var targetIndex = list.IndexOf(target);
		if (from < 0 || targetIndex < 0) return;

		// Drop above or below the target row depending on which half the pointer is over
		var below = e.GetPosition(row).Y > row.Bounds.Height / 2;
		var insert = below ? targetIndex + 1 : targetIndex;

		list.RemoveAt(from);
		if (from < insert) insert--;
		insert = Math.Clamp(insert, 0, list.Count);
		list.Insert(insert, moved);
	}

	private void ShowLine(Border line) {
		if (ReferenceEquals(m_activeLine, line)) return;
		if (m_activeLine != null) m_activeLine.IsVisible = false;
		line.IsVisible = true;
		m_activeLine = line;
	}

	private void ClearLine() {
		if (m_activeLine != null) m_activeLine.IsVisible = false;
		m_activeLine = null;
	}

	private static IBrush? Brush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}
}
