using Spectre.Console;
using Spectre.Console.Rendering;

namespace ReflectionGenerator;

// [=====>----] 3/10 path
internal sealed class BarColumn : ProgressColumn
{
	public override IRenderable Render(RenderOptions options, ProgressTask task, TimeSpan deltaTime)
	{
		var done = (int)task.Value;
		var total = (int)task.MaxValue;
		var counter = $" {done}/{total} ";

		var cols = Math.Max(options.ConsoleSize.Width, 20) - 1;
		var barWidth = Math.Min(40, Math.Max(cols - 2 - counter.Length, 0));
		var filled = total == 0 ? 0 : barWidth * done / total;

		var bar = new string('=', filled).PadRight(barWidth, '-');
		if (filled > 0 && done < total) bar = bar.Remove(filled - 1, 1).Insert(filled - 1, ">");

		var room = Math.Max(cols - 2 - barWidth - counter.Length, 0);
		var message = task.Description;
		if (message.Length > room) message = message[^room..];

		return new Markup($"[[[cyan]{bar}[/]]]{counter}{Markup.Escape(message)}");
	}
}
