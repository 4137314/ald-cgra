{
  description = "CGRA on FPGA: GHDL simulation, host C library + CLI, LaTeX docs";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      # `nix develop` — full toolchain to simulate, build the library/CLI and docs.
      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            # HDL simulation
            ghdl
            gtkwave
            # FPGA programming without Vivado
            openfpgaloader
            # host software
            gcc
            gnumake
            pkg-config
            clang-tools           # clangd for compile_flags.txt
            # documentation
            texliveSmall
          ];
          shellHook = ''
            echo "cgra dev shell — make sim | make sw | make test | make doc"
            echo "note: bitstream synthesis (make bit) still needs Vivado, which is not packaged here."
          '';
        };
      });

      # `nix build` — the host library + cgra CLI.
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "cgra";
          version = "0.1.0";
          src = ./.;
          nativeBuildInputs = [ pkgs.gnumake pkgs.gcc ];
          buildPhase = "make lib sw";
          installPhase = ''
            mkdir -p $out/bin $out/lib $out/include
            install -m755 sw/build/cgra           $out/bin/cgra
            install -m644 lib/build/libcgra.a     $out/lib/
            install -m644 lib/include/cgra.h      $out/include/
            mkdir -p $out/share/cgra
            cp -r sw/config/* $out/share/cgra/ 2>/dev/null || true
            mkdir -p $out/share/bash-completion/completions
            install -m644 sw/completions/cgra.bash \
              $out/share/bash-completion/completions/cgra 2>/dev/null || true
          '';
        };
      });
    };
}
