class [[ToastNode]] PlayerForDante {
	int health = 5;

	[[NoSerialize]]
	float timer = 0.0f;

	struct M{
		int a;
		int b;

		[[Serialize]] [[Dropdown]]
		int c;
	}m;
	public:

	float health_damage;

	[[Serialize, Slider(1,2)]]
	float squimble_factor = 0;

};
