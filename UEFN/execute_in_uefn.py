# UEFN "Execute Python Script" entry point.
#
# In UEFN: Tools > Execute Python Script... > select this file. It bootstraps the
# package import path and starts the loopback MCP listener. Safe to run repeatedly —
# a prior instance is replaced.
#
# Requires: Project Settings > Python Editor Scripting enabled (early preview).

import os
import sys


def _run():
    here = os.path.dirname(os.path.abspath(__file__))  # .../UEFN
    if here not in sys.path:
        sys.path.insert(0, here)
    from listener.uefn_listener import main
    return main()


_ctx = _run()
