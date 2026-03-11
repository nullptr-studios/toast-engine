{
  description = "gam250sp26 dev environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      llvmPkgs = pkgs.llvmPackages_19;
      customStdenv = llvmPkgs.stdenv;

      ultralightDeps = with pkgs; [
        gtk3
        cairo
        pango
        harfbuzz
        fontconfig
        atk
        gdk-pixbuf
        glib
        bzip2
        dbus
		# lsps
		typescript-language-server
		vscode-langservers-extracted
      ];
    in
    {
      devShells.${system}.default = pkgs.mkShell.override { stdenv = customStdenv; } {
        nativeBuildInputs = with pkgs; [
          cmake
          gnumake
          ninja
          pkg-config
          llvmPkgs.clang-tools
		  lldb
        ];

        buildInputs = with pkgs; [
		  lua5_4
		  lua54Packages.luafilesystem
          llvmPkgs.clang
          llvmPkgs.llvm
          wayland
          wayland-protocols
		  wayland-scanner
          libxkbcommon
          libX11
          libXcursor
          libXrandr
          libXi
          libXext
          libXfixes
          libXinerama
          libGL
        ] ++ ultralightDeps;

        shellHook = ''
          export CPLUS_INCLUDE_PATH="${pkgs.lib.makeSearchPathOutput "dev" "include" [ customStdenv.cc.cc ]}:${pkgs.lib.makeSearchPath "include" [ customStdenv.cc.cc ]}"
          
          export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [
            stdenv.cc.cc.lib
            libGL
          ] ++ ultralightDeps)}:$LD_LIBRARY_PATH"
          
          echo "gam250 environment loaded with $(clang --version | head -n1)"
        '';
      };
    };
}
