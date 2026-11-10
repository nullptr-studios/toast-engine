using System;
using System.Linq;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.LogicalTree;
using editor.Editors;
using editor.Workspace;

namespace editor.Components.Elements;

public sealed class LabelValueRow : Panel {
	private readonly Thumb m_grip;
	private string? m_loadedKey;
	private double? m_manualWidth;
	private double m_labelWidth = 110;
	private const double Gap = 6;

	public LabelValueRow() {
		m_grip = new Thumb {
			Cursor = new Cursor(StandardCursorType.SizeWestEast),
			Template = new FuncControlTemplate<Thumb>((_, _) => new Border { Background = Brushes.Transparent })
		};
		Children.Add(m_grip);
		m_grip.DragDelta += (_, e) => {
			var width = Math.Clamp(m_labelWidth + e.Vector.X, Math.Min(40, Bounds.Width / 2), Math.Max(40, Bounds.Width - Gap - 40));
			m_manualWidth = width;
			InvalidateMeasure();
		};
		m_grip.DragCompleted += (_, _) => {
			if (m_manualWidth is { } width && m_loadedKey is { } key) LabelWidthState.ForProject()?.Set(key, width);
		};
	}

	protected override void OnDataContextChanged(EventArgs e) {
		base.OnDataContextChanged(e);
		m_loadedKey = null; m_manualWidth = null;
		InvalidateMeasure();
	}

	private string? FieldKey() {
		var contexts = this.GetLogicalAncestors().OfType<Control>().Select(c => c.DataContext)
			.Prepend(DataContext).Where(c => c is not null).Distinct(ReferenceEqualityComparer.Instance).Reverse().ToArray();
		var pieces = new List<string>();
		GenericFieldVM? parent = null;
		foreach (var context in contexts) {
			switch (context) {
				case GenericViewModel editor: pieces.Add("data:" + (string.IsNullOrEmpty(editor.CurrentUid) ? editor.CurrentPath : editor.CurrentUid)); break;
				case ClassCardVM card: pieces.Add("inspector:" + card.TypeName); break;
				case FieldVM field: pieces.Add(string.IsNullOrEmpty(field.ParameterName) ? field.DisplayName : field.ParameterName); break;
				case GenericFieldVM field:
					pieces.Add(string.IsNullOrEmpty(field.Name) ? "item:" + (parent?.Children.IndexOf(field) ?? 0) : field.Name);
					parent = field; break;
			}
		}
		return pieces.Count == 0 ? null : System.Text.Json.JsonSerializer.Serialize(pieces);
	}

	protected override Size MeasureOverride(Size availableSize) {
		var parts = Children.Where(c => c != m_grip).ToArray();
		if (parts.Length != 2) return default;
		var key = FieldKey();
		if (key != m_loadedKey) {
			m_loadedKey = key;
			m_manualWidth = key is not null ? LabelWidthState.ForProject()?.Get(key) : null;
		}
		parts[0].ClipToBounds = true;
		parts[0].Measure(new Size(double.PositiveInfinity, availableSize.Height));
		var width = double.IsFinite(availableSize.Width) ? availableSize.Width : 360;
		var originalValueWidth = Math.Max(0, width - 110 - Gap);
		var maxLabelWidth = Math.Max(0, width - Gap - originalValueWidth / 2);
		m_labelWidth = Math.Clamp(m_manualWidth ?? Math.Max(110, parts[0].DesiredSize.Width), 0,
			m_manualWidth is null ? maxLabelWidth : Math.Max(0, width - Gap - 40));
		parts[0].Measure(new Size(m_labelWidth, availableSize.Height));
		parts[1].Measure(new Size(Math.Max(0, width - m_labelWidth - Gap), availableSize.Height));
		var height = Math.Max(parts[0].DesiredSize.Height, parts[1].DesiredSize.Height);
		m_grip.Measure(new Size(Gap, height));
		return new Size(width, height);
	}

	protected override Size ArrangeOverride(Size finalSize) {
		var parts = Children.Where(c => c != m_grip).ToArray();
		if (parts.Length != 2) return finalSize;
		parts[0].Arrange(new Rect(0, 0, m_labelWidth, finalSize.Height));
		parts[1].Arrange(new Rect(m_labelWidth + Gap, 0, Math.Max(0, finalSize.Width - m_labelWidth - Gap), finalSize.Height));
		m_grip.Arrange(new Rect(m_labelWidth, 0, Gap, finalSize.Height));
		return finalSize;
	}
}
