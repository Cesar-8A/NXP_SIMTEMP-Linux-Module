import tkinter as tk
from tkinter import ttk
import random
import matplotlib
matplotlib.use("TkAgg")  # Use Tkinter backend
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import matplotlib.pyplot as plt

# constants 
SIMTEMP_THRESHOLD = 30.0  # °C
TEMP_MIN = 20
TEMP_MAX = 40
MAX_SAMPLES = 50

class SimpleGraphGUI(ttk.Frame):
    def __init__(self, master):
        super().__init__(master, padding="10")
        self.master = master
        self.master.title("NXP simtemp - DEV GUI with Graph")
        self.master.geometry("600x500")

        # --- Temperature label ---
        self.temp_var = tk.StringVar(value="--.- °C")
        temp_label = ttk.Label(self, textvariable=self.temp_var, font=("Helvetica", 24, "bold"))
        temp_label.pack(pady=5)

        # --- Alert indicator --
        self.alert_canvas = tk.Canvas(self, width=100, height=50)
        self.alert_canvas.pack()
        self.alert_indicator = self.alert_canvas.create_oval(10, 10, 60, 50, fill="grey")
        self.alert_canvas.create_text(80, 20, text="Alert", anchor="w")

        # --- Matplotlib Figure ---
        self.fig = Figure(figsize=(5,3), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title("Last 50 Temperature Samples")
        self.ax.set_xlabel("Sample")
        self.ax.set_ylabel("Temperature (°C)")
        self.ax.set_xlim(0, MAX_SAMPLES-1)
        self.ax.set_ylim(TEMP_MIN, TEMP_MAX)
        self.ax.grid(True, linestyle='--', alpha=0.5)
        self.line, = self.ax.plot([], [], 'b-o', lw=2, markersize=4)

        # --- Threshold line 
        self.ax.axhline(SIMTEMP_THRESHOLD, color='red', linestyle='--', lw=2, label='Threshold')
        self.ax.legend()

        self.canvas = FigureCanvasTkAgg(self.fig, master=self)
        self.canvas.get_tk_widget().pack(fill="both", expand=True, pady=10)

        # provisional data storage for simulated samples
        self.samples = []

        # Start automatic update
        self.after(1000, self.update_temp)

    def update_temp(self):
        """Simulate reading temperature, update label, alert, and graph."""
        # Random temperature for demo
        temp = random.uniform(TEMP_MIN, TEMP_MAX)
        self.temp_var.set(f"{temp:.1f} °C")

        # Simple threshold alert
        if temp > SIMTEMP_THRESHOLD:
            self.alert_canvas.itemconfig(self.alert_indicator, fill="red")
        else:
            self.alert_canvas.itemconfig(self.alert_indicator, fill="grey")

        # Update samples
        self.samples.append(temp)
        if len(self.samples) > MAX_SAMPLES:
            self.samples.pop(0)

        # Update graph
        self.line.set_data(range(len(self.samples)), self.samples)
        self.ax.set_xlim(0, MAX_SAMPLES-1)
        self.canvas.draw()

        # Schedule next update
        self.after(1000, self.update_temp)

if __name__ == "__main__":
    root = tk.Tk()
    app = SimpleGraphGUI(master=root)
    app.pack(fill="both", expand=True)
    root.mainloop()
