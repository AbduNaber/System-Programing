#!/usr/bin/env python3
"""
Chat Client Implementation
Compile/Run: python3 chatclient.py <server_ip> <port>
"""

import socket
import threading
import sys
import signal
import time
import os
import select
from typing import Optional
from dataclasses import dataclass

# Constants
BUFFER_SIZE = 1024
MAX_USERNAME_LENGTH = 16
MAX_FILE_SIZE = 3 * 1024 * 1024  # 3 MB

# ANSI Color codes
ANSI_COLOR_SUCCESS = "\x1b[32m"      # Green
ANSI_COLOR_ERROR = "\x1b[31m"        # Red
ANSI_COLOR_WARNING = "\x1b[33m"      # Yellow
ANSI_COLOR_INFO = "\x1b[36m"         # Cyan
ANSI_COLOR_PRIVATE = "\x1b[35m"      # Magenta
ANSI_COLOR_BROADCAST = "\x1b[34m"    # Blue
ANSI_COLOR_SYSTEM = "\x1b[37m"       # White
ANSI_COLOR_USERNAME = "\x1b[1;36m"   # Bold cyan
ANSI_COLOR_FILENAME = "\x1b[1;33m"   # Bold yellow
ANSI_COLOR_PROMPT = "\x1b[1;32m"     # Bold green
ANSI_COLOR_RESET = "\x1b[0m"         # Reset

class ChatClient:
    def __init__(self, server_ip: str, port: int):
        self.server_ip = server_ip
        self.port = port
        self.socket = None
        self.username = ""
        self.is_running = True
        self.server_response_thread = None
        
        # Thread synchronization
        self.ready_for_file = threading.Event()
        self.file_transfer_finished = threading.Event()
        self.file_transfer_in_progress = threading.Lock()
        self.socket_lock = threading.Lock()
        self.prompt_lock = threading.Lock()
        self.prompt_shown = False
        
        # Setup signal handlers
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)

    def signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        print(f"{ANSI_COLOR_WARNING}\n[SYSTEM] Client shutting down gracefully...{ANSI_COLOR_RESET}")
        self.is_running = False

    def print_status_message(self, message: str, color: str):
        """Print colored status message"""
        print(f"{color}{message}{ANSI_COLOR_RESET}")
        sys.stdout.flush()

    def show_prompt(self):
        """Show input prompt"""
        with self.prompt_lock:
            if not self.prompt_shown:
                print(f"{ANSI_COLOR_PROMPT}> {ANSI_COLOR_RESET}", end='', flush=True)
                self.prompt_shown = True

    def clear_prompt(self):
        """Clear current line"""
        with self.prompt_lock:
            if self.prompt_shown:
                print("\r\033[K", end='', flush=True)
                self.prompt_shown = False

    def socket_send(self, data: bytes) -> int:
        """Thread-safe socket send"""
        with self.socket_lock:
            return self.socket.send(data)

    def print_help_menu(self):
        """Print help menu"""
        print(f"{ANSI_COLOR_SYSTEM}\n╔══════════════════════════════════════════════════════════╗")
        print("║                    AVAILABLE COMMANDS                   ║")
        print("╠══════════════════════════════════════════════════════════╣")
        print("║ /help                    - Show this help menu          ║")
        print("║ /list                    - List all online users        ║")
        print("║ /private <username> <msg> - Send private message        ║")
        print("║ /file <username> <path>  - Send file to user            ║")
        print("║ /broadcast <message>     - Send message to all users    ║")
        print("║ /quit                    - Leave the chat               ║")
        print("║                                                          ║")
        print("║ Note: Messages without commands are public messages     ║")
        print(f"╚══════════════════════════════════════════════════════════╝{ANSI_COLOR_RESET}")

    def connect_to_server(self) -> bool:
        """Connect to chat server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.server_ip, self.port))
            self.print_status_message(f"[SUCCESS] Connected to server at {self.server_ip}:{self.port}", ANSI_COLOR_SUCCESS)
            return True
        except Exception as e:
            self.print_status_message(f"[ERROR] Failed to connect: {e}", ANSI_COLOR_ERROR)
            return False

    def register_username(self) -> bool:
        """Register username with server"""
        while True:
            username = input(f"{ANSI_COLOR_USERNAME}Enter username (max {MAX_USERNAME_LENGTH} chars): {ANSI_COLOR_RESET}")
            
            if not username or len(username) > MAX_USERNAME_LENGTH:
                self.print_status_message(f"[ERROR] Username must be 1-{MAX_USERNAME_LENGTH} characters", ANSI_COLOR_ERROR)
                continue
                
            try:
                # Send username to server
                username_data = username.encode('utf-8')
                self.socket_send(username_data)
                
                # Wait for server response
                response = self.socket.recv(BUFFER_SIZE).decode('utf-8')
                
                if response == "USERNAME_ACCEPTED":
                    self.username = username
                    self.print_status_message(f"[SUCCESS] Username '{username}' registered successfully!", ANSI_COLOR_SUCCESS)
                    return True
                elif response == "USERNAME_TAKEN":
                    self.print_status_message("[ERROR] Username already taken. Please choose another.", ANSI_COLOR_ERROR)
                else:
                    self.print_status_message(f"[ERROR] Server error: {response}", ANSI_COLOR_ERROR)
                    
            except Exception as e:
                self.print_status_message(f"[ERROR] Registration failed: {e}", ANSI_COLOR_ERROR)
                return False

    def receive_messages(self):
        """Thread function to receive messages from server"""
        while self.is_running:
            try:
                # Use select to check if data is available
                ready, _, _ = select.select([self.socket], [], [], 0.1)
                if not ready:
                    continue
                    
                data = self.socket.recv(BUFFER_SIZE)
                if not data:
                    break
                    
                message = data.decode('utf-8')
                
                # Handle different message types
                if message.startswith("PRIVATE_FROM:"):
                    self.handle_private_message(message)
                elif message.startswith("BROADCAST_FROM:"):
                    self.handle_broadcast_message(message)
                elif message.startswith("PUBLIC_FROM:"):
                    self.handle_public_message(message)
                elif message.startswith("USER_LIST:"):
                    self.handle_user_list(message)
                elif message.startswith("FILE_REQUEST:"):
                    self.handle_file_request(message)
                elif message.startswith("FILE_ACCEPT"):
                    self.handle_file_accept()
                elif message.startswith("FILE_REJECT"):
                    self.handle_file_reject()
                elif message.startswith("SYSTEM:"):
                    self.handle_system_message(message)
                else:
                    self.clear_prompt()
                    print(f"{ANSI_COLOR_INFO}[SERVER] {message}{ANSI_COLOR_RESET}")
                    self.show_prompt()
                    
            except socket.timeout:
                continue
            except Exception as e:
                if self.is_running:
                    self.print_status_message(f"[ERROR] Connection lost: {e}", ANSI_COLOR_ERROR)
                break
                
        self.is_running = False

    def handle_private_message(self, message: str):
        """Handle incoming private message"""
        parts = message.split(":", 2)
        if len(parts) >= 3:
            sender = parts[1]
            msg_content = parts[2]
            self.clear_prompt()
            print(f"{ANSI_COLOR_PRIVATE}[PRIVATE from {sender}] {msg_content}{ANSI_COLOR_RESET}")
            self.show_prompt()

    def handle_broadcast_message(self, message: str):
        """Handle incoming broadcast message"""
        parts = message.split(":", 2)
        if len(parts) >= 3:
            sender = parts[1]
            msg_content = parts[2]
            self.clear_prompt()
            print(f"{ANSI_COLOR_BROADCAST}[BROADCAST from {sender}] {msg_content}{ANSI_COLOR_RESET}")
            self.show_prompt()

    def handle_public_message(self, message: str):
        """Handle incoming public message"""
        parts = message.split(":", 2)
        if len(parts) >= 3:
            sender = parts[1]
            msg_content = parts[2]
            self.clear_prompt()
            print(f"{ANSI_COLOR_INFO}[{sender}] {msg_content}{ANSI_COLOR_RESET}")
            self.show_prompt()

    def handle_user_list(self, message: str):
        """Handle user list response"""
        users = message.split(":", 1)[1] if ":" in message else ""
        self.clear_prompt()
        print(f"{ANSI_COLOR_SYSTEM}[ONLINE USERS] {users}{ANSI_COLOR_RESET}")
        self.show_prompt()

    def handle_system_message(self, message: str):
        """Handle system messages"""
        system_msg = message.split(":", 1)[1] if ":" in message else message
        self.clear_prompt()
        print(f"{ANSI_COLOR_SYSTEM}[SYSTEM] {system_msg}{ANSI_COLOR_RESET}")
        self.show_prompt()

    def handle_file_request(self, message: str):
        """Handle incoming file transfer request"""
        parts = message.split(":")
        if len(parts) >= 3:
            sender = parts[1]
            filename = parts[2]
            self.clear_prompt()
            print(f"{ANSI_COLOR_WARNING}[FILE REQUEST] {sender} wants to send you: {ANSI_COLOR_FILENAME}{filename}{ANSI_COLOR_RESET}")
            response = input(f"{ANSI_COLOR_WARNING}Accept file? (y/n): {ANSI_COLOR_RESET}")
            
            if response.lower() in ['y', 'yes']:
                self.socket_send(f"FILE_ACCEPT:{sender}".encode('utf-8'))
                self.receive_file(sender, filename)
            else:
                self.socket_send(f"FILE_REJECT:{sender}".encode('utf-8'))
            
            self.show_prompt()

    def handle_file_accept(self):
        """Handle file transfer acceptance"""
        self.ready_for_file.set()

    def handle_file_reject(self):
        """Handle file transfer rejection"""
        self.print_status_message("[INFO] File transfer rejected by recipient", ANSI_COLOR_WARNING)
        self.ready_for_file.set()

    def receive_file(self, sender: str, filename: str):
        """Receive file from sender"""
        try:
            with self.file_transfer_in_progress:
                # Create downloads directory if it doesn't exist
                downloads_dir = "downloads"
                os.makedirs(downloads_dir, exist_ok=True)
                
                # Generate unique filename to avoid conflicts
                base_name, ext = os.path.splitext(filename)
                counter = 1
                safe_filename = filename
                while os.path.exists(os.path.join(downloads_dir, safe_filename)):
                    safe_filename = f"{base_name}_{counter}{ext}"
                    counter += 1
                
                filepath = os.path.join(downloads_dir, safe_filename)
                
                # Receive file size first
                size_data = self.socket.recv(8)
                file_size = int.from_bytes(size_data, byteorder='big')
                
                self.print_status_message(f"[FILE] Receiving {filename} ({file_size} bytes)...", ANSI_COLOR_INFO)
                
                # Receive file data
                received_size = 0
                with open(filepath, 'wb') as f:
                    while received_size < file_size:
                        chunk_size = min(BUFFER_SIZE, file_size - received_size)
                        chunk = self.socket.recv(chunk_size)
                        if not chunk:
                            break
                        f.write(chunk)
                        received_size += len(chunk)
                        
                        # Show progress
                        progress = (received_size / file_size) * 100
                        print(f"\r{ANSI_COLOR_INFO}Progress: {progress:.1f}%{ANSI_COLOR_RESET}", end='', flush=True)
                
                print()  # New line after progress
                
                if received_size == file_size:
                    self.print_status_message(f"[SUCCESS] File saved as: {filepath}", ANSI_COLOR_SUCCESS)
                else:
                    self.print_status_message("[ERROR] File transfer incomplete", ANSI_COLOR_ERROR)
                    
        except Exception as e:
            self.print_status_message(f"[ERROR] File receive failed: {e}", ANSI_COLOR_ERROR)

    def send_file(self, recipient: str, filepath: str):
        """Send file to recipient"""
        try:
            if not os.path.exists(filepath):
                self.print_status_message(f"[ERROR] File not found: {filepath}", ANSI_COLOR_ERROR)
                return
                
            file_size = os.path.getsize(filepath)
            if file_size > MAX_FILE_SIZE:
                self.print_status_message(f"[ERROR] File too large (max {MAX_FILE_SIZE} bytes)", ANSI_COLOR_ERROR)
                return
                
            filename = os.path.basename(filepath)
            
            # Send file request
            request = f"FILE_REQUEST:{recipient}:{filename}"
            self.socket_send(request.encode('utf-8'))
            
            # Wait for acceptance
            self.ready_for_file.clear()
            if not self.ready_for_file.wait(timeout=30):
                self.print_status_message("[ERROR] File transfer timeout", ANSI_COLOR_ERROR)
                return
                
            with self.file_transfer_in_progress:
                # Send file size
                size_bytes = file_size.to_bytes(8, byteorder='big')
                self.socket_send(size_bytes)
                
                self.print_status_message(f"[FILE] Sending {filename} ({file_size} bytes)...", ANSI_COLOR_INFO)
                
                # Send file data
                sent_size = 0
                with open(filepath, 'rb') as f:
                    while sent_size < file_size:
                        chunk = f.read(BUFFER_SIZE)
                        if not chunk:
                            break
                        self.socket_send(chunk)
                        sent_size += len(chunk)
                        
                        # Show progress
                        progress = (sent_size / file_size) * 100
                        print(f"\r{ANSI_COLOR_INFO}Progress: {progress:.1f}%{ANSI_COLOR_RESET}", end='', flush=True)
                
                print()  # New line after progress
                self.print_status_message("[SUCCESS] File sent successfully", ANSI_COLOR_SUCCESS)
                
        except Exception as e:
            self.print_status_message(f"[ERROR] File send failed: {e}", ANSI_COLOR_ERROR)

    def process_command(self, command: str):
        """Process user commands"""
        try:
            if command.startswith('/help'):
                self.print_help_menu()
                
            elif command.startswith('/list'):
                self.socket_send("LIST_USERS".encode('utf-8'))
                
            elif command.startswith('/private'):
                parts = command.split(' ', 2)
                if len(parts) < 3:
                    self.print_status_message("[ERROR] Usage: /private <username> <message>", ANSI_COLOR_ERROR)
                else:
                    recipient, message = parts[1], parts[2]
                    msg = f"PRIVATE:{recipient}:{message}"
                    self.socket_send(msg.encode('utf-8'))
                    
            elif command.startswith('/file'):
                parts = command.split(' ', 2)
                if len(parts) < 3:
                    self.print_status_message("[ERROR] Usage: /file <username> <filepath>", ANSI_COLOR_ERROR)
                else:
                    recipient, filepath = parts[1], parts[2]
                    self.send_file(recipient, filepath)
                    
            elif command.startswith('/broadcast'):
                parts = command.split(' ', 1)
                if len(parts) < 2:
                    self.print_status_message("[ERROR] Usage: /broadcast <message>", ANSI_COLOR_ERROR)
                else:
                    message = parts[1]
                    msg = f"BROADCAST:{message}"
                    self.socket_send(msg.encode('utf-8'))
                    
            elif command.startswith('/quit'):
                self.is_running = False
                
            else:
                self.print_status_message("[ERROR] Unknown command. Type /help for available commands.", ANSI_COLOR_ERROR)
                
        except Exception as e:
            self.print_status_message(f"[ERROR] Command failed: {e}", ANSI_COLOR_ERROR)

    def run(self):
        """Main client loop"""
        # Connect to server
        if not self.connect_to_server():
            return
            
        # Register username
        if not self.register_username():
            return
            
        # Start message receiving thread
        self.server_response_thread = threading.Thread(target=self.receive_messages, daemon=True)
        self.server_response_thread.start()
        
        # Welcome message
        self.print_status_message(f"[SUCCESS] Welcome to the chat, {self.username}!", ANSI_COLOR_SUCCESS)
        self.print_status_message("[INFO] Type /help for available commands", ANSI_COLOR_INFO)
        
        # Main input loop
        try:
            while self.is_running:
                self.show_prompt()
                
                # Use select to check for input with timeout
                ready, _, _ = select.select([sys.stdin], [], [], 0.1)
                if ready:
                    user_input = input().strip()
                    self.clear_prompt()
                    
                    if not user_input:
                        continue
                        
                    if user_input.startswith('/'):
                        self.process_command(user_input)
                    else:
                        # Send public message
                        msg = f"PUBLIC:{user_input}"
                        self.socket_send(msg.encode('utf-8'))
                        
        except KeyboardInterrupt:
            pass
        except EOFError:
            pass
        finally:
            self.cleanup()

    def cleanup(self):
        """Clean up resources"""
        self.is_running = False
        
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
                
        self.print_status_message("[SYSTEM] Disconnected from server", ANSI_COLOR_WARNING)


def main():
    """Main function"""
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <server_ip> <port>")
        sys.exit(1)
        
    try:
        server_ip = sys.argv[1]
        port = int(sys.argv[2])
        
        client = ChatClient(server_ip, port)
        client.run()
        
    except ValueError:
        print("Error: Port must be a number")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()