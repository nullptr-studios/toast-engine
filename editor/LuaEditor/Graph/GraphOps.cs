using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace editor.LuaEditor.Graph;

public sealed partial class LuaGraph {

	public LocalDecl NewLocal(string name) {
		if (!LuaWriter.IsIdentifier(name)) throw new ArgumentException($"\"{name}\" is not a valid name", nameof(name));
		var declaration = new LocalDecl(this);
		declaration.AddName(name);
		declaration.AddValue();
		return declaration;
	}

	public CallStatement NewCallStatement(string globalFunction, params LuaConstant[] arguments) {
		var call = new CallNode(this, CallStyle.Function, null);
		call.Target.Connect(new GlobalGet(this, globalFunction).Result);
		foreach (var argument in arguments) call.AddArg().SetConstant(argument);
		var statement = new CallStatement(this);
		statement.Call.Connect(call.Result);
		Adopt(statement);
		return statement;
	}

	public CallNode NewMethodCall(string method) {
		if (!LuaWriter.IsIdentifier(method)) throw new ArgumentException($"\"{method}\" cannot be written after a colon", nameof(method));
		return new CallNode(this, CallStyle.Method, method);
	}

	public void SetConstant(InputPin pin, LuaConstant constant) {
		var previous = pin.Source;
		pin.SetConstant(constant);
		Prune(previous);
	}

	public void Disconnect(InputPin pin) {
		var previous = pin.Source;
		pin.Clear();
		Prune(previous);
	}

	public void Connect(OutputPin from, InputPin to) {
		if (from.Owner.Graph != this || to.Owner.Graph != this) throw new InvalidOperationException("both pins must belong to this graph");
		if (DependsOn(from.Owner, to.Owner)) throw new InvalidOperationException("that connection would make a value depend on itself");

		var site = to.Owner.OwnerStatement;
		var source = from;
		VariableGet? get = null;
		if (from.Binding is { IsMutable: true } mutable) {
			get = new VariableGet(this, mutable) { OwnerStatement = site };
			source = get.Result;
		}

		var previous = to.Source;
		var previousConstant = to.Constant;
		to.Connect(source);
		if (site is not null) AdoptUnowned(source.Owner, site);
		// moving an expression can put its names out of scope
		var problem = site is null ? null : SiteProblem([site]);
		if (problem is not null) {
			if (previous is not null) to.Connect(previous);
			else if (previousConstant is not null) to.SetConstant(previousConstant);
			else to.Clear();
			if (get is not null) Unregister(get);
			throw new InvalidOperationException(problem);
		}
		Prune(previous);
	}

	private static bool DependsOn(GraphNode node, GraphNode target) {
		if (node == target) return true;
		return node.Inputs.Any(p => p.Source is { Binding: null } source && source.Owner is ExprNode child && DependsOn(child, target));
	}

	public T Insert<T>(Block block, int index, T statement) where T : Statement {
		if (statement.Parent is not null) throw new ArgumentException("the statement is already in a block; use Move", nameof(statement));
		if (index < 0 || index > block.Statements.Count) throw new ArgumentOutOfRangeException(nameof(index));
		block.Insert(index, statement);
		Adopt(statement);
		var problem = PlacementProblem(block, statement);
		if (problem is not null) {
			block.Remove(statement);
			throw new InvalidOperationException(problem);
		}
		RefreshMutability();
		return statement;
	}

	internal string? PlacementProblem(Block block, Statement statement)
		=> ReturnProblem(block)
		   ?? SiteProblem(WithDescendants(statement))
		   ?? SiteProblem(SitesAfter(statement), DeclaredNames(statement));

	public void Remove(Statement statement) {
		var parent = statement.Parent ?? throw new InvalidOperationException("the statement is not in a block");
		var inside = WithDescendants(statement).ToHashSet();
		// a method declaration cannot lose its table
		foreach (var binding in DeclaredBy(statement)) {
			var outside = binding.Output.Consumers.Where(c => !(c.Owner.OwnerStatement is { } s && inside.Contains(s))).ToList();
			var declared = outside.Select(c => c.Owner).OfType<FunctionDecl>().FirstOrDefault(f => f.Parent is not null);
			if (declared is not null) throw new InvalidOperationException($"{declared.Name} is declared on {binding.Name}; remove or move it first");
			// removing a value must not leave a call on nil
			if (outside.Any(IsPrefixPosition)) throw new InvalidOperationException($"{binding.Name} is still used as a function or a table; remove those uses first");
		}
		foreach (var binding in DeclaredBy(statement).Where(b => b.IsMutable)) {
			if (StillUsedOutside(binding, statement))
				throw new InvalidOperationException($"{binding.Name} is assigned or read elsewhere; remove those first");
		}
		parent.Remove(statement);
		RemoveTree(statement);
		foreach (var cast in Nodes.OfType<CastStatement>().Where(c => c.Target is { } target && !m_nodes.Contains(target.Declarer)).ToList()) cast.Target = null;
		RefreshMutability();
	}

	internal LocalDecl CaptureCallResults(CallStatement statement, IReadOnlyList<string> names) {
		var block = statement.Parent ?? throw new InvalidOperationException("wire the call into the flow first");
		var call = statement.CallNode ?? throw new InvalidOperationException("the statement has no call");
		var index = block.IndexOf(statement);
		var capture = new LocalDecl(this);
		foreach (var name in names) capture.AddName(name);
		capture.AddValue().Connect(call.Result);
		capture.LeadingLines.AddRange(statement.LeadingLines);
		statement.Call.Clear();
		block.Remove(statement);
		Unregister(statement);
		Insert(block, index, capture);
		return capture;
	}

	public void Move(Statement statement, Block to, int index) {
		var from = statement.Parent ?? throw new InvalidOperationException("the statement is not in a block");
		if (IsInside(to, statement)) throw new InvalidOperationException("a statement cannot be moved into itself");
		var fromIndex = from.IndexOf(statement);
		from.Remove(statement);
		if (from == to && index > fromIndex) index--;
		if (index < 0 || index > to.Statements.Count) {
			from.Insert(fromIndex, statement);
			throw new ArgumentOutOfRangeException(nameof(index));
		}
		to.Insert(index, statement);
		var names = DeclaredNames(statement);
		var problem = ReturnProblem(to)
		              ?? SiteProblem(WithDescendants(statement))
		              ?? SiteProblem(SitesAfter(statement), names)
		              ?? SiteProblem(SitesFrom(from, fromIndex), names);
		if (problem is not null) {
			to.Remove(statement);
			from.Insert(fromIndex, statement);
			throw new InvalidOperationException(problem);
		}
	}

	private static bool IsPrefixPosition(InputPin pin) => pin.Owner switch {
		CallNode call => pin == call.Target,
		MemberGet member => pin == member.Object,
		IndexGet index => pin == index.Object,
		AssignStatement assign => assign.Targets.Any(t => t.Object == pin || t.Key == pin),
		_ => false,
	};

	private static Statement? OwnerStatementOf(Block block) => block.Owner switch {
		Statement s => s,
		FunctionEntry { Owner: FunctionDecl declaration } => declaration,
		FunctionEntry { Owner: FunctionNode node } => node.OwnerStatement,
		_ => null,
	};

	private static bool IsInside(Block block, Statement statement) {
		var current = block;
		while (current is not null) {
			var owner = OwnerStatementOf(current);
			if (owner == statement) return true;
			current = owner?.Parent;
		}
		return false;
	}

	internal static List<Statement> Flatten(IEnumerable<Statement> statements) {
		var result = new List<Statement>();
		foreach (var statement in statements) {
			if (statement is SequenceStatement sequence) result.AddRange(sequence.Branches.SelectMany(b => Flatten(b.Statements)));
			else result.Add(statement);
		}
		return result;
	}

	private static bool IsPlacement(Statement s) => s is not (CommentStatement or CastStatement);

	private static string? ReturnProblem(Block block) {
		var effective = block;
		while (effective.Owner is SequenceStatement { Parent: { } parent }) effective = parent;
		var flat = Flatten(effective.Statements);
		for (var i = 0; i < flat.Count; i++)
			if (flat[i] is ReturnStatement && flat.Skip(i + 1).Any(IsPlacement)) return "a return must be the last statement of its block";
		return null;
	}

	private void Adopt(Statement statement) {
		void Walk(InputPin pin, Statement owner) {
			if (pin.Source is not { Binding: null } source || source.Owner is not ExprNode node) return;
			node.OwnerStatement = owner;
			foreach (var input in node.Inputs) Walk(input, owner);
			if (node is FunctionNode function) foreach (var inner in function.Entry.Body.Statements) Adopt(inner);
		}
		foreach (var pin in statement.Inputs) Walk(pin, statement);
		foreach (var block in statement.ChildBlocks) foreach (var inner in block.Statements) Adopt(inner);
	}

	private static void AdoptUnowned(GraphNode node, Statement site) {
		if (node is not ExprNode) return;
		node.OwnerStatement ??= site;
		foreach (var input in node.Inputs)
			if (input.Source is { Binding: null } source) AdoptUnowned(source.Owner, site);
	}

	private static IEnumerable<Binding> DeclaredBy(Statement s) => s switch {
		LocalDecl local => local.Names,
		FunctionDecl { FunctionBinding: { } binding } => [binding],
		// sequence branches share a scope in the saved script
		SequenceStatement sequence => sequence.Branches.SelectMany(b => b.Statements).SelectMany(DeclaredBy),
		_ => [],
	};

	private static ISet<string> DeclaredNames(Statement s) => DeclaredBy(s).Select(b => b.Name).ToHashSet();

	private bool StillUsedOutside(Binding binding, Statement removed) {
		var inside = WithDescendants(removed).ToHashSet();
		return Nodes.OfType<VariableGet>().Any(g => g.Binding == binding && g.Result.Consumers.Any(c => c.Owner.OwnerStatement is { } s && !inside.Contains(s)))
		       || Nodes.OfType<AssignStatement>().Any(a => !inside.Contains(a) && a.Parent is not null && a.Targets.Any(t => t.Binding == binding))
		       || Nodes.OfType<FunctionDecl>().Any(f => !inside.Contains(f) && f.Parent is not null && f.AssignedBinding == binding);
	}

	private void RemoveTree(Statement statement) {
		foreach (var block in statement.ChildBlocks.ToList())
			foreach (var child in block.Statements.ToList()) RemoveTree(child);
		foreach (var pin in statement.Inputs.ToList()) {
			var source = pin.Source;
			pin.Clear();
			Prune(source);
		}
		foreach (var output in statement.Outputs) foreach (var consumer in output.Consumers.ToList()) consumer.Clear();
		if (statement is FunctionDecl function) Unregister(function.Entry);
		Unregister(statement);
	}

	private void Prune(OutputPin? output) {
		if (output is null || output.Binding is not null || output.Owner is not ExprNode node) return;
		if (node.Outputs.Any(o => o.Consumers.Count > 0)) return;
		foreach (var pin in node.Inputs.ToList()) {
			var source = pin.Source;
			pin.Clear();
			Prune(source);
		}
		if (node is FunctionNode function) {
			foreach (var statement in function.Entry.Body.Statements.ToList()) RemoveTree(statement);
			Unregister(function.Entry);
		}
		Unregister(node);
	}

	private static IEnumerable<Binding> VisibleAt(Statement site)
		=> site.Parent is { } block ? VisibleFromBlock(block, block.IndexOf(site)) : [];

	internal static IEnumerable<Binding> VisibleFromBlock(Block block, int index) {
		var current = block;
		var position = index;
		while (current is not null) {
			for (var i = position - 1; i >= 0; i--)
				foreach (var binding in DeclaredBy(current.Statements[i]).Reverse()) yield return binding;

			Statement? next;
			switch (current.Owner) {
				case FunctionEntry entry:
					foreach (var parameter in Enumerable.Reverse(entry.Params)) yield return parameter;
					if (entry.Self is { } self) yield return self;
					if (entry.Owner is FunctionDecl { FunctionBinding: { } own }) yield return own;
					next = OwnerStatementOf(current);
					break;
				case NumericFor numeric:
					yield return numeric.Var;
					next = numeric;
					break;
				case GenericFor generic:
					foreach (var variable in Enumerable.Reverse(generic.Vars)) yield return variable;
					next = generic;
					break;
				case SequenceStatement sequence:
					for (var b = sequence.Branches.IndexOf(current) - 1; b >= 0; b--)
						foreach (var binding in Enumerable.Reverse(sequence.Branches[b].Statements.SelectMany(DeclaredBy))) yield return binding;
					next = sequence;
					break;
				case Statement owner:
					next = owner;
					break;
				default:
					yield break;
			}
			if (next?.Parent is not { } parent) yield break;
			current = parent;
			position = parent.IndexOf(next);
		}
	}

	private static IEnumerable<Binding> VisibleTo(InputPin pin, Statement site) {
		var visible = VisibleAt(site);
		if (site is RepeatStatement repeat && pin == repeat.Condition)
			visible = Enumerable.Reverse(repeat.Body.Statements.SelectMany(DeclaredBy)).Concat(visible);
		return visible;
	}

	private static Binding? BindingRead(InputPin pin) => pin.Source?.Binding ?? (pin.Source?.Owner as VariableGet)?.Binding;

	private static IEnumerable<Statement> WithDescendants(Statement statement) {
		yield return statement;
		foreach (var block in statement.ChildBlocks)
			foreach (var child in block.Statements)
				foreach (var inner in WithDescendants(child)) yield return inner;
		foreach (var pin in statement.Inputs)
			foreach (var closure in ClosuresIn(pin))
				foreach (var child in closure.Entry.Body.Statements)
					foreach (var inner in WithDescendants(child)) yield return inner;
	}

	internal static IEnumerable<FunctionNode> ClosuresIn(InputPin pin) {
		if (pin.Source is not { Binding: null } source || source.Owner is not ExprNode node) yield break;
		if (node is FunctionNode function) {
			yield return function;
			yield break;
		}
		foreach (var input in node.Inputs)
			foreach (var closure in ClosuresIn(input)) yield return closure;
	}

	private static IEnumerable<InputPin> PinsOf(Statement site) {
		IEnumerable<InputPin> Tree(InputPin pin) {
			yield return pin;
			if (pin.Source is { Binding: null } source && source.Owner is ExprNode node && node is not FunctionNode)
				foreach (var input in node.Inputs)
					foreach (var inner in Tree(input)) yield return inner;
		}
		return site.Inputs.SelectMany(Tree);
	}

	private static IEnumerable<Statement> SitesFrom(Block block, int index) {
		var current = block;
		var position = index;
		while (current is not null) {
			for (var i = position; i < current.Statements.Count; i++)
				foreach (var inner in WithDescendants(current.Statements[i])) yield return inner;
			switch (current.Owner) {
				case SequenceStatement sequence when sequence.Parent is { } parent:
					for (var b = sequence.Branches.IndexOf(current) + 1; b < sequence.Branches.Count; b++)
						foreach (var statement in sequence.Branches[b].Statements)
							foreach (var inner in WithDescendants(statement)) yield return inner;
					current = parent;
					position = parent.IndexOf(sequence) + 1;
					continue;
				case RepeatStatement repeat:
					yield return repeat;
					break;
			}
			yield break;
		}
	}

	private static IEnumerable<Statement> SitesAfter(Statement statement)
		=> statement.Parent is { } block ? SitesFrom(block, block.IndexOf(statement) + 1) : [];

	private static string? SiteProblem(IEnumerable<Statement> sites, ISet<string>? onlyNames = null) {
		foreach (var site in sites.Distinct()) {
			if (site.Parent is null) continue;
			List<Binding>? visible = null;
			List<Binding> Visible() => visible ??= VisibleAt(site).ToList();

			foreach (var pin in PinsOf(site)) {
				var binding = BindingRead(pin);
				var name = binding?.Name ?? (pin.Source is { Binding: null, Owner: GlobalGet global } ? global.Name : null);
				if (name is null || (onlyNames is not null && !onlyNames.Contains(name))) continue;
				// repeat conditions can read locals from the loop body
				var scope = site is RepeatStatement ? VisibleTo(((RepeatStatement)site).Condition, site).ToList() : Visible();
				if (binding is not null) {
					if (NameResolution(binding, scope) is { } problem) return problem;
				} else if (scope.Any(v => v.Name == name)) {
					return $"{name} would stop referring to the global of that name";
				}
			}

			var statementProblem = StatementProblem(site, onlyNames, Visible);
			if (statementProblem is not null) return statementProblem;
		}
		return null;
	}

	private static string? NameResolution(Binding binding, ICollection<Binding> scope) {
		if (!scope.Contains(binding)) return $"{binding.Name} is not in scope there";
		return scope.First(v => v.Name == binding.Name) != binding ? $"{binding.Name} would refer to a different name there" : null;
	}

	private static string? StatementProblem(Statement site, ISet<string>? onlyNames, Func<List<Binding>> visible) {
		bool Wanted(string? name) => name is not null && (onlyNames is null || onlyNames.Contains(name));

		switch (site) {
			case AssignStatement assign:
				foreach (var target in assign.Targets) {
					if (target.Binding is { } binding && Wanted(binding.Name)) {
						if (NameResolution(binding, visible()) is { } problem) return problem;
						if (binding.Attribute is not null) return $"{binding.Name} is declared <{binding.Attribute}> and cannot be assigned";
					} else if (target.Kind == TargetKind.Global && Wanted(target.Name) && visible().Any(v => v.Name == target.Name)) {
						return $"{target.Name} would stop referring to the global of that name";
					}
				}
				break;
			case FunctionDecl { IsLocal: false } function when function.Path.Count == 1:
				if (function.AssignedBinding is { } assigned) {
					if (Wanted(assigned.Name) && NameResolution(assigned, visible()) is { } problem) return problem;
				} else if (Wanted(function.Path[0]) && visible().Any(v => v.Name == function.Path[0])) {
					return $"{function.Path[0]} would stop referring to the global function of that name";
				}
				break;
			case CastStatement { Target: { } target } when Wanted(target.Name):
				if (NameResolution(target, visible()) is { } castProblem) return castProblem;
				break;
		}
		if (onlyNames is not null) return null;

		switch (site) {
			case BreakStatement when !InLoop(site):
				return "break needs a loop around it";
			case GotoStatement @goto when !LabelInReach(site, @goto.Label):
				return $"there is no label {@goto.Label} in reach of this goto";
		}
		foreach (var pin in PinsOf(site)) {
			if (pin.Source is { Binding: null, Owner: VarargNode } && !VarargAllowed(site)) return "... can only be used inside a function that takes ...";
		}
		return null;
	}

	private static IEnumerable<(Block Block, GraphNode? Owner)> EnclosingBlocks(Statement site) {
		var block = site.Parent;
		while (block is not null) {
			yield return (block, block.Owner);
			if (block.Owner is FunctionEntry) yield break;
			block = OwnerStatementOf(block)?.Parent;
		}
	}

	private static bool InLoop(Statement site)
		=> EnclosingBlocks(site).Any(e => e.Owner is WhileStatement or RepeatStatement or NumericFor or GenericFor);

	private static bool LabelInReach(Statement site, string label)
		=> EnclosingBlocks(site).Any(e => Flatten(e.Block.Statements).OfType<LabelStatement>().Any(l => l.Name == label));

	private static bool VarargAllowed(Statement site) {
		var function = EnclosingBlocks(site).Select(e => e.Owner).OfType<FunctionEntry>().FirstOrDefault();
		return function is null || function.IsVararg;
	}

	private static IEnumerable<Statement> RegionOf(Binding binding) => binding.Declarer switch {
		LocalDecl declaration => WithDescendants(declaration).Concat(SitesAfter(declaration)),
		FunctionDecl declaration => WithDescendants(declaration).Concat(SitesAfter(declaration)),
		FunctionEntry entry => entry.Body.Statements.SelectMany(WithDescendants),
		Statement loop => WithDescendants(loop),
		_ => [],
	};

	public void Rename(Binding binding, string newName) {
		if (!LuaWriter.IsIdentifier(newName)) throw new ArgumentException($"\"{newName}\" is not a valid name", nameof(newName));
		if (binding.Kind == BindingKind.Self) throw new InvalidOperationException("self comes from the colon in the function's name and cannot be renamed");
		var old = binding.Name;
		if (old == newName) return;

		var casts = Nodes.OfType<CastStatement>().Where(c => c.Target == binding).ToList();
		var annotated = binding is { Kind: BindingKind.Param, Declarer: FunctionEntry { Owner: FunctionDecl declaration } } ? declaration : null;
		var annotations = annotated?.LeadingLines.ToList();

		binding.Name = newName;
		foreach (var cast in casts) cast.Variable = newName;
		if (annotated is not null) {
			for (var i = 0; i < annotated.LeadingLines.Count; i++) {
				var line = annotated.LeadingLines[i];
				foreach (var suffix in new[] { " ", "? " }) {
					var prefix = $"---@param {old}{suffix}";
					if (line.StartsWith(prefix, StringComparison.Ordinal)) annotated.LeadingLines[i] = $"---@param {newName}{suffix}" + line[prefix.Length..];
				}
			}
		}

		var problem = SiteProblem(RegionOf(binding), new HashSet<string> { newName });
		if (problem is null) return;
		binding.Name = old;
		foreach (var cast in casts) cast.Variable = old;
		if (annotated is not null && annotations is not null) {
			annotated.LeadingLines.Clear();
			annotated.LeadingLines.AddRange(annotations);
		}
		throw new InvalidOperationException(problem);
	}

	private void RefreshMutability() {
		var counts = new Dictionary<Binding, int>();
		foreach (var statement in Nodes.OfType<Statement>().Where(s => s.Parent is not null)) {
			switch (statement) {
				case AssignStatement assign:
					foreach (var target in assign.Targets.Where(t => t.Binding is not null)) counts[target.Binding!] = counts.GetValueOrDefault(target.Binding!) + 1;
					break;
				case FunctionDecl { AssignedBinding: { } assigned }:
					counts[assigned] = counts.GetValueOrDefault(assigned) + 1;
					break;
			}
		}

		var bindings = Nodes.SelectMany(n => n switch {
			LocalDecl l => l.Names,
			FunctionDecl { FunctionBinding: { } f } => [f],
			NumericFor numeric => [numeric.Var],
			GenericFor generic => generic.Vars,
			FunctionEntry entry => entry.Params.Concat(entry.Self is null ? [] : [entry.Self]),
			_ => Enumerable.Empty<Binding>(),
		}).ToList();

		foreach (var binding in bindings) {
			binding.Assignments = counts.GetValueOrDefault(binding);
			var mutable = binding.Assignments > 0;
			if (mutable == binding.IsMutable) continue;
			binding.IsMutable = mutable;
			if (mutable) {
				foreach (var consumer in binding.Output.Consumers.ToList()) {
					var get = new VariableGet(this, binding) { OwnerStatement = consumer.Owner.OwnerStatement };
					consumer.Connect(get.Result);
				}
			} else {
				foreach (var get in Nodes.OfType<VariableGet>().Where(g => g.Binding == binding).ToList()) {
					foreach (var consumer in get.Result.Consumers.ToList()) consumer.Connect(binding.Output);
					Unregister(get);
				}
			}
		}
	}

	private static InputPin Unwrapped(InputPin pin) => pin.Source is { Binding: null, Owner: ParenNode paren } ? paren.Inner : pin;

	private static bool SamePureValue(InputPin x, InputPin y) {
		x = Unwrapped(x);
		y = Unwrapped(y);
		if (x.Constant is not null || y.Constant is not null) return x.Constant is not null && x.Constant == y.Constant;
		if (x.Source is not { } a || y.Source is not { } b) return false;
		if (a == b) return true;
		if (BindingRead(x) is { } bx && BindingRead(y) is { } by) return bx == by;
		return (a.Owner, b.Owner) switch {
			(GlobalGet gx, GlobalGet gy) => gx.Name == gy.Name,
			(MemberGet mx, MemberGet my) => mx.Name == my.Name && SamePureValue(mx.Object, my.Object),
			(IndexGet ix, IndexGet iy) => SamePureValue(ix.Object, iy.Object) && SamePureValue(ix.Key, iy.Key),
			_ => false,
		};
	}

	public void SetCallStyle(CallNode call, CallStyle style) {
		if (call.Style == style) return;
		if (style == CallStyle.Function) {
			ToDotForm(call);
			return;
		}

		var member = call.Target.Source is { Binding: null, Owner: MemberGet m } ? m : null;
		if (member is null || call.Args.Count == 0) throw new InvalidOperationException("only a call written as object.name(object, ...) can use the colon form");
		var self = call.Args[0];
		if (!SamePureValue(member.Object, self)) throw new InvalidOperationException("the first argument is not the object the method is taken from, so the colon form would change the call");
		if (!LuaWriter.IsIdentifier(member.Name)) throw new InvalidOperationException($"\"{member.Name}\" cannot be written after a colon");

		var objectSource = member.Object.Source;
		var objectConstant = member.Object.Constant;
		var selfSource = self.Source;
		call.RemoveArgAt(0);
		call.Target.Clear();
		if (objectSource is not null) call.Target.Connect(objectSource);
		else if (objectConstant is not null) call.Target.SetConstant(objectConstant);
		Prune(member.Result);
		Prune(selfSource);
		call.Style = CallStyle.Method;
		call.Method = member.Name;
		call.Target.Name = "object";
	}

	private void ToDotForm(CallNode call) {
		var name = call.Method ?? throw new InvalidOperationException("a method call needs a name");
		var member = new MemberGet(this, name) { OwnerStatement = call.OwnerStatement };
		var objectSource = call.Target.Source;
		var objectConstant = call.Target.Constant;
		if (objectSource is not null) member.Object.Connect(objectSource);
		else if (objectConstant is not null) member.Object.SetConstant(objectConstant);

		var self = call.InsertArg(0, "self");
		if (objectSource is { Binding: null, Index: 0, Owner: CallNode or VarargNode }) {
			// parentheses stop the receiver from expanding into multiple arguments
			var paren = new ParenNode(this) { OwnerStatement = call.OwnerStatement };
			paren.Inner.Connect(objectSource);
			self.Connect(paren.Result);
		} else if (objectSource is not null) {
			self.Connect(objectSource);
		} else if (objectConstant is not null) {
			self.SetConstant(objectConstant);
		}

		call.Target.Clear();
		call.Target.Connect(member.Result);
		call.Style = CallStyle.Function;
		call.Method = null;
		call.Target.Name = "function";
	}

	private static bool IsDocLine(string line) => line.StartsWith("---", StringComparison.Ordinal);

	public void SetStatementComment(Statement statement, string? text) {
		var annotations = statement.LeadingLines.Where(IsDocLine).ToList();
		// comment text must never become executable code
		var comment = string.IsNullOrEmpty(text)
			? []
			: Regex.Split(text, "\r\n|\r|\n").Select(l => l.Length == 0 ? "--" : "-- " + l).ToList();
		statement.LeadingLines.Clear();
		statement.LeadingLines.AddRange(comment);
		statement.LeadingLines.AddRange(annotations);
	}

	public void SetAnnotationLines(Statement statement, IReadOnlyList<string> lines) {
		if (lines.Any(l => !IsDocLine(l) || l.Contains('\n') || l.Contains('\r'))) throw new ArgumentException("annotation lines are single lines starting with ---", nameof(lines));
		var comment = statement.LeadingLines.Where(l => !IsDocLine(l)).ToList();
		statement.LeadingLines.Clear();
		statement.LeadingLines.AddRange(comment);
		statement.LeadingLines.AddRange(lines);
	}
}
