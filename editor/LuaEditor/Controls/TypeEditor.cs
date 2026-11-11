using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Controls;

public sealed class TypeEditor : UserControl {
	public static readonly StyledProperty<string> TypeTextProperty = AvaloniaProperty.Register<TypeEditor, string>(nameof(TypeText), "", defaultBindingMode: Avalonia.Data.BindingMode.TwoWay);
	public static readonly StyledProperty<IReadOnlyList<PickItem>?> ItemsProperty = AvaloniaProperty.Register<TypeEditor, IReadOnlyList<PickItem>?>(nameof(Items));

	private enum Container { Value, Array, Dictionary, Table }

	private readonly StackPanel m_buttons = new() { Orientation = Orientation.Horizontal, Spacing = 3 };
	private readonly TypePicker m_element = new();
	private readonly TypePicker m_key = new();
	private readonly Control m_keyRow;
	private readonly Control m_elementRow;
	private readonly TextBlock m_elementLabel = new() { Text = "Value", VerticalAlignment = VerticalAlignment.Center };
	private readonly Dictionary<Container, (Button Button, PinShapeIcon Icon)> m_containers = [];
	private Container m_container = Container.Value;
	private bool m_loading;
	private bool m_composePending;

	static TypeEditor() {
		TypeTextProperty.Changed.AddClassHandler<TypeEditor>((e, _) => e.Load());
		ItemsProperty.Changed.AddClassHandler<TypeEditor>((e, _) => {
			e.m_element.Items = e.Items;
			e.m_key.Items = e.Items;
		});
	}

	public TypeEditor() {
		// reparenting a picker during a popup click crashes avalonia
		foreach (var (container, tip) in new[] { (Container.Value, "Value"), (Container.Array, "Array"), (Container.Dictionary, "Dictionary"), (Container.Table, "Table") }) {
			var icon = new PinShapeIcon { Width = 20, Height = 20, IsPair = container == Container.Dictionary };
			var button = new Button {
				Content = icon,
				Width = 40,
				Height = 34,
				MinHeight = 0,
				Padding = new Thickness(0),
				HorizontalContentAlignment = HorizontalAlignment.Center,
				VerticalContentAlignment = VerticalAlignment.Center,
				CornerRadius = new CornerRadius(6),
				BorderThickness = new Thickness(0)
			};
			ToolTip.SetTip(button, tip);
			var picked = container;
			button.Click += (_, _) => {
				m_container = picked;
				Refresh();
				QueueCompose();
			};
			m_containers[container] = (button, icon);
			m_buttons.Children.Add(button);
		}
		m_element.PropertyChanged += (_, e) => {
			if (e.Property == TypePicker.ValueProperty) {
				Refresh();
				QueueCompose();
			}
		};
		m_key.PropertyChanged += (_, e) => {
			if (e.Property == TypePicker.ValueProperty) QueueCompose();
		};
		m_keyRow = Labelled(new TextBlock { Text = "Key", VerticalAlignment = VerticalAlignment.Center }, m_key);
		m_elementRow = Labelled(m_elementLabel, m_element);
		Content = new StackPanel { Spacing = 6, Children = { m_buttons, m_keyRow, m_elementRow } };
		Load();
	}

	public string TypeText {
		get => GetValue(TypeTextProperty);
		set => SetValue(TypeTextProperty, value);
	}

	public IReadOnlyList<PickItem>? Items {
		get => GetValue(ItemsProperty);
		set => SetValue(ItemsProperty, value);
	}

	private static IBrush? Brush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true) return r as IBrush;
		return null;
	}

	private void Load() {
		if (m_loading) return;
		m_loading = true;
		try {
			var text = TypeText.Trim();
			string element = text, key = "string";
			m_container = Container.Value;
			if (text.Length > 0) {
				try {
					switch (TypeExprParser.Parse(text)) {
						case ArrayType array:
							m_container = Container.Array;
							element = TypeFormatter.Format(array.Element);
							break;
						case TableType table:
							m_container = Container.Dictionary;
							key = TypeFormatter.Format(table.Key);
							element = TypeFormatter.Format(table.Value);
							break;
						case PrimitiveType { Kind: PrimitiveKind.Table }:
							m_container = Container.Table;
							element = "table";
							break;
					}
				} catch (TypeSyntaxException) {
				}
			}
			m_element.Value = element;
			m_key.Value = key;
			Refresh();
		} finally {
			m_loading = false;
		}
	}

	private (PinShape Shape, string Color) Look(Container container) {
		var color = PinStyle.ColorOf(Safe(m_element.Value));
		return container switch {
			Container.Array => (PinShape.Stack, color),
			Container.Table => (PinShape.Diamond, PinStyle.Table),
			Container.Dictionary => (PinShape.Circle, color),
			_ => (PinStyle.ShapeOf(Safe(m_element.Value)), color)
		};
	}

	private static LuaType Safe(string text) {
		try {
			return string.IsNullOrWhiteSpace(text) ? LuaType.Any : TypeExprParser.Parse(text);
		} catch (TypeSyntaxException) {
			return LuaType.Any;
		}
	}

	private void Refresh() {
		foreach (var (container, parts) in m_containers) {
			var (shape, color) = Look(container);
			var chosen = container == m_container;
			parts.Icon.Shape = shape;
			parts.Icon.Color = color;
			parts.Icon.Filled = chosen;
			parts.Button.Background = chosen ? Brush("Bg5") : Brush("Bg1");
			parts.Button.BorderThickness = new Thickness(chosen ? 1.5 : 0);
			parts.Button.BorderBrush = chosen ? Brush("TextMuted") : null;
		}
		m_keyRow.IsVisible = m_container == Container.Dictionary;
		m_elementRow.IsVisible = m_container != Container.Table;
		m_elementLabel.IsVisible = m_container == Container.Dictionary;
	}

	private static Control Labelled(Control label, Control picker) {
		var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*") };
		label.Margin = new Thickness(0, 0, 8, 0);
		if (label is TextBlock text) text.Foreground = Brush("TextMuted");
		grid.Children.Add(label);
		Grid.SetColumn(picker, 1);
		grid.Children.Add(picker);
		return grid;
	}

	private void QueueCompose() {
		if (m_loading || m_composePending) return;
		m_composePending = true;
		Dispatcher.UIThread.Post(() => {
			m_composePending = false;
			try {
				Compose();
			} catch (Exception e) {
				System.Diagnostics.Debug.WriteLine("TypeEditor: " + e.Message);
			}
		}, DispatcherPriority.Background);
	}

	private void Compose() {
		if (m_loading) return;
		var element = m_element.Value.Length == 0 ? "any" : m_element.Value;
		var key = m_key.Value.Length == 0 ? "string" : m_key.Value;
		var wrapped = element.Contains('|') || element.StartsWith("fun(", StringComparison.Ordinal) ? "(" + element + ")" : element;
		var text = m_container switch {
			Container.Array => wrapped + "[]",
			Container.Dictionary => $"table<{key}, {element}>",
			Container.Table => "table",
			_ => element
		};
		if (text == TypeText) return;
		m_loading = true;
		try {
			SetCurrentValue(TypeTextProperty, text);
		} finally {
			m_loading = false;
		}
	}
}
