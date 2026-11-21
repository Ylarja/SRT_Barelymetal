import tkinter as tk
from tkinter import messagebox
from tkinter import filedialog, font
from pathlib import Path
import re

BLUE = "#2b6cff"
PURPLE = "#9b4dca"
GREEN = "#2ea043"
YELLOW = "#d4d4d4"  # placeholder, will be set per theme
DARK_YELLOW  = "#e8de26"   # soft editor gold (dark)
LIGHT_YELLOW = "#bbad46"   # readable yellow on light


LIGHT_THEME = {
    "bg": "#f6f8fa",
    "fg": "#24292f",
    "cursor": "#24292f",
    "line": "#eaeef2"
}

DARK_THEME = {
    "bg": "#1e1e1e",
    "fg": "#d4d4d4",
    "cursor": "#d4d4d4",
    "line": "#2a2a2a"
}


class SyntaxEditor(tk.Tk):
    def __init__(self):
        super().__init__()

        # ---------------- ICON ----------------
        script_dir = Path(__file__).parent.resolve()
        icon_path = script_dir / "Img/ODL_Editor_Icon.ico" 
        self.iconbitmap(icon_path)

        self.current_file = None
        self.theme = DARK_THEME

        self.title("ODL Editor")
        self.geometry("900x600")

        # Autosave state
        self.autosave_after_id = None   # holds the after() callback id
        self.autosave_delay = 1000      # milliseconds to wait after edit before saving

        self.create_top_bar()
        self.create_status_bar()
        self.create_text_area()
        self.bind("<Control-s>", lambda e: self.save_file())

    # ---------------- STATUS BAR ----------------
    def create_status_bar(self):
        self.status_bar = tk.Label(self, text="", anchor="w", font=("Consolas", 10), bg="#1e1e1e", fg="#d4d4d4", padx=10)
        self.status_bar.pack(fill="x", side="bottom")

    # ---------------- TOP BAR ----------------
    def create_top_bar(self):
        top_bar = tk.Frame(self, bg="#1e1e1e", height=40)
        top_bar.pack(fill="x", side="top")

        # Bottom border of top_bar
        bottom_border = tk.Frame(top_bar, height=2, bg="#2b6cff")  # 2px thick bottom border
        bottom_border.pack(side="bottom", fill="x")

        open_btn = tk.Button(top_bar, text="📂 Open", font=("Segoe UI Emoji", 10), command=self.open_file, bg="#3c3c3c", fg="#d4d4d4", activebackground="#555555", activeforeground="#ffffff")
        open_btn.pack(side="left", padx=(5,5), pady=5)

        save_btn = tk.Button(top_bar, text="💾 Save", font=("Segoe UI Emoji", 10), command=self.save_file, bg="#3c3c3c", fg="#d4d4d4", activebackground="#555555", activeforeground="#ffffff")
        save_btn.pack(side="left", pady=5)

        save_as_btn = tk.Button(top_bar, text="💾 Save As", font=("Segoe UI Emoji", 10), command=self.save_file_as, bg="#3c3c3c", fg="#d4d4d4", activebackground="#555555", activeforeground="#ffffff")
        save_as_btn.pack(side="left", padx=(5, 0), pady=5)

        theme_btn = tk.Button(top_bar, text="🌞 Light" if self.theme == DARK_THEME else "🌜 Dark", font=("Segoe UI Emoji", 10), command=self.toggle_theme,  bg="#3c3c3c", fg="#d4d4d4", activebackground="#555555", activeforeground="#ffffff")
        theme_btn.pack(side="left", padx=5)
        self.theme_btn = theme_btn  # store reference for later updates

        generate_btn = tk.Button(top_bar, text="⚙️ Generate", font=("Segoe UI Emoji", 10), command=self.generate, bg="#3c3c3c", fg="#d4d4d4", activebackground="#555555", activeforeground="#ffffff")
        generate_btn.pack(side="right", padx=5, pady=5)

    def toggle_theme(self):
        self.theme = DARK_THEME if self.theme == LIGHT_THEME else LIGHT_THEME
        self.apply_theme()
        self.redraw_line_numbers()
        self.theme_btn.config(text="🌞 Light" if self.theme == DARK_THEME else "🌜 Dark")

    # ---------------- TEXT + CANVAS ----------------
    def create_text_area(self):
        container = tk.Frame(self)
        container.pack(fill="both", expand=True)

        # Canvas gutter
        self.line_canvas = tk.Canvas(
            container,
            width=40,
            bg=self.theme["bg"],
            borderwidth=0,
            highlightthickness=0
        )
        self.line_canvas.pack(side="left", fill="y")

        # Text editor
        self.text = tk.Text(
            container,
            font=("Consolas", 12),
            undo=True,
            wrap="none",
            borderwidth=0,
            highlightthickness=0,
            padx=0,
            pady=0
        )
        self.text.pack(side="left", fill="both", expand=True)
        # Override Tab key to insert 4 spaces
        self.text.bind("<Tab>", self.insert_tab_spaces)
        self.text.bind("<Shift-Tab>", self.remove_tab_spaces)

        # Scrollbar
        scrollbar = tk.Scrollbar(container, command=self.on_scroll)
        scrollbar.pack(side="right", fill="y")
        self.text.config(yscrollcommand=scrollbar.set)

        # Syntax tags
        self.text.tag_configure("blue", foreground=BLUE)
        self.text.tag_configure("purple", foreground=PURPLE)
        self.text.tag_configure("green", foreground=GREEN)
        self.text.tag_configure("yellow", foreground=YELLOW) 
        self.text.tag_configure("current_line")

        # Font metrics for line height
        self.font_metrics = font.Font(font=self.text["font"]).metrics()
        self.line_height = self.font_metrics["linespace"]

        # Bindings
        self.text.bind("<<Modified>>", self.on_text_modified)
        self.text.edit_modified(False)
        self.text.bind("<KeyRelease>", self.on_cursor_move)
        self.text.bind("<KeyRelease>", self.on_text_edit)
        self.text.bind("<ButtonRelease-1>", self.on_cursor_move)
        self.text.bind("<MouseWheel>", self.on_mouse_wheel)
        self.text.bind("<Configure>", lambda e: self.redraw_line_numbers())

        # Initial draw
        self.apply_theme()
        self.redraw_line_numbers()


    # ---------------- INSERT TAB SPACES ----------------
    def insert_tab_spaces(self, event):
        self.text.insert("insert", "    ")  # 4 spaces
        return "break"  # prevent default tab behavior
    
    def remove_tab_spaces(self, event):
        index = self.text.index("insert linestart")
        line_text = self.text.get(index, f"{index} lineend")
        if line_text.startswith("    "):
            self.text.delete(index, f"{index}+4c")
        return "break"


    # ---------------- SCROLLING ----------------
    def on_scroll(self, *args):
        self.text.yview(*args)
        self.redraw_line_numbers()

    def on_mouse_wheel(self, event):
        self.text.yview_scroll(int(-1*(event.delta/120)), "units")
        self.redraw_line_numbers()
        return "break"

    # ---------------- LINE NUMBERS ----------------
    def redraw_line_numbers(self):
        self.line_canvas.delete("all")

        first_index = self.text.index("@0,0")
        last_index = self.text.index(f"@0,{self.text.winfo_height()}")

        first_line = int(first_index.split(".")[0])
        last_line = int(last_index.split(".")[0]) + 1

        canvas_width = int(self.line_canvas["width"])
        text_pad = int(self.text.cget("padx"))
        number_padding = 10
        x_pos = canvas_width - text_pad - number_padding

        for i in range(first_line, last_line):
            dline = self.text.dlineinfo(f"{i}.0")
            if dline:
                y, height = dline[1], dline[3]

                # Highlight current line number
                if str(i) == self.text.index("insert").split(".")[0]:
                    self.line_canvas.create_rectangle(
                        0, y,
                        canvas_width, y + height,
                        fill=self.theme["line"],
                        outline=""
                    )

                # Draw number right-aligned, vertically centered
                self.line_canvas.create_text(
                    x_pos, y + height / 2,
                    text=str(i),
                    anchor="e",
                    font=self.text["font"],
                    fill="#888888"
                )

    def on_text_modified(self, event=None):
        # Redraw line numbers
        self.redraw_line_numbers()

        # Re-run syntax highlighting if needed
        self.highlight()

        # Reset modified flag
        self.text.edit_modified(False)

    # ---------------- CURSOR & LINE HIGHLIGHT ----------------
    def on_cursor_move(self, event=None):
        # Remove old current_line highlight
        self.text.tag_remove("current_line", "1.0", "end")

        # Only highlight current line if there is no selection
        try:
            if not self.text.tag_ranges("sel"):
                line = self.text.index("insert").split(".")[0]
                start = f"{line}.0"
                end = f"{line}.0 lineend +1c"
                self.text.tag_add("current_line", start, end)
        except tk.TclError:
            pass

        self.redraw_line_numbers()
        self.highlight()

    # ---------------- TEXT EDIT ----------------
    def on_text_edit(self, event=None):

        # Cancel previous scheduled autosave
        if self.autosave_after_id:
            self.after_cancel(self.autosave_after_id)

        # Schedule new autosave after delay
        self.autosave_after_id = self.after(self.autosave_delay, self.autosave)

    # ---------------- AUTOSAVE ----------------
    def autosave(self):
        # Only save if a file is already opened
        if self.current_file:
            try:
                with open(self.current_file, "w") as f:
                    f.write(self.text.get("1.0", "end-1c"))
                
                # Update status bar
                import time
                self.status_bar.config(text=f"Autosaved at {time.strftime('%H:%M:%S')}")
                
            except Exception as e:
                self.show_error(e)

        # Reset autosave callback id
        self.autosave_after_id = None

    # ---------------- FILE OPERATIONS ----------------
    def open_file(self):
        path = filedialog.askopenfilename(filetypes=[("Object Description Language", "*.odl")])
        if not path:
            return

        with open(path, "r") as f:
            content = f.read()
        self.current_file = path
        self.title(f"ODL Editor — {path}")
        self.text.delete("1.0", "end")
        self.text.insert("1.0", content)
        self.highlight()
        self.redraw_line_numbers()

    def save_file(self):
        if not self.current_file:
            self.save_file_as()
            return
        with open(self.current_file, "w") as f:
            f.write(self.text.get("1.0", "end-1c"))

    def save_file_as(self):
        path = filedialog.asksaveasfilename(defaultextension=".odl", filetypes=[("Object Description Language", "*.odl")])
        if not path:
            return
        self.current_file = path
        self.title(f"ODL Editor — {path}")
        self.save_file()

    # ---------------- GENERATE ----------------
    def generate(self):
        import Compilers.ODL_C_Compiler as oc
        try:
            result = oc.Compile_ODL_Schema(
                schema_path=Path(self.current_file) if self.current_file else None
            )

            # Update editor path after migration
            if result and result.get("schema_path"):
                self.current_file = str(result["schema_path"])
                self.title(f"ODL Editor — {self.current_file}")

            messagebox.showinfo(
                "Generation Successful",
                "Code was generated successfully.\n"
                "Schema file was migrated to Core/DOL/Schemas."
            )

        except Exception as e:
            self.show_error(e)


    # ---------------- ERROR MESSAGEBOX ----------------
    def show_error(self, exc: Exception):
        messagebox.showerror(
            "Generation Error",
            str(exc)
        )

    # ---------------- SYNTAX HIGHLIGHT ----------------
    def highlight(self, event=None):
        self.text.tag_remove("blue", "1.0", "end")
        self.text.tag_remove("purple", "1.0", "end")
        self.text.tag_remove("green", "1.0", "end")
        self.text.tag_remove("yellow", "1.0", "end")

        lines = self.text.get("1.0", "end-1c").splitlines()
        for i, line in enumerate(lines, start=1):
            # COMMENTS
            if "#" in line:
                comment_start = line.index("#")
                self.text.tag_add("green", f"{i}.{comment_start}", f"{i}.end")
                code_part = line[:comment_start]
            else:
                code_part = line

            # NO INDENT IDENTIFIER
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]*\s*:", code_part):
                name_end = code_part.index(":")
                self.text.tag_add("blue", f"{i}.0", f"{i}.{name_end}")

            # CONTAINER FIELDS (ONE INDENT + NO TYPE)
            for match in re.finditer(r"^( {4}\w+)\s*:\s*(\d*)\s*$", code_part):
                field_name = match.group(1)
                s = match.start(1)
                e = match.end(1)
                self.text.tag_add("yellow", f"{i}.{s}", f"{i}.{e}")

            # AFTER ':'
            for match in re.finditer(r":\s*([^\s,]+)", code_part):
                s, e = match.start(1), match.end(1)
                self.text.tag_add("blue", f"{i}.{s}", f"{i}.{e}")

            # AFTER ','
            for match in re.finditer(r",\s*([^\s,]+)", code_part):
                s, e = match.start(1), match.end(1)
                self.text.tag_add("purple", f"{i}.{s}", f"{i}.{e}")

            # ONE INDENT + NUMBER
            if re.match(r"\s{4}\w+\s*:\s*\d+", code_part):
                match = re.search(r":\s*(\d+)", code_part)
                if match:
                    s, e = match.start(1), match.end(1)
                    self.text.tag_add("purple", f"{i}.{s}", f"{i}.{e}")

    # ---------------- THEME ----------------
    def apply_theme(self):
        self.text.config(
            bg=self.theme["bg"],
            fg=self.theme["fg"],
            insertbackground=self.theme["cursor"],
            selectbackground="#264f78",
            selectforeground="#ffffff"
        )
        self.status_bar.config(bg=self.theme["bg"], fg=self.theme["fg"])
        YELLOW = DARK_YELLOW if self.theme == DARK_THEME else LIGHT_YELLOW
        self.text.tag_configure("yellow", foreground=YELLOW)
        self.text.tag_configure("current_line", background=self.theme["line"])
        self.line_canvas.config(bg=self.theme["bg"])


if __name__ == "__main__":

    app = SyntaxEditor()
    app.mainloop()
