using System;
using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Graph;

public sealed partial class LuaGraph {

	public SequenceStatement GroupAsSequence(Block block, IReadOnlyList<int> startIndices) {
		if (startIndices.Count == 0) throw new ArgumentException("a sequence needs at least one branch", nameof(startIndices));
		for (var i = 0; i < startIndices.Count; i++) {
			if (startIndices[i] < 0 || startIndices[i] >= block.Statements.Count) throw new ArgumentException($"{startIndices[i]} is not a statement of the block", nameof(startIndices));
			if (i > 0 && startIndices[i] <= startIndices[i - 1]) throw new ArgumentException("the start indices must increase", nameof(startIndices));
		}

		var sequence = new SequenceStatement(this);
		var statements = block.Statements.ToList();
		for (var i = 0; i < startIndices.Count; i++) {
			var end = i + 1 < startIndices.Count ? startIndices[i + 1] : statements.Count;
			var branch = sequence.AddBranch();
			for (var s = startIndices[i]; s < end; s++) {
				block.Remove(statements[s]);
				branch.Add(statements[s]);
			}
		}
		block.Insert(startIndices[0], sequence);
		return sequence;
	}

	public void MaterializeSharedValues() {
		for (var guard = 0; guard < 10_000; guard++) {
			var shared = Nodes.OfType<ExprNode>().FirstOrDefault(n => n.OwnerStatement?.Parent is not null && HasEffects(n) && n.Outputs.Sum(o => o.Consumers.Count) >= 2);
			if (shared is null) return;
			Materialize(shared);
		}
	}

	private static bool HasEffects(GraphNode node)
		=> node is CallNode or TableNode or FunctionNode or RawExpression
		   || node.Inputs.Any(p => p.Source is { Binding: null } source && HasEffects(source.Owner));

	internal bool TryRestoreSharedValue(OutputPin output, InputPin input) {
		if (input.Source == output) return false;
		if (output.Owner is not ExprNode || input.Source?.Owner is not ExprNode previous ||
			output.Consumers.FirstOrDefault() is not { } original ||
			HasEffects(output.Owner) || HasEffects(previous) || !GraphSignature.SameExpression(original, input)) return false;
		try {
			Connect(output, input);
			return true;
		} catch (InvalidOperationException) {
			return false;
		}
	}

	private static IEnumerable<Block> BlocksAround(Statement statement) {
		var block = statement.Parent;
		while (block is not null) {
			yield return block;
			block = OwnerStatementOf(block)?.Parent;
		}
	}

	private static Statement? AncestorIn(Block block, Statement statement) {
		Statement? current = statement;
		while (current is not null) {
			if (current.Parent == block) return current;
			current = current.Parent is { } parent ? OwnerStatementOf(parent) : null;
		}
		return null;
	}

	private static string? EvaluationProblem(InputPin pin) {
		var owner = pin.Owner;
		switch (owner) {
			case WhileStatement @while when pin == @while.Condition:
			case RepeatStatement repeat when pin == repeat.Condition:
				return "a loop condition is evaluated again every time round, so its value cannot be kept in a local";
			case IfStatement @if when @if.Clauses.FindIndex(c => c.Condition == pin) > 0:
				return "an elseif condition is only evaluated when the earlier ones failed, so its value cannot be kept in a local";
			case BinaryOp { Op: BinaryKind.And or BinaryKind.Or } logic when pin == logic.Right:
				return "the right side of and/or is only evaluated sometimes, so its value cannot be kept in a local";
		}
		if (owner is ExprNode node)
			foreach (var consumer in node.Outputs.SelectMany(o => o.Consumers))
				if (EvaluationProblem(consumer) is { } problem) return problem;
		return null;
	}

	private static bool GivesAllValues(InputPin pin) {
		if (pin.Source is not { Binding: null, Index: 0, Owner: CallNode or VarargNode }) return false;
		return pin.Owner switch {
			CallNode call => call.Args.Count > 0 && call.Args[^1] == pin,
			ReturnStatement r => r.Values.Count > 0 && r.Values[^1] == pin,
			LocalDecl l => l.Values.Count > 0 && l.Values[^1] == pin,
			AssignStatement a => a.Values.Count > 0 && a.Values[^1] == pin,
			GenericFor g => g.Exprs.Count > 0 && g.Exprs[^1] == pin,
			TableNode t => t.Fields.Count > 0 && t.Fields[^1].Kind == TableFieldKind.Positional && t.Fields[^1].Value == pin,
			_ => false,
		};
	}

	private HashSet<string> NamesInUse() {
		var used = new HashSet<string>(Nodes.OfType<GlobalGet>().Select(g => g.Name));
		foreach (var node in Nodes) {
			switch (node) {
				case LocalDecl local:
					foreach (var name in local.Names) used.Add(name.Name);
					break;
				case FunctionEntry entry:
					foreach (var parameter in entry.Params) used.Add(parameter.Name);
					if (entry.Self is { } self) used.Add(self.Name);
					break;
				case NumericFor numeric:
					used.Add(numeric.Var.Name);
					break;
				case GenericFor generic:
					foreach (var variable in generic.Vars) used.Add(variable.Name);
					break;
				case FunctionDecl function:
					used.Add(function.Path[0]);
					if (function.FunctionBinding is { } binding) used.Add(binding.Name);
					break;
				case AssignStatement assign:
					foreach (var target in assign.Targets.Where(t => t.Kind == TargetKind.Global)) used.Add(target.Name!);
					break;
			}
		}
		return used;
	}

	private void Materialize(ExprNode node) {
		var consumers = node.Outputs.SelectMany(o => o.Consumers).ToList();
		foreach (var consumer in consumers) {
			if (EvaluationProblem(consumer) is { } problem) throw new InvalidOperationException(problem);
			if (GivesAllValues(consumer)) throw new InvalidOperationException("this call's values are all used in one place and the call is also used somewhere else; wrap one use in parentheses to take only the first value");
		}

		var statements = consumers.Select(c => c.Owner.OwnerStatement).Append(node.OwnerStatement).OfType<Statement>().Distinct().ToList();
		var block = BlocksAround(statements[0]).First(b => statements.All(s => AncestorIn(b, s) is not null));
		var index = statements.Select(s => block.IndexOf(AncestorIn(block, s)!)).Min();

		var used = NamesInUse();
		string Fresh() {
			var number = 1;
			while (used.Contains("v" + number)) number++;
			used.Add("v" + number);
			return "v" + number;
		}

		// shared results must not evaluate the call more than once
		var last = node.Outputs.Where(o => o.Consumers.Count > 0).Max(o => o.Index);
		var declaration = new LocalDecl(this);
		for (var i = 0; i <= last; i++) declaration.AddName(node.Outputs[i].Consumers.Count > 0 ? Fresh() : "_");
		var value = declaration.AddValue();
		value.Connect(node.Outputs[0]);

		try {
			Insert(block, index, declaration);
		} catch (InvalidOperationException) {
			value.Clear();
			Discard(declaration);
			throw;
		}
		foreach (var output in node.Outputs)
			foreach (var consumer in output.Consumers.Where(c => c != value).ToList()) consumer.Connect(declaration.Outputs[output.Index]);
	}

	internal void Discard(Statement statement) {
		statement.Parent?.Remove(statement);
		RemoveTree(statement);
		RefreshMutability();
	}

	internal void ForgetNodesAfter(int count) {
		foreach (var node in m_nodes.Skip(count).ToList()) {
			foreach (var pin in node.Inputs) pin.Clear();
			foreach (var output in node.Outputs) foreach (var consumer in output.Consumers.ToList()) consumer.Clear();
			m_nodes.Remove(node);
		}
	}
}
