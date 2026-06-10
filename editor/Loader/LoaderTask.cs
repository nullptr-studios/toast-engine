using System;
using System.Threading.Tasks;

namespace editor.Loader;

// A single unit of work shown in the loading screen
// Either a function (for editor-side operations) or a shell command (for cmake, git, etc.)
public record LoaderTask(
	string Label,
	Func<Action<string>, Task>? Action = null,
	string? Exe = null,
	string? Args = null
);
