"""Double-click entry point; startup errors remain visible without a console."""
from pathlib import Path
import sys
import traceback

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from cofcoop.ui import main
    main()
except Exception:
    import tkinter as tk
    from tkinter import messagebox
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror("Cry of Fear launcher", traceback.format_exc())
    root.destroy()
