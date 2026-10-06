from pathlib import Path
import shutil,subprocess,tempfile
with tempfile.TemporaryDirectory(prefix="cgra-fragment-repro-") as temp:
    root=Path(temp)
    for part in ("lib","scripts"):
        shutil.copytree(Path("/src")/part,root/part,ignore=shutil.ignore_patterns("build","__pycache__"))
    p=root/"lib/test/test_transport.c"
    s=p.read_text()
    s=s.replace("read(master, wire, sizeof(wire)) == 6", "observed == 6")
    s=s.replace("    fragment_writes = 0;\n    CHECK", "    fragment_writes = 0;\n    ssize_t observed = read(master, wire, sizeof(wire));\n    fprintf(stderr, \"fragmented PTY read=%zd (expected 6), rc=%d, EINTR_pending=%d, EAGAIN_pending=%d\\n\", observed, rc, inject_interrupt, inject_again);\n    CHECK")
    s=s[:s.index("int main(int argc, char **argv)")]+"int main(void) { setvbuf(stdout,NULL,_IOLBF,0); alarm(20); for(int i=0;i<100;i++) test_partial_write_and_retry_budget(); return 0;}\n"
    p.write_text(s)
    subprocess.run(["make","-C",str(root/"lib"),"-f","lib.mk","CC=gcc-13","build/test_transport"],check=True)
    subprocess.run([str(root/"lib/build/test_transport")],check=True)
