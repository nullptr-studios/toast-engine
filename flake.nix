{
	description = "toast-engine dev environment";

	inputs = {
		nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
	};

	outputs = { self, nixpkgs }:
	let
		system = "x86_64-linux";
		pkgs = nixpkgs.legacyPackages.${system};
		dotnet-sdk = pkgs.dotnetCorePackages.sdk_10_0;
		runtimeLibs = with pkgs; [
			dotnet-sdk
			icu
			openssl
			zlib
			stdenv.cc.cc.lib
		];
	in
	{
		devShells.${system}.default = pkgs.mkShell {
			nativeBuildInputs = with pkgs; [
				xmake
				rustup
				protobuf
				pkg-config
				dotnet-sdk
				clang-tools
				rust-analyzer
				buf
				roslyn-ls
				lldb
			];

			buildInputs = with pkgs; [
				lua5_4
				lua54Packages.luafilesystem
				llvmPackages.clang
				llvmPackages.llvm
			];

			shellHook = ''
				export DOTNET_ROOT="${dotnet-sdk}/share/dotnet";
				export PATH="${dotnet-sdk}/bin:$PATH";
				export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeLibs}:$LD_LIBRARY_PATH";
				export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1;
				echo "toast-engine environment loaded"
			'';
		};
	};
}
