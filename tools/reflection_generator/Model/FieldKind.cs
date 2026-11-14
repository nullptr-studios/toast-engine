namespace ReflectionGenerator.Model;

internal enum FieldKind
{
	Bool,
	Int,
	Float,
	String,
	Double,
	Uid,
	Vec2,
	Vec3,
	Vec4,
	Quaternion,
}

internal static class FieldKindNames
{
	// these are the FieldType enumerators in reflect_node.hpp and the field_type values the editor reads
	public static string Name(this FieldKind kind) => kind switch {
		FieldKind.Bool => "bool_t",
		FieldKind.Int => "int_t",
		FieldKind.Float => "float_t",
		FieldKind.String => "string_t",
		FieldKind.Double => "double_t",
		FieldKind.Uid => "uid_t",
		FieldKind.Vec2 => "vec2_t",
		FieldKind.Vec3 => "vec3_t",
		FieldKind.Vec4 => "vec4_t",
		FieldKind.Quaternion => "quaternion_t",
		_ => throw new ArgumentOutOfRangeException(nameof(kind)),
	};
}
