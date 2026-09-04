import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import threading
import queue
import time
import re
import csv
import os
from datetime import datetime

# -------------------------- 数据解析 --------------------------
class DataStore:
    def __init__(self):
        self.pressure = [0.0, 0.0, 0.0, 0.0]  # 索引0=F,1=R,2=B,3=L
        self.light = [0, 0, 0, 0]              # 0~3
        self.roll = 0.0
        self.pitch = 0.0
        self.yaw = 0.0
        self.arrow_state = [False, False, False, False]  # 上、右、下、左

    def parse_line(self, line):
        line = line.strip()
        if line.startswith('P:'):
            parts = line[2:].split(',')
            for part in parts:
                if '=' in part:
                    key, val = part.split('=')
                    try:
                        val = float(val)
                    except:
                        continue
                    if key == 'F':
                        self.pressure[0] = val
                    elif key == 'R':
                        self.pressure[1] = val
                    elif key == 'B':
                        self.pressure[2] = val
                    elif key == 'L':
                        self.pressure[3] = val
        elif line.startswith('L:'):
            parts = line[2:].split(',')
            for part in parts:
                if '=' in part:
                    idx, val = part.split('=')
                    try:
                        idx = int(idx)
                        val = int(val)
                        if 0 <= idx < 4:
                            self.light[idx] = val
                    except:
                        pass
        elif line.startswith('A:'):
            m_roll = re.search(r'roll=([\d.-]+)', line)
            m_pitch = re.search(r'pitch=([\d.-]+)', line)
            m_yaw = re.search(r'yaw=([\d.-]+)', line)
            if m_roll:
                self.roll = float(m_roll.group(1))
            if m_pitch:
                self.pitch = float(m_pitch.group(1))
            if m_yaw:
                self.yaw = float(m_yaw.group(1))

    def update_arrow_state(self, light_values, avg):
        """根据光强与平均值的关系更新箭头状态"""
        # 检查是否所有气压 < -30 kPa（低压保护状态）
        all_below_minus30 = all(p < -30.0 for p in self.pressure)
        
        if all_below_minus30:
            # 低压保护：所有箭头熄灭
            self.arrow_state = [False, False, False, False]
            return
        
        # 正常光强控制逻辑
        # lightToDrive映射: 光强0→驱动器2,3; 光强1→驱动器1,2; 光强2→驱动器0,1; 光强3→驱动器0,3
        light_to_drive = [
            [2, 3],  # 光强0
            [1, 2],  # 光强1
            [0, 1],  # 光强2
            [0, 3]   # 光强3
        ]
        
        # 箭头索引: 0=上(驱动器0/F), 1=右(驱动器1/R), 2=下(驱动器2/B), 3=左(驱动器3/L)
        arrow_active = [False, False, False, False]
        
        for drive_idx in range(4):
            should_exhaust = False
            for light_idx in range(4):
                if drive_idx in light_to_drive[light_idx]:
                    if light_values[light_idx] > avg + 500.0:
                        should_exhaust = True
                        break
            
            if should_exhaust:
                arrow_active[drive_idx] = True
        
        self.arrow_state = arrow_active

# -------------------------- UI 主窗口 --------------------------
class App:
    def __init__(self, root):
        self.root = root
        self.root.title("气压/光强/角度监控 - 正方形布局")
        self.root.geometry("1000x800")  # 放大窗口

        self.data = DataStore()
        self.running = False
        self.serial_port = None
        self.reader_thread = None
        self.data_queue = queue.Queue()
        
        # CSV 相关
        self.csv_writer = None
        self.csv_file = None
        self.csv_enabled = False
        self.last_csv_time = 0

        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="9600")
        self.cmd_var = tk.StringVar(value="ZERO")

        self.create_widgets()
        self.update_ui()

    def create_widgets(self):
        main_frame = tk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        canvas_frame = tk.Frame(main_frame)
        canvas_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(canvas_frame, width=600, height=600, bg='white', relief=tk.SUNKEN, bd=2)
        self.canvas.pack(padx=5, pady=5)

        right_frame = tk.Frame(main_frame, width=320)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=False, padx=10)
        right_frame.pack_propagate(False)

        # 角度
        angle_label = tk.Label(right_frame, text="倾斜角度", font=('Arial', 14, 'bold'))
        angle_label.pack(pady=(0,5))
        self.roll_var = tk.StringVar(value="Roll: 0.0°")
        self.pitch_var = tk.StringVar(value="Pitch: 0.0°")
        self.yaw_var = tk.StringVar(value="Yaw: 0.0°")
        tk.Label(right_frame, textvariable=self.roll_var, font=('Arial', 12)).pack(anchor='w')
        tk.Label(right_frame, textvariable=self.pitch_var, font=('Arial', 12)).pack(anchor='w')
        tk.Label(right_frame, textvariable=self.yaw_var, font=('Arial', 12)).pack(anchor='w')

        ttk.Separator(right_frame, orient='horizontal').pack(fill='x', pady=10)

        # 数值列表
        val_label = tk.Label(right_frame, text="实时数值", font=('Arial', 14, 'bold'))
        val_label.pack(pady=(0,5))
        self.pressure_vars = [tk.StringVar(value=f"驱动器{i+1}: 0.0 kPa") for i in range(4)]
        self.light_vars = [tk.StringVar(value=f"光强{i}: 0") for i in range(4)]
        for i in range(4):
            tk.Label(right_frame, textvariable=self.pressure_vars[i], font=('Arial', 10), anchor='w').pack(fill='x')
        for i in range(4):
            tk.Label(right_frame, textvariable=self.light_vars[i], font=('Arial', 10), anchor='w').pack(fill='x')

        ttk.Separator(right_frame, orient='horizontal').pack(fill='x', pady=10)

        # 串口控制
        serial_frame = tk.LabelFrame(right_frame, text="串口控制", padx=5, pady=5)
        serial_frame.pack(fill='x', pady=5)

        port_row = tk.Frame(serial_frame)
        port_row.pack(fill='x', pady=2)
        tk.Label(port_row, text="端口:").pack(side=tk.LEFT)
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var, state='readonly', width=12)
        self.port_combo.pack(side=tk.LEFT, padx=5)
        tk.Button(port_row, text="刷新", command=self.refresh_ports).pack(side=tk.LEFT, padx=2)

        baud_row = tk.Frame(serial_frame)
        baud_row.pack(fill='x', pady=2)
        tk.Label(baud_row, text="波特率:").pack(side=tk.LEFT)
        baud_combo = ttk.Combobox(baud_row, textvariable=self.baud_var, values=['9600','115200','57600'], state='readonly', width=10)
        baud_combo.pack(side=tk.LEFT, padx=5)

        btn_row = tk.Frame(serial_frame)
        btn_row.pack(fill='x', pady=5)
        self.connect_btn = tk.Button(btn_row, text="连接", command=self.connect_serial, width=10)
        self.connect_btn.pack(side=tk.LEFT, padx=2)
        self.disconnect_btn = tk.Button(btn_row, text="断开", command=self.disconnect_serial, width=10, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT, padx=2)

        cmd_frame = tk.LabelFrame(right_frame, text="发送命令", padx=5, pady=5)
        cmd_frame.pack(fill='x', pady=5)

        cmd_row = tk.Frame(cmd_frame)
        cmd_row.pack(fill='x', pady=2)
        tk.Label(cmd_row, text="命令:").pack(side=tk.LEFT)
        cmd_entry = tk.Entry(cmd_row, textvariable=self.cmd_var, width=12)
        cmd_entry.pack(side=tk.LEFT, padx=5)
        tk.Button(cmd_row, text="发送", command=self.send_command).pack(side=tk.LEFT, padx=2)

        quick_row = tk.Frame(cmd_frame)
        quick_row.pack(fill='x', pady=2)
        tk.Button(quick_row, text="归零 (ZERO)", command=lambda: self.send_cmd_str("ZERO")).pack(side=tk.LEFT, padx=2)
        tk.Button(quick_row, text="放气 (VENT)", command=lambda: self.send_cmd_str("VENT")).pack(side=tk.LEFT, padx=2)
        tk.Button(quick_row, text="停止 (OFF)", command=lambda: self.send_cmd_str("OFF")).pack(side=tk.LEFT, padx=2)

        # CSV 控制 - 放到更显眼的位置
        csv_frame = tk.LabelFrame(right_frame, text="📊 数据记录", padx=5, pady=5, fg='blue')
        csv_frame.pack(fill='x', pady=5)
        
        csv_row = tk.Frame(csv_frame)
        csv_row.pack(fill='x', pady=5)
        self.csv_btn = tk.Button(csv_row, text="▶ 开始记录", command=self.toggle_csv, 
                                width=15, height=1, font=('Arial', 10, 'bold'),
                                bg='lightgreen', fg='black')
        self.csv_btn.pack(side=tk.LEFT, padx=5)
        self.csv_status = tk.Label(csv_row, text="⏸ 未记录", fg='gray', font=('Arial', 10))
        self.csv_status.pack(side=tk.LEFT, padx=10)
        
        # 显示当前记录文件
        self.csv_file_label = tk.Label(csv_frame, text="", fg='blue', font=('Arial', 8))
        self.csv_file_label.pack(anchor='w', pady=2)

        self.status_var = tk.StringVar(value="未连接")
        status_bar = tk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, anchor='w')
        status_bar.pack(side=tk.BOTTOM, fill='x')

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_var.set(ports[0])

    def connect_serial(self):
        port = self.port_var.get()
        if not port:
            messagebox.showerror("错误", "请选择串口号")
            return
        try:
            baud = int(self.baud_var.get())
            self.serial_port = serial.Serial(port, baud, timeout=0.1)
            self.running = True
            self.reader_thread = threading.Thread(target=self.serial_reader, daemon=True)
            self.reader_thread.start()
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            self.status_var.set(f"已连接 {port} @ {baud}")
        except Exception as e:
            messagebox.showerror("连接失败", str(e))

    def disconnect_serial(self):
        self.running = False
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=1)
        self.serial_port = None
        self.connect_btn.config(state=tk.NORMAL)
        self.disconnect_btn.config(state=tk.DISABLED)
        self.status_var.set("未连接")
        while not self.data_queue.empty():
            self.data_queue.get()
        
        if self.csv_enabled:
            self.toggle_csv()

    def serial_reader(self):
        while self.running and self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting > 0:
                    line = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        self.data_queue.put(line)
                else:
                    time.sleep(0.01)
            except Exception as e:
                print("读取错误:", e)
                break
        self.running = False

    def send_cmd_str(self, cmd):
        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.write((cmd + '\n').encode())
            except Exception as e:
                messagebox.showerror("发送失败", str(e))
        else:
            messagebox.showwarning("未连接", "请先连接串口")

    def send_command(self):
        cmd = self.cmd_var.get().strip()
        if cmd:
            self.send_cmd_str(cmd)

    def toggle_csv(self):
        if not self.csv_enabled:
            try:
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"data_log_{timestamp}.csv"
                
                self.csv_file = open(filename, 'w', newline='')
                self.csv_writer = csv.writer(self.csv_file)
                
                headers = ['时间', '光强0', '光强1', '光强2', '光强3', 
                          '气压F', '气压R', '气压B', '气压L',
                          'Roll', 'Pitch', 'Yaw']
                self.csv_writer.writerow(headers)
                self.csv_file.flush()
                
                self.csv_enabled = True
                self.csv_btn.config(text="⏹ 停止记录", bg='lightcoral')
                self.csv_status.config(text="● 记录中...", fg='green')
                self.csv_file_label.config(text=f"📁 {filename}")
                self.status_var.set(f"数据记录到: {filename}")
                
                print(f"开始记录数据到: {filename}")
                
            except Exception as e:
                messagebox.showerror("错误", f"无法创建CSV文件: {str(e)}")
        else:
            self.csv_enabled = False
            if self.csv_file:
                self.csv_file.close()
                self.csv_file = None
                self.csv_writer = None
            self.csv_btn.config(text="▶ 开始记录", bg='lightgreen')
            self.csv_status.config(text="⏸ 已停止", fg='red')
            self.csv_file_label.config(text="")
            self.status_var.set("数据记录已停止")

    def save_csv_data(self):
        if not self.csv_enabled or self.csv_writer is None:
            return
        
        try:
            current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
            
            row = [
                current_time,
                self.data.light[0], self.data.light[1], 
                self.data.light[2], self.data.light[3],
                self.data.pressure[0], self.data.pressure[1],
                self.data.pressure[2], self.data.pressure[3],
                self.data.roll, self.data.pitch, self.data.yaw
            ]
            
            self.csv_writer.writerow(row)
            self.csv_file.flush()
            
        except Exception as e:
            print(f"CSV写入错误: {e}")
            if self.csv_enabled:
                self.toggle_csv()

    def update_ui(self):
        while not self.data_queue.empty():
            line = self.data_queue.get()
            self.data.parse_line(line)
        
        if any(self.data.light):
            avg = sum(self.data.light) / 4.0
            self.data.update_arrow_state(self.data.light, avg)
        
        for i in range(4):
            self.pressure_vars[i].set(f"驱动器{i+1}: {self.data.pressure[i]:.1f} kPa")
            self.light_vars[i].set(f"光强{i}: {self.data.light[i]}")

        self.roll_var.set(f"Roll: {self.data.roll:.1f}°")
        self.pitch_var.set(f"Pitch: {self.data.pitch:.1f}°")
        self.yaw_var.set(f"Yaw: {self.data.yaw:.1f}°")

        self.draw_square()
        
        current_time = time.time() * 1000
        if current_time - self.last_csv_time >= 100:
            self.save_csv_data()
            self.last_csv_time = current_time
        
        self.root.after(100, self.update_ui)

    def draw_square(self):
        canvas = self.canvas
        canvas.delete("all")
        w = canvas.winfo_width()
        h = canvas.winfo_height()
        if w < 10 or h < 10:
            w, h = 600, 600
        margin = 50
        size = min(w, h) - 2 * margin
        x0, y0 = margin, margin
        x1, y1 = margin + size, margin + size
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        half = size // 2

        # 驱动器位置：左上=驱动器1(F)，右上=驱动器2(R)，右下=驱动器3(B)，左下=驱动器4(L)
        # 索引顺序：0=F, 1=R, 2=B, 3=L
        corners = [
            (cx - half, cy - half),  # 左上 -> 驱动器1 (F)
            (cx + half, cy - half),  # 右上 -> 驱动器2 (R)
            (cx + half, cy + half),  # 右下 -> 驱动器3 (B)
            (cx - half, cy + half)   # 左下 -> 驱动器4 (L)
        ]
        
        # 边中点：上、右、下、左 (对应传感器2,1,0,3)
        edges = [
            (cx, cy - half),  # 上边 -> 传感器2
            (cx + half, cy),  # 右边 -> 传感器1
            (cx, cy + half),  # 下边 -> 传感器0
            (cx - half, cy)   # 左边 -> 传感器3
        ]
        
        # 箭头位置：上、右、下、左 (对应驱动器1,2,3,4)
        arrow_positions = [
            (cx, cy - half//2),  # 上 -> 驱动器1 (F)
            (cx + half//2, cy),  # 右 -> 驱动器2 (R)
            (cx, cy + half//2),  # 下 -> 驱动器3 (B)
            (cx - half//2, cy)   # 左 -> 驱动器4 (L)
        ]

        canvas.create_rectangle(x0, y0, x1, y1, outline='black', width=2)

        # 绘制驱动器信息（使用1、2、3、4命名）
        for i, (x, y) in enumerate(corners):
            canvas.create_text(x, y-22, text=f"驱动器{i+1}", font=('Arial', 11, 'bold'), fill='blue')
            val = self.data.pressure[i]
            color = 'green' if val >= 0 else 'red'
            canvas.create_text(x, y+5, text=f"{val:.1f} kPa", font=('Arial', 10), fill=color)
            # 添加小标签显示原始方向
            dir_names = ['(F)', '(R)', '(B)', '(L)']
            canvas.create_text(x, y+22, text=dir_names[i], font=('Arial', 8), fill='gray')

        # 绘制光强传感器
        for i, (x, y) in enumerate(edges):
            canvas.create_oval(x-6, y-6, x+6, y+6, fill='darkorange', outline='black')
            if i == 0:      # 上
                dx, dy = 0, 20
            elif i == 1:    # 右
                dx, dy = -30, 0
            elif i == 2:    # 下
                dx, dy = 0, -20
            else:           # 左
                dx, dy = 30, 0
            canvas.create_text(x + dx, y + dy,
                               text=f"光强{i}\n{self.data.light[i]}",
                               font=('Arial', 9), fill='purple', justify='center')

        # 绘制箭头
        arrow_length = half // 3
        arrow_head_size = 12
        
        for idx, (x, y) in enumerate(arrow_positions):
            is_active = self.data.arrow_state[idx]
            color = 'red' if is_active else 'lightgray'
            
            if idx == 0:      # 上
                tip_x, tip_y = x, y - arrow_length
            elif idx == 1:    # 右
                tip_x, tip_y = x + arrow_length, y
            elif idx == 2:    # 下
                tip_x, tip_y = x, y + arrow_length
            else:             # 左
                tip_x, tip_y = x - arrow_length, y
            
            canvas.create_line(x, y, tip_x, tip_y, 
                              width=3 if is_active else 2, 
                              fill=color, arrow='last',
                              arrowshape=(arrow_head_size, arrow_head_size, arrow_head_size//2))
            
            if is_active:
                canvas.create_oval(x-8, y-8, x+8, y+8, 
                                  outline='', fill='red', stipple='gray50')
                dir_names = ['上', '右', '下', '左']
                canvas.create_text(x, y + (20 if idx == 0 else -20 if idx == 2 else 0),
                                  text=f"{dir_names[idx]} ↑", 
                                  font=('Arial', 8), fill='red')

        # 中心显示角度概要
        angle_text = f"Roll:{self.data.roll:.1f}°  Pitch:{self.data.pitch:.1f}°  Yaw:{self.data.yaw:.1f}°"
        canvas.create_text(cx, cy + half//4, text=angle_text, font=('Arial', 10), fill='gray')

if __name__ == "__main__":
    root = tk.Tk()
    app = App(root)
    root.mainloop()
