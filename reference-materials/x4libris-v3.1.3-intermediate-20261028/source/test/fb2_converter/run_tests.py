"""Run converter regression suites in isolated, disposable SD-card directories."""
from pathlib import Path
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="witchhunt-fb2-") as root:
    for suite in ("verify", "formatting_verify"):
        subprocess.run([sys.executable, str(here / (suite + ".py")),
                        str(Path(sys.argv[1]).resolve()), str(Path(root) / suite)], check=True)
