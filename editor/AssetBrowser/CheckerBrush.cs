using Avalonia;
using Avalonia.Media;

namespace editor.AssetBrowser;

public static class CheckerBrush {
	private static IBrush? s_instance;
	public static IBrush Instance => s_instance ??= Create();

	private static IBrush Create() {
		const int s = 10;
		var dark = new SolidColorBrush(Color.FromRgb(0x1e, 0x1e, 0x1e));
		var light = new SolidColorBrush(Color.FromRgb(0x30, 0x30, 0x30));

		var group = new DrawingGroup();
		group.Children.Add(new GeometryDrawing {
			Brush = dark,
			Geometry = new RectangleGeometry { Rect = new Rect(0, 0, s * 2, s * 2) }
		});
		group.Children.Add(new GeometryDrawing {
			Brush = light,
			Geometry = Geometry.Parse($"M0,0 H{s} V{s} H0Z M{s},{s} H{s * 2} V{s * 2} H{s}Z")
		});

		return new DrawingBrush {
			Drawing = group,
			TileMode = TileMode.Tile,
			DestinationRect = new RelativeRect(0, 0, s * 2, s * 2, RelativeUnit.Absolute),
			Stretch = Stretch.None
		};
	}
}
