using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace editor.Components.Modals;

public static class TerminalReader {
	public const char LivePrefix = '\r';

	public static void Pump(StreamReader reader, Action<string> log) {
		var line = new StringBuilder();
		var overwrite = false;
		var buffer = new char[1024];
		var escape = Escape.None;
		int read;

		while ((read = reader.Read(buffer, 0, buffer.Length)) > 0) {
			for (var i = 0; i < read; i++) {
				var c = buffer[i];

				switch (escape) {
					case Escape.Start:
						escape = c switch { '[' => Escape.Csi, ']' => Escape.Osc, _ => Escape.None };
						continue;
					case Escape.Csi:
						if (c is >= '@' and <= '~') escape = Escape.None;
						continue;
					case Escape.Osc:
						if (c is '\a') escape = Escape.None;
						continue;
				}

				switch (c) {
					case '\x1b': escape = Escape.Start; break;
					case '\r': overwrite = true; break;
					case '\n':
						log(line.ToString());
						line.Clear();
						overwrite = false;
						break;
					default:
						if (overwrite) {
							line.Clear();
							overwrite = false;
						}
						line.Append(c);
						break;
				}
			}

			if (line.Length > 0) log(LivePrefix + line.ToString());
		}

		if (line.Length > 0) log(line.ToString());
	}

	// runs the process to the end sending its output through log
	public static int Run(Process proc, Action<string> log, Action<string>? onStderr = null) {
		proc.StartInfo.RedirectStandardOutput = true;
		proc.StartInfo.RedirectStandardError = true;
		proc.Start();

		var err = Task.Run(() => Pump(proc.StandardError, l => {
			log(l);
			if (onStderr is not null && l.Length > 0 && l[0] != LivePrefix) onStderr(l);
		}));
		Pump(proc.StandardOutput, log);
		err.Wait();
		proc.WaitForExit();
		return proc.ExitCode;
	}

	private enum Escape { None, Start, Csi, Osc }
}
