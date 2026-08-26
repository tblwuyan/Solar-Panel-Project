import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import time

class PneumaticController:
    def __init__(self, root):
        self.root = root
        self.root.title("气动+光照控制系统")
        self.root.geometry("780x900")          # 加大窗口容纳光照显示
        self.root.resizable(False, False)

        self.ser = None
        self.connected = False

        # 手动模式
        self.mode_var = tk.StringVar(value="C")

        # 压力相关
        self.target_vars = [tk.DoubleVar(value=0.0) for _ in range(4)]
        self.pressure_vars = [tk.StringVar(value="0.0") for _ in range(4)]
        self.manual_status_vars = [tk.StringVar(value="● 关") for _ in range(4)]

        # 光照相关
        self.light_vars = [tk.StringVar(value="--") for _ in range(4)]

        self.dir_buttons = {}

        self.create_widgets()
        self.refresh_ports()
        self.update_serial()

    def create_widgets(self):
        # ---- 串口设置 ----
        frame_top = ttk.LabelFrame(self.root, text="串口设置", padding=8)
        frame_top.pack(fill="x", padx=12, pady=8)

        ttk.Label(frame_top, text="端口:").grid(row=0, column=0, padx=5, pady=5)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(frame_top, textvariable=self.port_var, width=18)
        self.port_combo.grid(row=0, column=1, padx=5, pady=5)

        self.refresh_btn = ttk.Button(frame_top, text="刷新", command=self.refresh_ports)
        self.refresh_btn.grid(row=0, column=2, padx=5)

        self.connect_btn = ttk.Button(frame_top, text="连接", command=self.toggle_connect)
        self.connect_btn.grid(row=0, column=3, padx=5)

        self.status_label = ttk.Label(frame_top, text="未连接", foreground="red")
        self.status_label.grid(row=0, column=4, padx=15)

        # ---- 手动模式选择 ----
        frame_mode = ttk.LabelFrame(self.root, text="手动模式选择", padding=10)
        frame_mode.pack(fill="x", padx=12, pady=8)

        ttk.Radiobutton(frame_mode, text="充气", variable=self.mode_var,
                        value="C").pack(side="left", padx=25)
        ttk.Radiobutton(frame_mode, text="吸气", variable=self.mode_var,
                        value="I").pack(side="left", padx=25)

        # ---- 方向按钮 (手动控制) ----
        frame_dir = ttk.LabelFrame(self.root, text="手动控制 (点击切换 开/关)", padding=15)
        frame_dir.pack(fill="both", expand=True, padx=12, pady=8)

        btn_config = {
            'F': {'text': '前 ▲', 'row': 0, 'col': 1},
            'R': {'text': '右 ►', 'row': 1, 'col': 2},
            'B': {'text': '后 ▼', 'row': 2, 'col': 1},
            'L': {'text': '左 ◄', 'row': 1, 'col': 0},
        }
        for key, cfg in btn_config.items():
            btn = tk.Button(frame_dir, text=cfg['text'], width=10, height=2,
                            font=("Arial", 14),
                            command=lambda d=key: self.toggle_manual(d))
            btn.grid(row=cfg['row'], column=cfg['col'], padx=15, pady=12, sticky="nsew")
            self.dir_buttons[key] = btn
            btn.config(state="disabled")

        self.close_btn = tk.Button(frame_dir, text="全部关闭", font=("Arial", 14, "bold"),
                                   bg="orange", fg="white",
                                   command=self.send_off)
        self.close_btn.grid(row=3, column=0, columnspan=3, padx=15, pady=12, sticky="ew")
        self.close_btn.config(state="disabled")

        for r in range(4):
            frame_dir.grid_rowconfigure(r, weight=1)
        for c in range(3):
            frame_dir.grid_columnconfigure(c, weight=1)

        # ---- 自动压力控制区域 ----
        frame_auto = ttk.LabelFrame(self.root, text="自动控制 (设定目标值, 0为关闭)", padding=12)
        frame_auto.pack(fill="x", padx=12, pady=8)

        # 表头
        ttk.Label(frame_auto, text="方向", font=("Arial", 10, "bold")).grid(row=0, column=0, padx=5, pady=5)
        ttk.Label(frame_auto, text="目标 (kPa)", font=("Arial", 10, "bold")).grid(row=0, column=1, padx=5, pady=5)
        ttk.Label(frame_auto, text="当前 (kPa)", font=("Arial", 10, "bold")).grid(row=0, column=2, padx=5, pady=5)
        ttk.Label(frame_auto, text="手动状态", font=("Arial", 10, "bold")).grid(row=0, column=3, padx=5, pady=5)

        dir_names = ['前', '右', '后', '左']
        for i in range(4):
            ttk.Label(frame_auto, text=dir_names[i], font=("Arial", 10)).grid(row=i+1, column=0, padx=5, pady=5)
            entry = ttk.Entry(frame_auto, textvariable=self.target_vars[i], width=10)
            entry.grid(row=i+1, column=1, padx=5, pady=5)
            entry.bind('<FocusOut>', lambda e, idx=i: self.send_set_target(idx))
            entry.bind('<Return>', lambda e, idx=i: self.send_set_target(idx))
            ttk.Label(frame_auto, textvariable=self.pressure_vars[i], width=10,
                      font=("Arial", 10), relief="sunken", anchor="center").grid(row=i+1, column=2, padx=5, pady=5)
            ttk.Label(frame_auto, textvariable=self.manual_status_vars[i], width=8,
                      font=("Arial", 10), foreground="blue").grid(row=i+1, column=3, padx=5, pady=5)

        # ---- 光照强度显示区域 ----
        frame_light = ttk.LabelFrame(self.root, text="光照强度 (lx)", padding=12)
        frame_light.pack(fill="x", padx=12, pady=8)

        # 按方向显示四个光强
        light_labels = ['前', '右', '后', '左']
        for i in range(4):
            ttk.Label(frame_light, text=light_labels[i] + ":", font=("Arial", 10, "bold")).grid(row=0, column=i*2, padx=5, pady=5)
            ttk.Label(frame_light, textvariable=self.light_vars[i], width=10,
                      font=("Arial", 10), relief="sunken", anchor="center").grid(row=0, column=i*2+1, padx=5, pady=5)

        # ---- 状态栏 ----
        frame_status = ttk.LabelFrame(self.root, text="状态", padding=6)
        frame_status.pack(fill="x", padx=12, pady=8)
        self.status_text = tk.StringVar(value="就绪")
        ttk.Label(frame_status, textvariable=self.status_text, font=("Arial", 10)).pack(pady=4)

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.set(port_list[0])
        else:
            self.port_combo.set('')

    def toggle_connect(self):
        if self.connected:
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.connected = False
            self.connect_btn.config(text="连接")
            self.status_label.config(text="已断开", foreground="red")
            for btn in self.dir_buttons.values():
                btn.config(state="disabled")
            self.close_btn.config(state="disabled")
            self.status_text.set("未连接")
        else:
            port = self.port_var.get().strip()
            if not port:
                messagebox.showerror("错误", "请选择串口")
                return
            try:
                self.ser = serial.Serial(port, 9600, timeout=0.5)
                time.sleep(2)
                self.connected = True
                self.connect_btn.config(text="断开")
                self.status_label.config(text="已连接", foreground="green")
                for btn in self.dir_buttons.values():
                    btn.config(state="normal")
                self.close_btn.config(state="normal")
                self.send_command("OFF")
                self.status_text.set("已就绪")
            except Exception as e:
                messagebox.showerror("连接失败", str(e))
                self.connected = False
                self.connect_btn.config(text="连接")
                self.status_label.config(text="未连接", foreground="red")

    def send_command(self, cmd):
        try:
            if self.ser and self.ser.is_open:
                self.ser.write((cmd + "\n").encode())
        except Exception as e:
            messagebox.showerror("发送错误", str(e))

    def toggle_manual(self, direction):
        if not self.connected:
            messagebox.showwarning("警告", "请先连接串口")
            return
        idx = {'F':0, 'R':1, 'B':2, 'L':3}[direction]
        if not hasattr(self, 'manual_states'):
            self.manual_states = [False] * 4
        current = self.manual_states[idx]
        if current:
            self.send_command(f"DEACT,{direction}")
            self.manual_states[idx] = False
            self.manual_status_vars[idx].set("● 关")
            self.status_text.set(f"关闭手动: {direction}")
        else:
            mode = self.mode_var.get()
            self.send_command(f"MAN,{direction},{mode}")
            self.manual_states[idx] = True
            mode_str = "充气" if mode == 'C' else "吸气"
            self.manual_status_vars[idx].set("● 开(充)" if mode=='C' else "● 开(吸)")
            self.status_text.set(f"启动手动: {direction} {mode_str}")

    def send_set_target(self, idx):
        if not self.connected:
            return
        target = self.target_vars[idx].get()
        dir_char = ['F','R','B','L'][idx]
        self.send_command(f"SET,{dir_char},{target:.1f}")
        self.status_text.set(f"设定 {dir_char} 目标: {target:.1f} kPa")
        if hasattr(self, 'manual_states'):
            self.manual_states[idx] = False
            self.manual_status_vars[idx].set("● 关")

    def send_off(self):
        if not self.connected:
            return
        self.send_command("OFF")
        self.status_text.set("全部关闭")
        if hasattr(self, 'manual_states'):
            for i in range(4):
                self.manual_states[i] = False
                self.manual_status_vars[i].set("● 关")

    def update_serial(self):
        """定时读取串口数据并更新UI (压力 + 光照)"""
        if self.connected and self.ser and self.ser.is_open:
            try:
                while self.ser.in_waiting:
                    line = self.ser.readline().decode().strip()
                    if line.startswith("P:"):
                        # 压力数据: P:F=12.3,R=15.6,B=10.1,L=8.9
                        parts = line[2:].split(',')
                        for part in parts:
                            if '=' in part:
                                key, val = part.split('=')
                                for i, ch in enumerate(['F','R','B','L']):
                                    if key == ch:
                                        self.pressure_vars[i].set(val)
                                        break
                    elif line.startswith("L:"):
                        # 光照数据: L:F=123,R=456,B=789,L=012
                        parts = line[2:].split(',')
                        for part in parts:
                            if '=' in part:
                                key, val = part.split('=')
                                for i, ch in enumerate(['F','R','B','L']):
                                    if key == ch:
                                        self.light_vars[i].set(val)
                                        break
            except:
                pass
        self.root.after(100, self.update_serial)

    def on_closing(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = PneumaticController(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()