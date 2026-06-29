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

		libraries = with pkgs; [
			lua5_4
			lua54Packages.luafilesystem
			llvmPackages.clang
			llvmPackages.llvm

			vulkan-loader
			vulkan-validation-layers
			vulkan-extension-layer
			vulkan-headers

			libxkbcommon
			libX11
			libXcursor
			libXtst
			libxscrnsaver
			xcbutilcursor
			libXrandr
			libXi
			libXext
			libXfixes
			libXinerama
			libXft
			libXrender
			libxcb
			xcbutil
			xcbutilimage
			xcbutilkeysyms
			xcbutilwm

			libglvnd
			libGL
			wayland
			wayland-protocols
			dbus
			ibus
			udev
			mesa

			sdl3
			fontconfig
			libICE
			libSM

			zip
			unzip
			gnutar
			curl
		];

		runtimeLibs = with pkgs; [
			dotnet-sdk
			icu
			openssl
			zlib
			stdenv.cc.cc.lib
		] ++ libraries;

		cmake-gen = pkgs.writeShellScriptBin "cmake-gen" ''
			exec cmake -B build/Debug -G Ninja -DCMAKE_TOOLCHAIN_FILE=${pkgs.vcpkg}/share/vcpkg/scripts/buildsystems/vcpkg.cmake "$@"
		'';

		cmake-build = pkgs.writeShellScriptBin "cmake-build" ''
			exec cmake --build ./build/Debug "$@"
		'';
	in
	{
		devShells.${system}.default = pkgs.mkShell {
			nativeBuildInputs = with pkgs; [
				cmake
				ninja
				valgrind
				rustup
				protobuf
				grpc
				pkg-config
				vcpkg
				dotnet-sdk
				clang-tools
				rust-analyzer
				buf
				roslyn-ls
				gdb
				lldb
				pkg-config
				cmake-gen
				cmake-build

				autoconf
				autoconf-archive
				automake
				libtool

				ktx-tools
				vulkan-tools
				wayland-scanner

			];

			buildInputs = libraries;

			hardeningDisable = [ "fortify" ];

			shellHook = ''
				export DOTNET_ROOT="${dotnet-sdk}/share/dotnet";
				export PATH="${dotnet-sdk}/bin:$PATH";
				export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath runtimeLibs}:$LD_LIBRARY_PATH";
				export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1;
				echo "toast-engine environment loaded"
				export VCPKG_FORCE_SYSTEM_BINARIES=1
				export VCPKG_ROOT="${pkgs.vcpkg}/share/vcpkg"
				export NIX_SSL_CERT_FILE="${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
				export PKG_CONFIG="${pkgs.pkg-config}/bin/pkg-config"
				export VCPKG_KEEP_ENV_VARS=$(env | grep -E '^(NIX_|PKG_CONFIG)' | cut -d= -f1 | tr '\n' ';')
				export PROTOBUF_PROTOC="${pkgs.protobuf}/bin/protoc"
				export GRPC_PROTOC_PLUGIN="${pkgs.grpc}/bin/grpc_csharp_plugin"
				export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d:${pkgs.vulkan-extension-layer}/share/vulkan/explicit_layer.d";
				'';
		};
	};
}
