using System;
using System.Threading.Tasks;

namespace editor.Components.Modals;

/// <summary>
///    A step shown in the loading screen — use <see cref="Run" /> or <see cref="Do" /> to construct, not the record
///    directly.
/// </summary>
public abstract record LoaderTask(string Label) {
	/// <summary>Shell process — stdout/stderr goes to the log.</summary>
	public static LoaderTask Run(string label, string exe, string args) {
		return new ProcessTask(label, exe, args);
	}

	/// <summary>C# async delegate with a log callback.</summary>
	public static LoaderTask Do(string label, Func<Action<string>, Task> action) {
		return new ActionTask(label, action);
	}
}

internal sealed record ProcessTask(string Label, string Exe, string Args) : LoaderTask(Label);

internal sealed record ActionTask(string Label, Func<Action<string>, Task> Action) : LoaderTask(Label);
