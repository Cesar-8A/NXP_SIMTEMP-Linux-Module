import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import threading
import queue
import os
import select
import struct
import time
import random
import sys
from pathlib import Path
import matplotlib
matplotlib.use("TkAgg")  # Use Tkinter backend
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg



#try to get thye configutarion on CLI, if not possible, use other as default
try:
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent.parent
    sys.path.append(str(project_root))
    
    #look for cli config
    from user.cli.main import DEVICE_PATH, SYSFS_PATH, STRUCT_FORMAT, STRUCT_SIZE, SIMTEMP_FLAG_THRESHOLD_CROSSED
    print(f"Values imported successfully from {project_root / 'user/cli/main.py'}")

except ImportError as e:
    print(f"WARNING: Could not import from user.cli.main.py (Error: {e}).")
    print("Using default values. Make sure user/cli/main.py exists.")
    DEVICE_PATH = "/dev/simtemp"
    SYSFS_PATH = "/sys/class/simtemp/simtemp"
    STRUCT_FORMAT = 'Q i I' # 8-byte u64, 4-byte s32, 4-byte u32
    STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)
    SIMTEMP_FLAG_THRESHOLD_CROSSED = (1 << 1)

# --- GUI Constants ---
TEMP_MIN_GRAPH = 15  # Graph lower limit
TEMP_MAX_GRAPH = 45  # Graph upper limit
MAX_SAMPLES = 50     # Number of samples in the graph

# --- Helper Functions  ---
def sysfs_write(attr, value):
    """Writes a value to a sysfs attribute."""
    path = os.path.join(SYSFS_PATH, attr)
    try:
        with open(path, 'w') as f:
            f.write(str(value))
    except Exception as e:
        # Reraise for the GUI to handle
        raise PermissionError(f"Error writing to {path}: {e}\n\nDid you run with sudo?")

def sysfs_read(attr):
    """Reads a value from a sysfs attribute."""
    path = os.path.join(SYSFS_PATH, attr)
    try:
        with open(path, 'r') as f:
            return f.read().strip()
    except Exception as e:
        raise IOError(f"Error reading {path}: {e}\n\nIs the module loaded?")

# --- Worker Thread (from final code) ---
class DeviceWorker(threading.Thread):
    """
    This thread runs in the background. Its only job is to
    block on poll() and read from the kernel device.
    """
    def __init__(self, data_queue):
        super().__init__(daemon=True) # daemon=True so it dies if the GUI dies
        self.data_queue = data_queue
        self.running = True
        self.fd = None
        self.poller = None

    def run(self):
        try:
            self.fd = os.open(DEVICE_PATH, os.O_RDONLY | os.O_NONBLOCK)
            self.poller = select.poll()
            self.poller.register(self.fd, select.POLLIN | select.POLLRDNORM | select.POLLPRI)
        except Exception as e:
            # Send the error to the GUI
            self.data_queue.put({"error": str(e)})
            return

        while self.running:
            try:
                # Wait for events with a 1s timeout
                # The timeout allows the loop to check self.running
                events = self.poller.poll(1000) 
                if not events:
                    continue # Timeout, check again

                for fd, event in events:
                    if event & (select.POLLIN | select.POLLRDNORM):
                        # Read the binary struct from the kernel
                        binary_data = os.read(self.fd, STRUCT_SIZE)
                        if len(binary_data) != STRUCT_SIZE:
                            continue # Short read
                        
                        # Decode the 16 bytes
                        timestamp, temp, flags = struct.unpack(STRUCT_FORMAT, binary_data)
                        
                        message = {
                            "temp_c": temp / 1000.0,
                            "alert_triggered": bool(flags & SIMTEMP_FLAG_THRESHOLD_CROSSED),
                            "timestamp_ns": timestamp,
                            "error": None
                        }
                        # Put the message in the thread-safe queue
                        self.data_queue.put(message)

                    elif event & select.POLLPRI:
                        # Notify the GUI of the alert event
                        self.data_queue.put({"alert_event": True})

            except Exception as e:
                self.data_queue.put({"error": str(e)})
                time.sleep(1) # Avoid error spamming
        
        if self.fd:
            os.close(self.fd)

    def stop(self):
        self.running = False

# --- Main Application  ---
class MainApplication(ttk.Frame):
    """
    Main GUI class, now integrated with the Worker Thread
    and your Matplotlib graph.
    """
    def __init__(self, master):
        super().__init__(master, padding="10")
        self.master = master
        self.master.title("NXP simtemp Dashboard (con Gráfico)")
        self.master.geometry("600x650") # Bigger to fit controls

        # Threading Logic (from final code
        self.data_queue = queue.Queue()
        self.samples = [] # Data storage for the graph
        self.current_threshold = 27.0 # Default value, will be loaded from sysfs

        # Create Widgets (Mix of both) 
        self.create_widgets()

        # start Worker Thread (from final code)
        self.worker = DeviceWorker(self.data_queue)
        self.worker.start()

        #Start Queue Polling Loop (from final code)
        self.poll_queue()

        # clean Shutdown (from final code)
        self.master.protocol("WM_DELETE_WINDOW", self.on_closing)

    def create_widgets(self):
        # --- Visualization Frame (Your code) ---
        vis_frame = ttk.LabelFrame(self, text="Live Data", padding="10")
        vis_frame.pack(fill="x", expand=False, pady=5) # Don't expand

        # Temperature Label (Your code)
        self.temp_var = tk.StringVar(value="--.- °C")
        temp_label = ttk.Label(vis_frame, textvariable=self.temp_var, font=("Helvetica", 24, "bold"), anchor="center")
        temp_label.pack(pady=5, fill="x")

        # Alert Indicator (Your code)
        self.alert_canvas = tk.Canvas(vis_frame, width=100, height=50)
        self.alert_canvas.pack()
        self.alert_indicator = self.alert_canvas.create_oval(10, 10, 60, 50, fill="grey")
        self.alert_canvas.create_text(80, 20, text="Alert", anchor="w")

        # --- Matplotlib Graph (Your code, with improvements) ---
        graph_frame = ttk.Frame(self) # Frame for the graph
        graph_frame.pack(fill="both", expand=True, pady=10) # Expand

        self.fig = Figure(figsize=(5,3), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title("Last 50 Temperature Samples")
        self.ax.set_xlabel("Sample")
        self.ax.set_ylabel("Temperature (°C)")
        self.ax.set_ylim(TEMP_MIN_GRAPH, TEMP_MAX_GRAPH) # Use fixed limits
        self.ax.grid(True, linestyle='--', alpha=0.5)
        
        # Data line
        self.line, = self.ax.plot([], [], 'b-o', lw=2, markersize=4) 
        
        # Threshold Line (now a class attribute)
        self.threshold_line = self.ax.axhline(
            self.current_threshold, color='red', linestyle='--', lw=2, label='Threshold'
        )
        self.ax.legend(loc="upper left")

        self.canvas = FigureCanvasTkAgg(self.fig, master=graph_frame)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)

        # --- Configuration Frame (from final code) ---
        config_frame = ttk.LabelFrame(self, text="Controls (sudo required)", padding="10")
        config_frame.pack(fill="x", expand=False, pady=5) # Don't expand
        config_frame.columnconfigure(1, weight=1) # The Entry expands

        ttk.Label(config_frame, text="Sampling (ms):").grid(row=0, column=0, sticky="w", pady=5)
        self.sampling_var = tk.StringVar()
        ttk.Entry(config_frame, textvariable=self.sampling_var).grid(row=0, column=1, sticky="ew", padx=5)

        ttk.Label(config_frame, text="Threshold (mC):").grid(row=1, column=0, sticky="w", pady=5)
        self.thresh_var = tk.StringVar()
        ttk.Entry(config_frame, textvariable=self.thresh_var).grid(row=1, column=1, sticky="ew", padx=5)

        apply_button = ttk.Button(config_frame, text="Apply Config", command=self.apply_config)
        apply_button.grid(row=2, column=0, columnspan=2, pady=10)

        # --- Status Bar (from final code) ---
        self.status_var = tk.StringVar(value="Connecting to driver...")
        status_label = ttk.Label(self, textvariable=self.status_var, relief=tk.SUNKEN, anchor="w")
        status_label.pack(side="bottom", fill="x", pady=(10,0), ipady=5)

        # Load current values from sysfs
        self.load_initial_config()

    def load_initial_config(self):
        """Loads the current config from sysfs on startup."""
        try:
            sampling = sysfs_read("sampling_ms")
            threshold_mc = sysfs_read("threshold_mC")
            
            self.sampling_var.set(sampling)
            self.thresh_var.set(threshold_mc)
            
            # Update the graph's threshold line
            self.current_threshold = int(threshold_mc) / 1000.0
            self.threshold_line.set_ydata([self.current_threshold])
            self.canvas.draw()
            
            self.status_var.set("Connected. Reading data...")
        except Exception as e:
            self.status_var.set(f"Error reading config: {e}")
            messagebox.showerror("Startup Error", str(e))

    def apply_config(self):
        """Called by the 'Apply Config' button."""
        try:
            ms = self.sampling_var.get()
            mc_str = self.thresh_var.get()
            
            # Validate before writing
            if not (ms.isdigit() and int(ms) > 0):
                raise ValueError("Sampling (ms) must be a number > 0")
            if not mc_str.isdigit(): # Assuming positive
                raise ValueError("Threshold (mC) must be a number")

            # Write to sysfs
            sysfs_write("sampling_ms", ms)
            sysfs_write("threshold_mC", mc_str)
            
            # Update the graph with the new threshold
            self.current_threshold = int(mc_str) / 1000.0
            self.threshold_line.set_ydata([self.current_threshold])
            self.canvas.draw() # Redraw
            
            self.status_var.set(f"Config updated: {ms}ms, {mc_str}mC")

        except Exception as e:
            self.status_var.set("Error applying config.")
            messagebox.showerror("Sysfs Error", str(e))
    
    def poll_queue(self):
        """
        Checks the queue for messages from the worker thread.
        This method NEVER blocks.
        """
        try:
            while True:
                # Process all messages in the queue (non-blocking)
                message = self.data_queue.get(block=False)

                if message.get("error"):
                    self.status_var.set(f"Worker Error: {message['error']}")
                    self.temp_var.set("ERROR")
                    self.alert_canvas.itemconfig(self.alert_indicator, fill="orange")
                    continue
                
                if "temp_c" in message:
                    # Real data received
                    temp_c = message['temp_c']
                    alert_flag = message.get('alert_triggered', False)
                    # Call the function that updates the GUI
                    self.update_gui_with_data(temp_c, alert_flag)
                
                if message.get("alert_event"):
                    # POLLPRI event
                    self.trigger_alert_indicator()
            
        except queue.Empty:
            # The queue is empty, this is normal.
            pass
        
        # Call this function again after 100ms
        self.master.after(100, self.poll_queue)

    def update_gui_with_data(self, temp_c, alert_flag):
 
        # Update temperature label (with more precision)
        self.temp_var.set(f"{temp_c:.3f} °C")

        # Update alert indicator
        # (use the flag from the data, more reliable than POLLPRI)
        if alert_flag:
            self.alert_canvas.itemconfig(self.alert_indicator, fill="red")
        else:
            self.alert_canvas.itemconfig(self.alert_indicator, fill="grey")

        # Update graph data
        self.samples.append(temp_c)
        if len(self.samples) > MAX_SAMPLES:
            self.samples.pop(0)

        # Redraw the graph
        self.line.set_data(range(len(self.samples)), self.samples)
        
        # Dynamically adjust Y-axis 
        min_y = min(min(self.samples or [0]), self.current_threshold) - 5
        max_y = max(max(self.samples or [0]), self.current_threshold) + 5
        self.ax.set_ylim(min_y, max_y)
        self.ax.set_xlim(0, MAX_SAMPLES-1) # Keep the X-axis fixed
        self.canvas.draw()
        
        # 5. Update status bar
        self.status_var.set(f"Last read: {temp_c:.3f}°C")


    def trigger_alert_indicator(self):
        """Sets the indicator to red momentarily."""
        self.alert_canvas.itemconfig(self.alert_indicator, fill="red")
        self.master.after(2000, lambda: self.alert_canvas.itemconfig(self.alert_indicator, fill="grey"))

    def on_closing(self):
        """Called when the window is closed."""
        print("Closing: Stopping worker thread...")
        self.worker.stop()
        self.master.destroy()

# --- Main Execution Block ---
if __name__ == "__main__":
    
    if os.geteuid() != 0:
        print("Warning: Script is not running as root (sudo).")
        print("Data reading may work, but 'Apply Config' will fail.")

    root = tk.Tk()
    
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        print("Theme 'clam' not available, using default.")

    app = MainApplication(master=root)
    app.pack(fill="both", expand=True)
    
    # Start the GUI loop
    root.mainloop()