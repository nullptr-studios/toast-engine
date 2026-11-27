//
// ColorWheelsPanel.cs
// 27 Nov 2026
//

using System.Linq;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Layout;
using editor.Components.Elements;
using Lucide.Avalonia;

namespace editor.Editors;

/// Lift, gamma and gain wheels for an object field marked x-toast-widget "color_wheels"
public class ColorWheelsPanel : UserControl {
	private static readonly (string Label, string Tint, string Offset)[] Wheels = [
		("Lift", "lift", "lift_offset"),
		("Gamma", "gamma", "gamma_offset"),
		("Gain", "gain", "gain_offset")
	];

	public ColorWheelsPanel() {
		DataContextChanged += (_, _) => Build();
	}

	private void Build() {
		Content = null;
		// Every object row hosts this panel, only the marked one may claim its children
		if (DataContext is not GenericFieldVM { IsColorWheels: true } group) return;

		var stack = new StackPanel { Spacing = 12, Margin = new(0, 4, 0, 4) };
		foreach (var (label, tintName, offsetName) in Wheels) {
			var tint = group.Children.FirstOrDefault(c => c.Name == tintName && c.IsColor3);
			var offset = group.Children.FirstOrDefault(c => c.Name == offsetName && c.IsFloat);
			// A schema missing a field keeps its plain row rather than losing the control
			if (tint is null || offset is null) continue;
			tint.OwnedByWidget = true;
			offset.OwnedByWidget = true;

			stack.Children.Add(BuildWheel(label, tint, offset));
		}

		Content = stack;
	}

	private static StackPanel BuildWheel(string label, GenericFieldVM tint, GenericFieldVM offset) {
		var wheel = new ColorWheel { HorizontalAlignment = HorizontalAlignment.Center };
		wheel.Bind(ColorWheel.RProperty, new Binding(nameof(GenericFieldVM.X)) { Source = tint, Mode = BindingMode.TwoWay });
		wheel.Bind(ColorWheel.GProperty, new Binding(nameof(GenericFieldVM.Y)) { Source = tint, Mode = BindingMode.TwoWay });
		wheel.Bind(ColorWheel.BProperty, new Binding(nameof(GenericFieldVM.Z)) { Source = tint, Mode = BindingMode.TwoWay });

		var brightness = new DragFloatBox {
			Minimum = offset.Minimum, Maximum = offset.Maximum, CoarseStep = 0.01f, Classes = { "compact-field" }
		};
		brightness.Bind(DragFloatBox.ValueProperty,
			new Binding(nameof(GenericFieldVM.FloatVal)) { Source = offset, Mode = BindingMode.TwoWay });
		ToolTip.SetTip(brightness, "Brightness");

		var reset = new Button {
			Classes = { "icon-btn", "compact-control" },
			Content = new LucideIcon { Kind = LucideIconKind.RotateCcw, Size = 14, StrokeWidth = 2.5 },
			VerticalAlignment = VerticalAlignment.Center
		};
		ToolTip.SetTip(reset, $"Reset {label.ToLowerInvariant()}");
		reset.Click += (_, _) => {
			wheel.Reset();
			offset.FloatVal = 0f;
		};

		var header = new DockPanel();
		DockPanel.SetDock(reset, Avalonia.Controls.Dock.Right);
		header.Children.Add(reset);
		header.Children.Add(new TextBlock {
			Text = label, Classes = { "lbl", "compact-text" }, VerticalAlignment = VerticalAlignment.Center
		});

		return new StackPanel { Spacing = 4, Margin = new(4, 0), Children = { header, wheel, brightness } };
	}
}
