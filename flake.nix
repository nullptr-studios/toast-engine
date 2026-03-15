{
	description = "toast-engine dev environment";

	inputs = {
		nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
	};

	outputs = { self, nixpkgs }:
	let
		system = "x86_64-linux";
		pkgs = nixpkgs.legacyPackages.${system};
	in
	{
		devShells.${system}.default = pkgs.mkShell {
			nativeBuildInputs = with pkgs; [
				xmake
				rustup
				protobuf
				pkg-config

				# LSPs
				clang-tools
				rust-analyzer
				buf

				lldb
			];

			buildInputs = with pkgs; [
				lua5_4
				lua54Packages.luafilesystem
				llvmPackages.clang
				llvmPackages.llvm
			];

			shellHook = ''
				echo "toast-engine environment loaded"
				echo "using compiler $(clang --version | head -n1)"
			'';
		};
	};
}
