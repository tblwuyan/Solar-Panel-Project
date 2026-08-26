import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
from collections import deque

class PneumaticGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("气动控制系统 + 姿态显示 (带归零)")
        self.root.geometry("900x720")
        self.root.resizable(False, False)

        # 串口相关
        self.ser = None
        self.connected = False
        self.running = False

        # 存储最新数据
        self.pressures = [0.0, 0.0, 0.0, 0.0]
        self.roll = 0.0
        self.pitch = 0.0
        self.yaw = 0.0

        # 平滑滤波
        self.filter_size = 5
        self.pressure_history = [deque(maxlen=self.filter_size) for _ in range(4)]

        # 手动状态
        self.manual_states = [False, False, False, False]
        self.manual_modes = ['C', 'C', 'C', 'C']

        # 归零状态
        self.zero_status = tk.StringVar(value="就绪")

        self.create_widgets()
        self.refresh_ports()

        self.running = True
        self.read_thread = threading.Thread(target=self.serial_reader, daemon=True)
        self.read_thread.start()

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

        # ---- 角度归零按钮 ----
        frame_zero = ttk.Frame(frame_top)
        frame_zero.grid(row=0, column=5, padx=20)
        self.zero_btn = ttk.Button(frame_zero, text="角度归零", command=self.send_zero)
        self.zero_btn.pack(side="left")
        self.zero_btn.config(state="disabled")
        self.zero_status_label = ttk.Label(frame_zero, textvariable=self.zero_status, foreground="blue")
        self.zero_status_label.pack(side="left", padx=10)

        # ---- 四个腔室 ----
        frame_main = ttk.Frame(self.root)
        frame_main.pack(fill="both", expand=True, padx=12, pady=8)

        dir_names = ['前 (F)', '右 (R)', '后 (B)', '左 (L)']
        dir_chars = ['F', 'R', 'B', 'L']
        self.chamber_frames = []

        for i in range(4):
            frame = ttk.LabelFrame(frame_main, text=dir_names[i], padding=10)
            frame.grid(row=i//2, column=i%2, padx=8, pady=8, sticky="nsew")
            self.chamber_frames.append(frame)

            # 当前压力
            ttk.Label(frame, text="当前压力:", font=("Arial", 10)).grid(row=0, column=0, sticky="w")
            pressure_label = ttk.Label(frame, text="0.0 kPa", font=("Arial", 14, "bold"))
            pressure_label.grid(row=0, column=1, padx=10, pady=5, sticky="w")
            frame.pressure_var = tk.StringVar(value="0.0")
            pressure_label.config(textvariable=frame.pressure_var)

            # 平滑值
            ttk.Label(frame, text="平滑值:", font=("Arial", 9)).grid(row=0, column=2, sticky="w", padx=10)
            smooth_label = ttk.Label(frame, text="0.0 kPa", font=("Arial", 10), foreground="blue")
            smooth_label.grid(row=0, column=3, padx=5, pady=5, sticky="w")
            frame.smooth_var = tk.StringVar(value="0.0")
            smooth_label.config(textvariable=frame.smooth_var)

            # 目标设定
            ttk.Label(frame, text="目标 (kPa):", font=("Arial", 10)).grid(row=1, column=0, sticky="w", pady=5)
            target_entry = ttk.Entry(frame, width=10)
            target_entry.grid(row=1, column=1, padx=10, pady=5, sticky="w")
            target_entry.bind('<Return>', lambda e, idx=i: self.send_set_target(idx))
            target_entry.bind('<FocusOut>', lambda e, idx=i: self.send_set_target(idx))
            frame.target_entry = target_entry

            # 手动按钮
            btn_frame = ttk.Frame(frame)
            btn_frame.grid(row=2, column=0, columnspan=4, pady=8)

            btn_charge = ttk.Button(btn_frame, text="充气", width=8,
                                    command=lambda idx=i: self.send_manual(idx, 'C'))
            btn_charge.pack(side="left", padx=5)
            btn_exhaust = ttk.Button(btn_frame, text="吸气", width=8,
                                     command=lambda idx=i: self.send_manual(idx, 'I'))
            btn_exhaust.pack(side="left", padx=5)

            # 手动状态
            manual_status = ttk.Label(frame, text="● 关", foreground="gray")
            manual_status.grid(row=3, column=0, columnspan=4, pady=5)
            frame.manual_status_var = tk.StringVar(value="● 关")
            manual_status.config(textvariable=frame.manual_status_var)

            frame.dir_char = dir_chars[i]

        for r in range(2):
            frame_main.grid_rowconfigure(r, weight=1)
        for c in range(2):
            frame_main.grid_columnconfigure(c, weight=1)

        # ---- 角度显示 ----
        frame_angle = ttk.LabelFrame(self.root, text="JY901 姿态角 (归零后)", padding=10)
        frame_angle.pack(fill="x", padx=12, pady=8)

        self.roll_var = tk.StringVar(value="0.0°")
        self.pitch_var = tk.StringVar(value="0.0°")
        self.yaw_var = tk.StringVar(value="0.0°")

        ttk.Label(frame_angle, text="横滚 (Roll):", font=("Arial", 10)).grid(row=0, column=0, padx=10, pady=5)
        ttk.Label(frame_angle, textvariable=self.roll_var, font=("Arial", 12, "bold"), width=10, relief="sunken").grid(row=0, column=1, padx=5)

        ttk.Label(frame_angle, text="俯仰 (Pitch):", font=("Arial", 10)).grid(row=0, column=2, padx=10, pady=5)
        ttk.Label(frame_angle, textvariable=self.pitch_var, font=("Arial", 12, "bold"), width=10, relief="sunken").grid(row=0, column=3, padx=5)

        ttk.Label(frame_angle, text="偏航 (Yaw):", font=("Arial", 10)).grid(row=0, column=4, padx=10, pady=5)
        ttk.Label(frame_angle, textvariable=self.yaw_var, font=("Arial", 12, "bold"), width=10, relief="sunken").grid(row=0, column=5, padx=5)

        # ---- 底部 ----
        frame_bottom = ttk.Frame(self.root)
        frame_bottom.pack(fill="x", padx=12, pady=10)

        self.off_btn = ttk.Button(frame_bottom, text="全部关闭 (OFF)", command=self.send_off)
        self.off_btn.pack(side="left", padx=10)
        self.off_btn.config(state="disabled")

        self.status_text = tk.StringVar(value="就绪")
        ttk.Label(frame_bottom, textvariable=self.status_text, font=("Arial", 10)).pack(side="left", padx=20)

    # ========== 串口操作 ==========
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
            self.running = False
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.connected = False
            self.connect_btn.config(text="连接")
            self.status_label.config(text="已断开", foreground="red")
            self.off_btn.config(state="disabled")
            self.zero_btn.config(state="disabled")
            self.status_text.set("未连接")
            for i in range(4):
                self.chamber_frames[i].pressure_var.set("0.0")
                self.chamber_frames[i].smooth_var.set("0.0")
            self.roll_var.set("0.0°")
            self.pitch_var.set("0.0°")
            self.yaw_var.set("0.0°")
            self.zero_status.set("就绪")
        else:
            port = self.port_var.get().strip()
            if not port:
                messagebox.showerror("错误", "请选择串口")
                return
            try:
                self.ser = serial.Serial(port, 9600, timeout=0.5)
                time.sleep(2)
                self.connected = True
                self.running = True
                self.connect_btn.config(text="断开")
                self.status_label.config(text="已连接", foreground="green")
                self.off_btn.config(state="normal")
                self.zero_btn.config(state="normal")
                self.status_text.set("已就绪")
                self.send_command("OFF")
                if not self.read_thread.is_alive():
                    self.read_thread = threading.Thread(target=self.serial_reader, daemon=True)
                    self.read_thread.start()
            except Exception as e:
                messagebox.showerror("连接失败", str(e))
                self.connected = False
                self.connect_btn.config(text="连接")
                self.status_label.config(text="未连接", foreground="red")

    def send_command(self, cmd):
        try:
            if self.ser and self.ser.is_open:
                self.ser.write((cmd + "\n").encode())
                print(f"发送: {cmd}")
        except Exception as e:
            messagebox.showerror("发送错误", str(e))

    # ========== 归零命令 ==========
    def send_zero(self):
        if not self.connected:
            messagebox.showwarning("警告", "请先连接串口")
            return
        self.zero_status.set("归零中...")
        self.zero_btn.config(state="disabled")
        self.send_command("ZERO")

    # ========== 串口读取线程 ==========
    def serial_reader(self):
        while self.running:
            if self.connected and self.ser and self.ser.is_open:
                try:
                    if self.ser.in_waiting:
                        line = self.ser.readline().decode().strip()
                        if line:
                            self.parse_line(line)
                except Exception as e:
                    print("读取错误:", e)
            time.sleep(0.01)

    def parse_line(self, line):
        # 忽略空行
        if not line:
            return

        if line.startswith("P:"):
            parts = line[2:].split(',')
            for part in parts:
                if '=' in part:
                    key, val = part.split('=')
                    idx = {'F':0, 'R':1, 'B':2, 'L':3}.get(key)
                    if idx is not None:
                        try:
                            p = float(val)
                            self.pressures[idx] = p
                            self.pressure_history[idx].append(p)
                            avg = sum(self.pressure_history[idx]) / len(self.pressure_history[idx])
                            self.root.after(0, lambda i=idx, v=p, a=avg: self.update_pressure_ui(i, v, a))
                        except:
                            pass
        elif line.startswith("A:"):
            try:
                data = line[2:].split(',')
                for item in data:
                    if '=' in item:
                        k, v = item.split('=')
                        v = float(v)
                        if k == 'roll':
                            self.roll = v
                            self.root.after(0, lambda val=v: self.roll_var.set(f"{val:.1f}°"))
                        elif k == 'pitch':
                            self.pitch = v
                            self.root.after(0, lambda val=v: self.pitch_var.set(f"{val:.1f}°"))
                        elif k == 'yaw':
                            self.yaw = v
                            self.root.after(0, lambda val=v: self.yaw_var.set(f"{val:.1f}°"))
            except:
                pass
        elif line == "System Ready":
            self.root.after(0, lambda: self.status_text.set("系统就绪"))
        elif line == "ZERO_START":
            self.root.after(0, lambda: self.zero_status.set("归零中... (50点)"))
        elif line == "ZERO_DONE":
            self.root.after(0, lambda: self.zero_status.set("归零完成"))
            self.root.after(0, lambda: self.zero_btn.config(state="normal"))

    def update_pressure_ui(self, idx, raw, smooth):
        self.chamber_frames[idx].pressure_var.set(f"{raw:.1f}")
        self.chamber_frames[idx].smooth_var.set(f"{smooth:.1f}")

    # ========== 控制命令 ==========
    def send_set_target(self, idx):
        if not self.connected:
            messagebox.showwarning("警告", "请先连接串口")
            return
        entry = self.chamber_frames[idx].target_entry
        try:
            target = float(entry.get())
        except ValueError:
            messagebox.showwarning("输入错误", "请输入有效数字")
            return
        dir_char = self.chamber_frames[idx].dir_char
        self.send_command(f"SET,{dir_char},{target:.1f}")
        self.status_text.set(f"设定 {dir_char} 目标: {target:.1f} kPa")
        self.manual_states[idx] = False
        self.chamber_frames[idx].manual_status_var.set("● 关")

    def send_manual(self, idx, mode):
        if not self.connected:
            messagebox.showwarning("警告", "请先连接串口")
            return
        dir_char = self.chamber_frames[idx].dir_char
        self.send_command(f"MAN,{dir_char},{mode}")
        self.manual_states[idx] = True
        self.manual_modes[idx] = mode
        status = "● 开(充)" if mode == 'C' else "● 开(吸)"
        self.chamber_frames[idx].manual_status_var.set(status)
        self.status_text.set(f"手动 {dir_char} {'充气' if mode=='C' else '吸气'}")

    def send_off(self):
        if not self.connected:
            return
        self.send_command("OFF")
        self.status_text.set("全部关闭")
        for i in range(4):
            self.manual_states[i] = False
            self.chamber_frames[i].manual_status_var.set("● 关")

    def on_closing(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = PneumaticGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()