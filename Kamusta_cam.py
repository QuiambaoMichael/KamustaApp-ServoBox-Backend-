import cv2
import time
import os
import threading
from ftplib import FTP, error_perm
import mysql.connector
from mysql.connector import Error
import tkinter as tk
from tkinter import messagebox
from tkinter import ttk
from datetime import datetime

# Interval to capture frames (in seconds)
capture_interval = 0.1

# Directory to save frames temporarily
frame_dir = "captured_frames"
os.makedirs(frame_dir, exist_ok=True)

capturing = False  # Flag to control capturing

# Remote path template for FTP upload
remote_path_template = "livefeed/{filename}"
status_label = None

def update_status(message, is_error=False):
    """Update the status label in the GUI with the provided message."""
    status_label.config(text=message)
    status_label.config(fg="red" if is_error else "green")

def generate_unique_filename(filename):
    """Generate a unique filename by appending a timestamp if the file exists."""
    base, ext = os.path.splitext(filename)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"{base}_{timestamp}{ext}"

def insert_livefeed_entry(patient_video, patient_id, db_details):
    """Inserts a new entry into the patient_livefeed table."""
    try:
        connection = mysql.connector.connect(
            host=db_details["host"],
            user=db_details["user"],
            password=db_details["password"],
            database=db_details["name"]
        )
        if connection.is_connected():
            cursor = connection.cursor()
            query = "INSERT INTO patient_livefeed (patient_video, patient_id) VALUES (%s, %s)"
            cursor.execute(query, (patient_video, patient_id))
            connection.commit()
            print(f"Inserted video '{patient_video}' for patient ID '{patient_id}'.")
    except Error as e:
        update_status(f"Database Error: {e}", is_error=True)
    finally:
        if connection.is_connected():
            cursor.close()
            connection.close()

def fetch_patient_ids_with_on_status(db_details):
    """Fetches patient IDs where hardware_status is 'ON'."""
    patient_ids = []
    try:
        connection = mysql.connector.connect(
            host=db_details["host"],
            user=db_details["user"],
            password=db_details["password"],
            database=db_details["name"]
        )
        if connection.is_connected():
            cursor = connection.cursor()
            cursor.execute("SELECT id FROM patient WHERE hardware_status = 'ON'")
            patient_ids = [row[0] for row in cursor.fetchall()]
            print(f"Found patient IDs with 'ON' status: {patient_ids}")
    except Error as e:
        update_status(f"Database Error: {e}", is_error=True)
    finally:
        if connection.is_connected():   
            cursor.close()
            connection.close()
    return patient_ids

def capture_and_compile_video(stream_url, ftp_details, db_details):
    """Captures frames from the ESP32-CAM stream and compiles them into an MP4 video."""
    global capturing

    cap = cv2.VideoCapture(stream_url)
    if not cap.isOpened():
        update_status("Stream Error: Could not open video stream. Check the stream URL.", is_error=True)
        return False

    output_video = generate_unique_filename("output.mp4")  # Ensure unique filename

    fourcc = cv2.VideoWriter_fourcc(*'H264')
    fps = int(1 / capture_interval)
    frame_size = (640, 480)
    out = cv2.VideoWriter(output_video, fourcc, fps, frame_size)

    try:
        while capturing:
            ret, frame = cap.read()
            if not ret:
                update_status("Stream Error: Could not read frame from stream.", is_error=True)
                break
            frame = cv2.resize(frame, frame_size)
            out.write(frame)
            time.sleep(capture_interval)
    except Exception as e:
        update_status(f"Capture Error: {e}", is_error=True)
    finally:
        cap.release()
        out.release()
        update_status(f"Video saved as {output_video}")

    # Once capturing stops, upload to FTP and insert into the database
    upload_video_to_ftp(output_video, ftp_details)
    insert_video_to_database(output_video, db_details)
    return output_video

def upload_video_to_ftp(output_video, ftp_details):
    """Upload the captured video to FTP server."""
    try:
        with FTP(ftp_details["server"]) as ftp:
            ftp.login(user=ftp_details["username"], passwd=ftp_details["password"])
            filename = os.path.basename(output_video)
            remote_path = remote_path_template.format(filename=filename)

            if file_exists_on_ftp(ftp, remote_path):
                filename = generate_unique_filename(filename)
                remote_path = remote_path_template.format(filename=filename)

            remote_dirs = remote_path.split('/')[:-1]
            for directory in remote_dirs:
                try:
                    ftp.cwd(directory)
                except error_perm:
                    ftp.mkd(directory)
                    ftp.cwd(directory)

            with open(output_video, 'rb') as video_file:
                ftp.storbinary(f'STOR {filename}', video_file)
                
        update_status("Upload to FTP completed!")
    except Exception as e:
        update_status(f"FTP Upload Error: {e}", is_error=True)

def file_exists_on_ftp(ftp, remote_path):
    """Check if a file exists on the FTP server."""
    try:
        files = ftp.nlst(os.path.dirname(remote_path))
        return os.path.basename(remote_path) in files
    except Exception as e:
        print(f"Error checking file on FTP: {e}")
        return False

def insert_video_to_database(output_video, db_details):
    """Insert video entry into the database."""
    patient_ids = fetch_patient_ids_with_on_status(db_details)
    for patient_id in patient_ids:
        insert_livefeed_entry(output_video, patient_id, db_details)
    update_status("Database entry completed!")

def start_capture():
    global capturing
    capturing = True
    stream_url = stream_url_entry.get()
    ftp_details = {
        "server": ftp_server_entry.get(),
        "username": ftp_user_entry.get(),
        "password": ftp_password_entry.get()
    }
    db_details = {
        "host": db_host_entry.get(),
        "user": db_user_entry.get(),
        "password": db_password_entry.get(),
        "name": db_name_entry.get()
    }
    
    # Start capturing video in a separate thread
    threading.Thread(target=capture_and_compile_video, args=(stream_url, ftp_details, db_details), daemon=True).start()
    update_status("Capturing in progress...")

def stop_capture():
    global capturing
    capturing = False
    update_status("Capture stopped, uploading video...")


root = tk.Tk()
root.title("ESP32-CAM Capture and Upload")

# Set the window size (Width x Height)
root.geometry("700x500")  # Increased size

# Configure the grid to scale properly when resizing
root.grid_rowconfigure(0, weight=1)
root.grid_columnconfigure(0, weight=1)
root.grid_columnconfigure(1, weight=3)

# Add Labels and Entries for user inputs
tk.Label(root, text="ESP32-CAM Stream URL:").grid(row=0, column=0, sticky="e", padx=10, pady=5)
stream_url_entry = tk.Entry(root, width=40)  # Increased width
stream_url_entry.insert(0, "http://192.168.0.100:81/stream")
stream_url_entry.grid(row=0, column=1, padx=10, pady=5)

tk.Label(root, text="FTP Server:").grid(row=1, column=0, sticky="e", padx=10, pady=5)
ftp_server_entry = tk.Entry(root, width=40)  # Increased width
ftp_server_entry.insert(0, "ftp.kamustaph.com")
ftp_server_entry.grid(row=1, column=1, padx=10, pady=5)

tk.Label(root, text="FTP Username:").grid(row=2, column=0, sticky="e", padx=10, pady=5)
ftp_user_entry = tk.Entry(root, width=40)  # Increased width
ftp_user_entry.insert(0, "u693536525.kamusta")
ftp_user_entry.grid(row=2, column=1, padx=10, pady=5)

tk.Label(root, text="FTP Password:").grid(row=3, column=0, sticky="e", padx=10, pady=5)
ftp_password_entry = tk.Entry(root, show="*", width=40)  # Increased width
ftp_password_entry.insert(0, "O?x2;+>cjdn5ue]R")
ftp_password_entry.grid(row=3, column=1, padx=10, pady=5)

tk.Label(root, text="Database Host:").grid(row=4, column=0, sticky="e", padx=10, pady=5)
db_host_entry = tk.Entry(root, width=40)  # Increased width
db_host_entry.insert(0, "153.92.15.3")
db_host_entry.grid(row=4, column=1, padx=10, pady=5)

tk.Label(root, text="Database User:").grid(row=5, column=0, sticky="e", padx=10, pady=5)
db_user_entry = tk.Entry(root, width=40)  # Increased width
db_user_entry.insert(0, "u693536525_user")
db_user_entry.grid(row=5, column=1, padx=10, pady=5)

tk.Label(root, text="Database Password:").grid(row=6, column=0, sticky="e", padx=10, pady=5)
db_password_entry = tk.Entry(root, show="*", width=40)  # Increased width
db_password_entry.insert(0, "&y2G6?tH")
db_password_entry.grid(row=6, column=1, padx=10, pady=5)

tk.Label(root, text="Database Name:").grid(row=7, column=0, sticky="e", padx=10, pady=5)
db_name_entry = tk.Entry(root, width=40)  # Increased width
db_name_entry.insert(0, "u693536525_kamusta")
db_name_entry.grid(row=7, column=1, padx=10, pady=5)

# Start and Stop buttons
start_button = tk.Button(root, text="Start Capture", command=start_capture)
start_button.grid(row=8, column=0, pady=10, padx=10)

stop_button = tk.Button(root, text="Stop Capture", command=stop_capture)
stop_button.grid(row=8, column=1, pady=10, padx=10)

# Status label
status_label = tk.Label(root, text="Ready", fg="green", width=50)
status_label.grid(row=9, column=0, columnspan=2, pady=10)

# Start the GUI main loop
root.mainloop()


