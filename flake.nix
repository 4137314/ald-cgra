{
  description = "CGRA on FPGA: GHDL simulation, host C library + CLI, LaTeX docs";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      # Only this platform has executed PTY, GNU linker and formal gates.
      systems = [ "x86_64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      version = "0.1.0";
      reportTex = pkgs: pkgs.texlive.combine {
        inherit (pkgs.texlive) scheme-medium pgfplots;
      };
      formalTools = pkgs: [ pkgs.ghdl
        (pkgs.yosys.withPlugins [ pkgs.yosys.allPlugins.ghdl ])
        pkgs.sby pkgs.yices pkgs.z3 pkgs.python3 pkgs.gnumake pkgs.git ];
    in
    {
      # `nix develop` - full toolchain to simulate, build/test the library+CLI,
      # and build the docs. This is the *primary*, standard-Unix workflow;
      # `nix build`/`nix profile install` below are the optional Nix-native path.
      devShells = forAll (pkgs: {
        formal = pkgs.mkShell {
          packages = formalTools pkgs;
        };
        default = pkgs.mkShell {
          packages = with pkgs; [
            # HDL simulation
            ghdl gtkwave tcl         # includes offline timing script tests
            # FPGA programming without Vivado
            openfpgaloader
            # host software
            gcc gnumake pkg-config python3  # JSON/PTY regression tests
            readline                # `cgra shell` line editing
            clang-tools             # clangd (make compdb -> compile_commands.json)
            bear                    # alt. compile_commands.json generator
            # profiling / bug-hunting
            valgrind                # make valgrind / callgrind
            linuxPackages.perf      # make perf
            binutils                # gprof (make gprof)
            # documentation
            texinfo groff           # man pages + GNU info manual
            (reportTex pkgs)         # LaTeX report, including the pgfplots chart
          ];
          shellHook = ''
            echo "cgra dev shell - make | make test | make sim | make docs | make check-deps"
            echo "note: bitstream synthesis (make bit / make sta) needs Vivado, not packaged here."
          '';
        };
      });

      checks = forAll (pkgs: {
        software = pkgs.runCommand "cgra-software-check" {
          nativeBuildInputs = [ pkgs.gcc pkgs.gnumake pkgs.python3 pkgs.pkg-config
            pkgs.texinfo pkgs.readline pkgs.bash ];
        } ''
          cp -r ${./.} source
          chmod -R u+w source
          cd source
          mkdir -p $out
          trap 'cat $out/*.txt' ERR
          make -j4 test > $out/software.txt 2>&1
          make test-install test-compdb test-build > $out/packaging.txt 2>&1
          make -C lib -f lib.mk analyze > $out/analyzer-lib.txt 2>&1
          make -C sw -f sw.mk analyze > $out/analyzer-cli.txt 2>&1
          make -C sw -f sw.mk HAVE_READLINE=0 test > $out/no-readline.txt 2>&1
        '';
        hdl = pkgs.runCommand "cgra-hdl-check" {
          nativeBuildInputs = [ pkgs.ghdl pkgs.gcc pkgs.gnumake pkgs.python3 pkgs.tcl ];
        } ''
          cp -r ${./.} source
          chmod -R u+w source
          cd source
          mkdir -p $out
          trap 'cat $out/*.txt' ERR
          make sim > $out/ghdl.txt 2>&1
          make -C hw -f hw.mk test-scripts > $out/tcl.txt 2>&1
        '';
        documents = self.packages.${pkgs.stdenv.hostPlatform.system}.doc;
        formal-fast = pkgs.runCommand "cgra-formal-fast" {
          nativeBuildInputs = formalTools pkgs;
          CGRA_SOURCE_REVISION = self.rev or (self.dirtyRev or "unknown");
          CGRA_SOURCE_DIRTY = if self ? rev then "false" else "true";
        } ''
          cp -r ${./.} source
          chmod -R u+w source
          cd source
          make formal-fast
          mkdir -p $out
          cp -r hw/build/formal/. $out/
        '';
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
          nativeBuildInputs = [ pkgs.gcc pkgs.gnumake pkgs.pkg-config pkgs.texinfo pkgs.python3 ];
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
            # No repository license has been chosen by the author yet.
            # Packaging metadata must not invent an MIT grant.
            platforms = systems;
            mainProgram = "cgra";
          };
        };

        # `nix build .#doc` -> the IEEE-style hardware report (PDF).
        doc = pkgs.stdenv.mkDerivation {
          pname = "cgra-doc";
          inherit version;
          src = ./.;
          nativeBuildInputs = [ pkgs.gnumake pkgs.python3 (reportTex pkgs) ];
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
