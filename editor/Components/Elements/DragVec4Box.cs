//
// DragVec4Box.cs by Xein
// 23 Jun 2026
//

using Avalonia;
using Avalonia.Data;

namespace editor.Components.Elements;

public sealed class DragVec4Box : DragVectorBoxBase {
	public static readonly StyledProperty<float> XProperty =
		AvaloniaProperty.Register<DragVec4Box, float>(nameof(X), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> YProperty =
		AvaloniaProperty.Register<DragVec4Box, float>(nameof(Y), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> ZProperty =
		AvaloniaProperty.Register<DragVec4Box, float>(nameof(Z), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> WProperty =
		AvaloniaProperty.Register<DragVec4Box, float>(nameof(W), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<string> LabelXProperty =
		AvaloniaProperty.Register<DragVec4Box, string>(nameof(LabelX), "X");

	public static readonly StyledProperty<string> LabelYProperty =
		AvaloniaProperty.Register<DragVec4Box, string>(nameof(LabelY), "Y");

	public static readonly StyledProperty<string> LabelZProperty =
		AvaloniaProperty.Register<DragVec4Box, string>(nameof(LabelZ), "Z");

	public static readonly StyledProperty<string> LabelWProperty =
		AvaloniaProperty.Register<DragVec4Box, string>(nameof(LabelW), "W");

	public float X {
		get => GetValue(XProperty);
		set => SetValue(XProperty, value);
	}

	public float Y {
		get => GetValue(YProperty);
		set => SetValue(YProperty, value);
	}

	public float Z {
		get => GetValue(ZProperty);
		set => SetValue(ZProperty, value);
	}

	public float W {
		get => GetValue(WProperty);
		set => SetValue(WProperty, value);
	}

	public string LabelX {
		get => GetValue(LabelXProperty);
		set => SetValue(LabelXProperty, value);
	}

	public string LabelY {
		get => GetValue(LabelYProperty);
		set => SetValue(LabelYProperty, value);
	}

	public string LabelZ {
		get => GetValue(LabelZProperty);
		set => SetValue(LabelZProperty, value);
	}

	public string LabelW {
		get => GetValue(LabelWProperty);
		set => SetValue(LabelWProperty, value);
	}

	protected override int AxisCount => 4;

	protected override AxisSpec[] BuildAxes() {
		return [
			new AxisSpec(LabelXProperty, "Red", XProperty),
			new AxisSpec(LabelYProperty, "Green", YProperty),
			new AxisSpec(LabelZProperty, "Blue", ZProperty),
			new AxisSpec(LabelWProperty, "Magenta", WProperty)
		];
	}
}
