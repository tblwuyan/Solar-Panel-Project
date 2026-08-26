import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import time

class PneumaticController:
    def __init__(self, root):
        self.root = root
        self.root.title("气动系统控制 - 方向+模式")
        self.root.geometry("420x440")
        self.root.resizable(False, False)

        self.ser = None
        self.connected = False

        self.current_direction = None
        self.current_mode = tk.StringVar(value="C")

        self.create_widgets()
        self.refresh_ports()

    def create_widgets(self):
        # ---- 串口设置 ----
        frame_top = ttk.LabelFrame(self.root, text="串口设置", padding=5)
        frame_top.pack(fill="x", padx=10, pady=5)

        ttk.Label(frame_top, text="端口:").grid(row=0, column=0, padx=5, pady=5)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(frame_top, textvariable=self.port_var, width=15)
        self.port_combo.grid(row=0, column=1, padx=5, pady=5)

        self.refresh_btn = ttk.Button(frame_top, text="刷新", command=self.refresh_ports)
        self.refresh_btn.grid(row=0, column=2, padx=5)

        self.connect_btn = ttk.Button(frame_top, text="连接", command=self.toggle_connect)
        self.connect_btn.grid(row=0, column=3, padx=5)

        self.status_label = ttk.Label(frame_top, text="未连接", foreground="red")
        self.status_label.grid(row=0, column=4, padx=10)

        # ---- 模式选择 ----
        frame_mode = ttk.LabelFrame(self.root, text="模式", padding=10)
        frame_mode.pack(fill="x", padx=10, pady=5)

        ttk.Radiobutton(frame_mode, text="充气", variable=self.current_mode,
                        value="C").pack(side="left", padx=20)
        ttk.Radiobutton(frame_mode, text="吸气", variable=self.current_mode,
                        value="I").pack(side="left", padx=20)

        # ---- 方向按钮 + 关闭按钮 ----
        frame_dir = ttk.LabelFrame(self.root, text="方向 (点击即发送)", padding=10)
        frame_dir.pack(fill="both", expand=True, padx=10, pady=5)

        self.dir_btns = {}
        btn_config = {
            'F': {'text': '前 ▲', 'row': 0, 'col': 1},
            'R': {'text': '右 ►', 'row': 1, 'col': 2},
            'B': {'text': '后 ▼', 'row': 2, 'col': 1},
            'L': {'text': '左 ◄', 'row': 1, 'col': 0},
        }
        for key, cfg in btn_config.items():
            btn = tk.Button(frame_dir, text=cfg['text'], width=8, height=2,
                            font=("Arial", 12),
                            command=lambda d=key: self.send_direction(d))
            btn.grid(row=cfg['row'], column=cfg['col'], padx=10, pady=10, sticky="nsew")
            self.dir_btns[key] = btn
            btn.config(state="disabled")

        # 新增 "关闭" 按钮，占满底部一行（跨3列）
        self.close_btn = tk.Button(frame_dir, text="关闭", font=("Arial", 14, "bold"),
                                   bg="orange", fg="white",
                                   command=self.close_all)
        self.close_btn.grid(row=3, column=0, columnspan=3, padx=10, pady=10, sticky="ew")
        self.close_btn.config(state="disabled")

        for r in range(4):
            frame_dir.grid_rowconfigure(r, weight=1)
        for c in range(3):
            frame_dir.grid_columnconfigure(c, weight=1)

        # ---- 紧急停止（原有功能保留） ----
        frame_stop = ttk.LabelFrame(self.root, text="紧急停止", padding=5)
        frame_stop.pack(fill="x", padx=10, pady=5)
        self.stop_btn = ttk.Button(frame_stop, text="紧急停止", command=self.emergency_stop)
        self.stop_btn.pack(pady=5)
        self.stop_btn.config(state="disabled")

        # ---- 状态显示 ----
        frame_status = ttk.LabelFrame(self.root, text="当前状态", padding=5)
        frame_status.pack(fill="x", padx=10, pady=5)
        self.status_text = tk.StringVar(value="未操作")
        ttk.Label(frame_status, textvariable=self.status_text).pack()

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
            for btn in self.dir_btns.values():
                btn.config(state="disabled")
            self.close_btn.config(state="disabled")
            self.stop_btn.config(state="disabled")
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
                for btn in self.dir_btns.values():
                    btn.config(state="normal")
                self.close_btn.config(state="normal")
                self.stop_btn.config(state="normal")
                self.close_all()  # 确保初始全关
                self.status_text.set("已就绪")
            except Exception as e:
                messagebox.showerror("连接失败", str(e))
                self.connected = False
                self.connect_btn.config(text="连接")
                self.status_label.config(text="未连接", foreground="red")

    def send_command(self, cmd):
        try:
            self.ser.write((cmd + "\n").encode())
        except Exception as e:
            messagebox.showerror("发送错误", str(e))

    def send_direction(self, direction):
        if not self.connected:
            messagebox.showwarning("警告", "请先连接串口")
            return
        mode = self.current_mode.get()
        cmd = direction + mode
        self.send_command(cmd)
        dir_names = {'F': '前', 'R': '右', 'B': '后', 'L': '左'}
        mode_names = {'C': '充气', 'I': '吸气'}
        self.status_text.set(f"发送: {dir_names[direction]} + {mode_names[mode]}")

    def close_all(self):
        """关闭所有引脚（发送 OFF 命令）"""
        if not self.connected:
            return
        self.send_command("OFF")
        self.status_text.set("已关闭所有引脚")

    def emergency_stop(self):
        """紧急停止（发送 ST 命令，效果相同）"""
        if not self.connected:
            return
        self.send_command("ST")
        self.status_text.set("紧急停止 (全部关闭)")

    def on_closing(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = PneumaticController(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()