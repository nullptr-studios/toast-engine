using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Graph;

public enum VariableKind { Field, ModuleLocal, Function }

public sealed record GraphVariable(string Name, VariableKind Kind, bool Exported, LuaType Type, string? Unit, string Doc, Statement Declaration);

public sealed partial class LuaGraph {
	public IReadOnlyList<GraphVariable> Variables {
		get {
			var script = ScriptInfo.Find(this);
			var result = new List<GraphVariable>();
			var seenFields = new HashSet<string>();
			foreach (var statement in Module.Statements) {
				switch (statement) {
					case AssignStatement assign when script?.Fields.FirstOrDefault(f => f.Statement == assign) is { } field: {
						if (!seenFields.Add(field.Name)) break;
						var declared = field.Annotations.Tags.OfType<TypeTag>().FirstOrDefault()?.Type;
						var type = declared ?? Typer.FieldTypeFrom(field.Value.ActualType);
						result.Add(new GraphVariable(field.Name, VariableKind.Field, !field.Name.StartsWith('_'), type, UnitOf(field.Annotations), field.Annotations.Doc, assign));
						break;
					}
					case LocalDecl local when local != script?.Declaration: {
						var annotations = ScriptInfo.AnnotationsOf(local);
						var declared = annotations.Tags.OfType<TypeTag>().FirstOrDefault()?.Type;
						for (var i = 0; i < local.Names.Count; i++) {
							var type = (i == 0 ? declared : null) ?? (local.Names[i].Type == LuaType.Unknown ? LuaType.Any : local.Names[i].Type);
							result.Add(new GraphVariable(local.Names[i].Name, VariableKind.ModuleLocal, false, type, UnitOf(annotations), annotations.Doc, local));
						}
						break;
					}
					case FunctionDecl function: {
						var annotations = ScriptInfo.AnnotationsOf(function);
						result.Add(new GraphVariable(function.Name, VariableKind.Function, !function.IsLocal && !function.Name.StartsWith('_'),
							ScriptInfo.SignatureOf(function.Entry, annotations), UnitOf(annotations), annotations.Doc, function));
						break;
					}
				}
			}
			return result;
		}
	}

	private static string? UnitOf(AnnotationBlock annotations) => annotations.Tags.OfType<UnitTag>().FirstOrDefault()?.Unit;

	private static void SetLine(List<string> lines, Func<string, bool> matches, string? newLine, Func<string, bool>? insertBefore = null) {
		var first = lines.FindIndex(l => matches(l));
		lines.RemoveAll(l => matches(l));
		if (newLine is null) return;
		var at = first;
		if (at < 0) {
			var before = insertBefore is null ? -1 : lines.FindIndex(l => insertBefore(l));
			at = before >= 0 ? before : lines.Count;
		}
		lines.Insert(Math.Min(at, lines.Count), newLine);
	}

	public void SetDeclaredType(Statement declaration, LuaType? type)
		=> SetLine(declaration.LeadingLines, l => l.StartsWith("---@type ", StringComparison.Ordinal), type is null ? null : "---@type " + TypeFormatter.Format(type));

	public void SetUnit(Statement declaration, string? unit)
		=> SetLine(declaration.LeadingLines, l => l.StartsWith("---@unit", StringComparison.Ordinal), string.IsNullOrWhiteSpace(unit) ? null : "---@unit " + unit.Trim());

	public void SetDoc(Statement declaration, string? text) {
		var lines = declaration.LeadingLines;
		bool IsDoc(string l) => l.StartsWith("---", StringComparison.Ordinal) && !l.StartsWith("---@", StringComparison.Ordinal);
		var at = lines.FindIndex(IsDoc);
		if (at < 0) at = lines.FindIndex(l => l.StartsWith("---", StringComparison.Ordinal));
		if (at < 0) at = lines.Count;
		var removedBefore = lines.Take(at).Count(IsDoc);
		lines.RemoveAll(IsDoc);
		at -= removedBefore;
		if (string.IsNullOrEmpty(text)) return;
		lines.InsertRange(Math.Min(at, lines.Count), text.Replace("\r\n", "\n").Split('\n').Select(l => "---" + l));
	}

	public void SetParamType(FunctionDecl function, string parameter, LuaType? type) {
		if (function.Entry.Params.All(p => p.Name != parameter) && !(parameter == "..." && function.Entry.IsVararg))
			throw new ArgumentException($"{function.Name} has no parameter {parameter}", nameof(parameter));
		SetLine(function.LeadingLines,
			l => l.StartsWith($"---@param {parameter} ", StringComparison.Ordinal) || l.StartsWith($"---@param {parameter}? ", StringComparison.Ordinal),
			type is null ? null : $"---@param {parameter} {TypeFormatter.Format(type)}",
			l => l.StartsWith("---@return", StringComparison.Ordinal));
	}

	public void SetScriptClass(string? name, string? baseName) {
		var script = ScriptInfo.Find(this) ?? throw new InvalidOperationException("this file does not define a script class");
		if (name is { Length: > 0 } && !LuaWriter.IsIdentifier(name)) throw new ArgumentException($"\"{name}\" is not a valid class name", nameof(name));
		var lines = script.Declaration.LeadingLines;
		lines.RemoveAll(l => l.StartsWith("---@class", StringComparison.Ordinal));
		if (string.IsNullOrEmpty(name)) return;
		lines.Insert(0, "---@class " + name + (string.IsNullOrEmpty(baseName) ? "" : " : " + baseName));
	}

	public void AddParam(FunctionDecl function, string name) {
		if (!LuaWriter.IsIdentifier(name)) throw new ArgumentException($"\"{name}\" is not a valid name", nameof(name));
		if (function.Entry.Params.Any(p => p.Name == name) || function.Entry.Self?.Name == name) throw new InvalidOperationException($"{function.Name} already has a parameter called {name}");
		function.Entry.AddParam(name, BindingKind.Param);
	}

	public void RemoveParam(FunctionDecl function, string name) {
		var binding = function.Entry.Params.FirstOrDefault(p => p.Name == name) ?? throw new ArgumentException($"{function.Name} has no parameter {name}", nameof(name));
		if (binding.IsMutable || Nodes.OfType<CastStatement>().Any(c => c.Target == binding)) throw new InvalidOperationException($"{name} is assigned or cast in the body; remove that first");
		foreach (var consumer in binding.Output.Consumers.ToList()) Disconnect(consumer);
		function.LeadingLines.RemoveAll(l => l.StartsWith($"---@param {name} ", StringComparison.Ordinal) || l.StartsWith($"---@param {name}? ", StringComparison.Ordinal));
		function.Entry.RemoveParam(binding);
	}

	public void SetReturnTypes(FunctionDecl function, IReadOnlyList<LuaType> types) {
		function.LeadingLines.RemoveAll(l => l.StartsWith("---@return", StringComparison.Ordinal));
		function.LeadingLines.AddRange(types.Select(t => "---@return " + TypeFormatter.Format(t)));
	}

	private static bool IsClass(LuaType type, string className)
		=> type is NamedType named ? named.Name == className : type is UnionType union && union.Members.Any(m => m is NamedType n && n.Name == className);

	public void RenameMember(string oldName, string newName) {
		if (!LuaWriter.IsIdentifier(newName)) throw new ArgumentException($"\"{newName}\" is not a valid name", nameof(newName));
		var script = ScriptInfo.Find(this) ?? throw new InvalidOperationException("this file does not define a script class");
		if (!script.Fields.Any(f => f.Name == oldName) && !script.Methods.Any(m => m.Name == oldName))
			throw new InvalidOperationException($"the script has no field or method {oldName}");
		if (script.Fields.Any(f => f.Name == newName) || script.Methods.Any(m => m.Name == newName))
			throw new InvalidOperationException($"the script already has {newName}");

		foreach (var parent in (script.ClassTag?.Bases ?? []).OfType<NamedType>()) {
			var inherited = Universe.MembersOf(parent.Name).Select(m => m.Decl.Name).ToHashSet();
			if (inherited.Contains(oldName)) throw new InvalidOperationException($"{oldName} comes from {parent.Name} and cannot be renamed here");
			if (inherited.Contains(newName)) throw new InvalidOperationException($"{newName} is already a member of {parent.Name}");
		}

		Analyze();
		bool OnScript(InputPin? pin) => pin is not null && IsClass(pin.ActualType, script.ClassName);
		var attached = Nodes.Where(n => n.OwnerStatement?.Parent is not null).ToList();
		foreach (var member in attached.OfType<MemberGet>().Where(m => m.Name == oldName && OnScript(m.Object))) member.Name = newName;
		foreach (var assign in attached.OfType<AssignStatement>())
			foreach (var target in assign.Targets.Where(t => t.Kind == TargetKind.Member && t.Name == oldName && OnScript(t.Object))) target.Name = newName;
		foreach (var call in attached.OfType<CallNode>().Where(c => c.Style == CallStyle.Method && c.Method == oldName && OnScript(c.Target))) call.Method = newName;
		foreach (var method in script.Methods.Where(m => m.Name == oldName)) method.Path[^1] = newName;
	}
}
