import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import re

class MainApp:
    def __init__(self, root):
        self.root = root
        self.root.title("气动控制系统 - 仪表板")

        # 串口对象
        self.ser = None
        self.running = False
        self.lock = threading.Lock()

        # 数据显示变量
        self.pressure = [0.0, 0.0, 0.0, 0.0]
        self.lux = [0, 0, 0, 0]
        self.roll = 0.0
        self.pitch = 0.0
        self.yaw = 0.0

        # 创建UI
        self.create_widgets()

        # 自动刷新
        self.update_display()

    def create_widgets(self):
        # 顶部：串口连接
        top_frame = tk.Frame(self.root)
        top_frame.pack(pady=5)

        tk.Label(top_frame, text="串口:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo = ttk.Combobox(top_frame, textvariable=self.port_var, values=ports, width=12)
        self.port_combo.pack(side=tk.LEFT, padx=5)
        if ports:
            self.port_combo.current(0)

        tk.Label(top_frame, text="波特率:").pack(side=tk.LEFT, padx=(10,0))
        self.baud_var = tk.StringVar(value="9600")
        baud_combo = ttk.Combobox(top_frame, textvariable=self.baud_var, values=["9600", "115200"], width=8)
        baud_combo.pack(side=tk.LEFT, padx=5)

        self.connect_btn = tk.Button(top_frame, text="连接", command=self.toggle_connect)
        self.connect_btn.pack(side=tk.LEFT, padx=10)

        # 显示区域：四个驱动器气压、四个光强、角度
        display_frame = tk.Frame(self.root)
        display_frame.pack(pady=10, padx=10)

        # 气压
        pressure_frame = tk.LabelFrame(display_frame, text="驱动器气压 (kPa)", font=("Arial", 10, "bold"))
        pressure_frame.grid(row=0, column=0, padx=10, pady=5, sticky="nsew")

        self.pressure_bars = []
        self.pressure_labels = []
        for i, name in enumerate(["F", "R", "B", "L"]):
            sub = tk.Frame(pressure_frame)
            sub.pack(fill=tk.X, pady=2)
            tk.Label(sub, text=name+":", width=3).pack(side=tk.LEFT)
            bar = ttk.Progressbar(sub, length=150, mode='determinate', maximum=300, value=0)
            bar.pack(side=tk.LEFT, padx=5)
            label = tk.Label(sub, text="0.0", width=8)
            label.pack(side=tk.LEFT)
            self.pressure_bars.append(bar)
            self.pressure_labels.append(label)

        # 光强
        lux_frame = tk.LabelFrame(display_frame, text="光强 (lx)", font=("Arial", 10, "bold"))
        lux_frame.grid(row=0, column=1, padx=10, pady=5, sticky="nsew")

        self.lux_labels = []
        for i in range(4):
            sub = tk.Frame(lux_frame)
            sub.pack(fill=tk.X, pady=2)
            tk.Label(sub, text=f"光强{i}:", width=7).pack(side=tk.LEFT)
            label = tk.Label(sub, text="0", width=8)
            label.pack(side=tk.LEFT)
            self.lux_labels.append(label)

        # 角度
        angle_frame = tk.LabelFrame(display_frame, text="姿态角度 (°)", font=("Arial", 10, "bold"))
        angle_frame.grid(row=0, column=2, padx=10, pady=5, sticky="nsew")

        self.angle_labels = {}
        for name in ["横滚", "俯仰", "偏航"]:
            sub = tk.Frame(angle_frame)
            sub.pack(fill=tk.X, pady=2)
            tk.Label(sub, text=name+":", width=6).pack(side=tk.LEFT)
            label = tk.Label(sub, text="0.0", width=10)
            label.pack(side=tk.LEFT)
            self.angle_labels[name] = label

        # 控制按钮
        ctrl_frame = tk.Frame(self.root)
        ctrl_frame.pack(pady=10)

        self.release_btn = tk.Button(ctrl_frame, text="一键放气 (5秒)", command=self.emergency_release, bg="orange", fg="white", font=("Arial", 10, "bold"))
        self.release_btn.pack(side=tk.LEFT, padx=10)

        self.stop_btn = tk.Button(ctrl_frame, text="紧急停止", command=self.emergency_stop, bg="red", fg="white", font=("Arial", 10, "bold"))
        self.stop_btn.pack(side=tk.LEFT, padx=10)

        self.zero_btn = tk.Button(ctrl_frame, text="角度归零", command=self.zero_angle)
        self.zero_btn.pack(side=tk.LEFT, padx=10)

        # 状态栏
        self.status_var = tk.StringVar(value="未连接")
        status_bar = tk.Label(self.root, textvariable=self.status_var, bd=1, relief=tk.SUNKEN, anchor=tk.W)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

    def toggle_connect(self):
        if self.ser and self.ser.is_open:
            self.close_serial()
        else:
            self.open_serial()

    def open_serial(self):
        port = self.port_var.get()
        baud = int(self.baud_var.get())
        try:
            self.ser = serial.Serial(port, baud, timeout=0.1)
            self.running = True
            self.connect_btn.config(text="断开")
            self.status_var.set(f"已连接 {port}")
            # 启动读取线程
            self.read_thread = threading.Thread(target=self.serial_reader, daemon=True)
            self.read_thread.start()
        except Exception as e:
            messagebox.showerror("错误", f"无法打开串口:\n{e}")

    def close_serial(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connect_btn.config(text="连接")
        self.status_var.set("已断开")
        # 清空显示
        for label in self.pressure_labels:
            label.config(text="0.0")
        for bar in self.pressure_bars:
            bar.config(value=0)
        for label in self.lux_labels:
            label.config(text="0")
        for label in self.angle_labels.values():
            label.config(text="0.0")

    def serial_reader(self):
        buffer = ""
        while self.running and self.ser and self.ser.is_open:
            try:
                data = self.ser.read(1).decode('utf-8', errors='ignore')
                if data == '\n':
                    line = buffer.strip()
                    buffer = ""
                    if line:
                        self.parse_line(line)
                else:
                    buffer += data
            except Exception as e:
                print("读取错误:", e)
                break

    def parse_line(self, line):
        # 解析格式：P:F=xxx,R=xxx,B=xxx,L=xxx
        # A:roll=xxx,pitch=xxx,yaw=xxx
        # L:0=xxx,1=xxx,2=xxx,3=xxx
        with self.lock:
            if line.startswith("P:"):
                # 提取四个气压
                parts = line[2:].split(',')
                for part in parts:
                    if '=' in part:
                        key, val = part.split('=')
                        idx = ord(key) - ord('F')  # F=0,R=1,B=2,L=3
                        if 0 <= idx < 4:
                            try:
                                self.pressure[idx] = float(val)
                            except:
                                pass
            elif line.startswith("A:"):
                # 提取角度
                # 格式: roll=xx,pitch=xx,yaw=xx
                data = line[2:]
                items = data.split(',')
                for item in items:
                    if '=' in item:
                        key, val = item.split('=')
                        try:
                            v = float(val)
                            if key == 'roll':
                                self.roll = v
                            elif key == 'pitch':
                                self.pitch = v
                            elif key == 'yaw':
                                self.yaw = v
                        except:
                            pass
            elif line.startswith("L:"):
                # 光强
                parts = line[2:].split(',')
                for part in parts:
                    if '=' in part:
                        key, val = part.split('=')
                        try:
                            idx = int(key)
                            if 0 <= idx < 4:
                                self.lux[idx] = int(float(val))
                        except:
                            pass

    def update_display(self):
        # 更新UI，定期调用
        with self.lock:
            # 气压
            for i in range(4):
                val = self.pressure[i]
                # 范围映射到0~300，但实际可能负，进度条最小0，所以负值显示为0
                bar_val = max(0, val)  # 若显示负值需要调整，我们简单处理
                self.pressure_bars[i].config(value=bar_val)
                self.pressure_labels[i].config(text=f"{val:.1f}")
            # 光强
            for i in range(4):
                self.lux_labels[i].config(text=str(self.lux[i]))
            # 角度
            self.angle_labels["横滚"].config(text=f"{self.roll:.1f}")
            self.angle_labels["俯仰"].config(text=f"{self.pitch:.1f}")
            self.angle_labels["偏航"].config(text=f"{self.yaw:.1f}")

        self.root.after(100, self.update_display)  # 每100ms刷新

    def send_command(self, cmd):
        if self.ser and self.ser.is_open:
            self.ser.write((cmd + "\n").encode())
        else:
            messagebox.showwarning("警告", "串口未连接")

    def emergency_release(self):
        self.send_command("RELEASE")
        self.status_var.set("已发送一键放气命令")

    def emergency_stop(self):
        self.send_command("STOP")
        self.status_var.set("已发送紧急停止")

    def zero_angle(self):
        self.send_command("ZERO")
        self.status_var.set("正在归零角度...")

if __name__ == "__main__":
    root = tk.Tk()
    app = MainApp(root)
    root.mainloop()