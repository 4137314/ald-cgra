{
  description = "CGRA on FPGA: GHDL simulation, host C library + CLI, LaTeX docs";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      version = "0.1.0";
    in
    {
      # `nix develop` - full toolchain to simulate, build/test the library+CLI,
      # and build the docs. This is the *primary*, standard-Unix workflow;
      # `nix build`/`nix profile install` below are the optional Nix-native path.
      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            # HDL simulation
            ghdl gtkwave
            # FPGA programming without Vivado
            openfpgaloader
            # host software
            gcc gnumake pkg-config
            readline                # `cgra shell` line editing
            clang-tools             # clangd (make compdb -> compile_commands.json)
            bear                    # alt. compile_commands.json generator
            # profiling / bug-hunting
            valgrind                # make valgrind / callgrind
            linuxPackages.perf      # make perf
            binutils                # gprof (make gprof)
            # documentation
            texinfo groff           # man pages + GNU info manual
            texliveMedium           # LaTeX report (needs tikz/pgf -> scheme-medium)
          ];
          shellHook = ''
            echo "cgra dev shell - make | make test | make sim | make docs | make check-deps"
            echo "note: bitstream synthesis (make bit / make sta) needs Vivado, not packaged here."
          '';
        };
      });

      # `nix build`            -> the cgra CLI + libcgra + man/info/completion.
      # `nix profile install .` -> installs it like any package manager would,
      # by reusing the project's standard `make install` (KISS: one install path).
      # `nix run . -- <args>`  -> runs the cgra CLI.
      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "cgra";
          inherit version;
          src = ./.;
          nativeBuildInputs = [ pkgs.gcc pkgs.gnumake pkgs.pkg-config pkgs.texinfo ];
          buildInputs = [ pkgs.readline ];
          # The Makefile's install target builds the optimised (-O3) binaries,
          # the info manual, and lays out bin/lib/include/man/info/completion
          # plus a pkg-config file under $out.
          dontConfigure = true;
          dontBuild = true;
          installPhase = ''
            runHook preInstall
            make install PREFIX="$out"
            runHook postInstall
          '';
          meta = with pkgs.lib; {
            description = "Host CLI + library for a UART-attached CGRA accelerator";
            license = licenses.mit;
            platforms = platforms.unix;
            mainProgram = "cgra";
          };
        };

        # `nix build .#doc` -> the IEEE-style hardware report (PDF).
        doc = pkgs.stdenv.mkDerivation {
          pname = "cgra-doc";
          inherit version;
          src = ./.;
          nativeBuildInputs = [ pkgs.gnumake pkgs.texliveMedium ];
          dontConfigure = true;
          buildPhase = "make -C doc -f doc.mk";
          installPhase = "install -Dm644 doc/build/main.pdf $out/share/doc/cgra/cgra.pdf";
          meta.description = "CGRA logical-design report (IEEE LaTeX)";
        };
      });

      # `nix run` the CLI directly.
      apps = forAll (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.default}/bin/cgra";
        };
      });
    };
}
