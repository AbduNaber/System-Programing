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
        self.current_room = ""
        self.is_running = True
        self.server_response_thread = None
        
        # Thread synchronization
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
        self.clear_prompt()
        print(f"{color}{message}{ANSI_COLOR_RESET}")
        sys.stdout.flush()
        self.show_prompt()

    def show_prompt(self):
        """Show input prompt"""
        with self.prompt_lock:
            if not self.prompt_shown and self.is_running:
                print(f"{ANSI_COLOR_PROMPT}> {ANSI_COLOR_RESET}", end='', flush=True)
                self.prompt_shown = True

    def clear_prompt(self):
        """Clear current line"""
        with self.prompt_lock:
            if self.prompt_shown:
                print("\r\033[K", end='', flush=True)
                self.prompt_shown = False

    def socket_send(self, data: bytes) -> bool:
        """Thread-safe socket send"""
        try:
            with self.socket_lock:
                self.socket.send(data)
            return True
        except:
            return False

    def print_help_menu(self):
        """Print help menu"""
        self.clear_prompt()
        print(f"{ANSI_COLOR_SYSTEM}\n╔══════════════════════════════════════════════════════════╗")
        print("║                    AVAILABLE COMMANDS                   ║")
        print("╠══════════════════════════════════════════════════════════╣")
        print("║ /join <room_name>        - Join or create a room        ║")
        print("║ /leave                   - Leave current room           ║")
        print("║ /broadcast <message>     - Send message to room         ║")
        print("║ /whisper <user> <msg>    - Send private message         ║")
        print("║ /sendfile <file> <user>  - Send file to user            ║")
        print("║ /list                    - List users in current room   ║")
        print("║ /exit                    - Leave the chat               ║")
        print("║                                                          ║")
        print("║ Note: Must join a room before broadcasting              ║")
        print(f"╚══════════════════════════════════════════════════════════╝{ANSI_COLOR_RESET}")
        self.show_prompt()

    def connect_to_server(self) -> bool:
        """Connect to chat server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.server_ip, self.port))
            
            # Wait for initial server response
            response = self.socket.recv(BUFFER_SIZE).decode('utf-8')
            if response == "SUCCESS_LOGIN":
                self.print_status_message(f"[SUCCESS] Connected to server at {self.server_ip}:{self.port}", ANSI_COLOR_SUCCESS)
                return True
            else:
                self.print_status_message(f"[ERROR] Server rejected connection: {response}", ANSI_COLOR_ERROR)
                return False
        except Exception as e:
            self.print_status_message(f"[ERROR] Failed to connect: {e}", ANSI_COLOR_ERROR)
            return False

    def register_username(self) -> bool:
        """Register username with server"""
        while True:
            username = input(f"{ANSI_COLOR_USERNAME}Enter username (max {MAX_USERNAME_LENGTH} chars, alphanumeric): {ANSI_COLOR_RESET}")
            
            # Validate username
            if not username or len(username) > MAX_USERNAME_LENGTH:
                self.print_status_message(f"[ERROR] Username must be 1-{MAX_USERNAME_LENGTH} characters", ANSI_COLOR_ERROR)
                continue
                
            if not username.isalnum():
                self.print_status_message("[ERROR] Username must be alphanumeric only", ANSI_COLOR_ERROR)
                continue
                
            try:
                # Send username command to server
                username_cmd = f"/username {username}"
                self.socket_send(username_cmd.encode('utf-8'))
                
                # Wait for server response
                response = self.socket.recv(BUFFER_SIZE).decode('utf-8')
                
                if response == "SET_USERNAME":
                    self.username = username
                    self.print_status_message(f"[SUCCESS] Username '{username}' registered successfully!", ANSI_COLOR_SUCCESS)
                    return True
                elif response == "ALREADY_TAKEN":
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
                self.handle_server_message(message)
                    
            except socket.timeout:
                continue
            except Exception as e:
                if self.is_running:
                    self.print_status_message(f"[ERROR] Connection lost: {e}", ANSI_COLOR_ERROR)
                break
                
        self.is_running = False

    def handle_server_message(self, message: str):
        """Handle incoming server messages"""
        self.clear_prompt()
        
        if message.startswith("[WHISPER from"):
            print(f"{ANSI_COLOR_PRIVATE}{message}{ANSI_COLOR_RESET}")
        elif message.startswith("[BROADCAST]"):
            print(f"{ANSI_COLOR_BROADCAST}{message}{ANSI_COLOR_RESET}")
        elif message.startswith("INCOMING_FILE"):
            parts = message.split()
            if len(parts) >= 4:
                sender = parts[1]
                filename = parts[2]
                filesize = parts[3]
                print(f"{ANSI_COLOR_WARNING}[FILE REQUEST] {sender} wants to send you: {ANSI_COLOR_FILENAME}{filename}{ANSI_COLOR_RESET} ({filesize} bytes)")
                response = input(f"{ANSI_COLOR_WARNING}Accept file? (y/n): {ANSI_COLOR_RESET}")
                if response.lower() in ['y', 'yes']:
                    self.receive_file(sender, filename, int(filesize))
        elif message.startswith("READY_FOR_FILE"):
            print(f"{ANSI_COLOR_INFO}[FILE] Ready to send file...{ANSI_COLOR_RESET}")
        elif message.startswith("FILE_TRANSFER_SUCCESS"):
            print(f"{ANSI_COLOR_SUCCESS}[FILE] Transfer completed successfully{ANSI_COLOR_RESET}")
        elif message.startswith("FILE_TRANSFER_FAILED"):
            print(f"{ANSI_COLOR_ERROR}[FILE] Transfer failed{ANSI_COLOR_RESET}")
        elif message.startswith("ROOM_LEFT"):
            self.current_room = ""
            print(f"{ANSI_COLOR_SUCCESS}[SERVER] Left the room{ANSI_COLOR_RESET}")
        elif message.startswith("[SERVER]"):
            if "joined" in message and "room" in message:
                # Extract room name from server message
                if "'" in message:
                    room_start = message.find("'") + 1
                    room_end = message.find("'", room_start)
                    if room_end > room_start:
                        self.current_room = message[room_start:room_end]
            print(f"{ANSI_COLOR_SUCCESS}{message}{ANSI_COLOR_RESET}")
        else:
            print(f"{ANSI_COLOR_INFO}{message}{ANSI_COLOR_RESET}")
            
        self.show_prompt()

    def receive_file(self, sender: str, filename: str, filesize: int):
        """Receive file from sender"""
        try:
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
            
            print(f"{ANSI_COLOR_INFO}[FILE] Receiving {filename} ({filesize} bytes)...{ANSI_COLOR_RESET}")
            
            # Receive file data (simulated - in real implementation would receive actual bytes)
            time.sleep(0.5)  # Simulate transfer time
            
            # Create a dummy file for demonstration
            with open(filepath, 'w') as f:
                f.write(f"File {filename} received from {sender}\n")
                f.write(f"Original size: {filesize} bytes\n")
                f.write("This is a simulated file transfer.\n")
            
            print(f"{ANSI_COLOR_SUCCESS}[FILE] File saved as: {filepath}{ANSI_COLOR_RESET}")
                    
        except Exception as e:
            print(f"{ANSI_COLOR_ERROR}[ERROR] File receive failed: {e}{ANSI_COLOR_RESET}")

    def send_file(self, filename: str, recipient: str):
        """Send file to recipient"""
        try:
            if not os.path.exists(filename):
                self.print_status_message(f"[ERROR] File not found: {filename}", ANSI_COLOR_ERROR)
                return
                
            file_size = os.path.getsize(filename)
            if file_size > MAX_FILE_SIZE:
                self.print_status_message(f"[ERROR] File too large (max {MAX_FILE_SIZE} bytes)", ANSI_COLOR_ERROR)
                return
            
            # Check file extension
            valid_extensions = ['.txt', '.pdf', '.jpg', '.png']
            if not any(filename.lower().endswith(ext) for ext in valid_extensions):
                self.print_status_message("[ERROR] Invalid file type. Allowed: .txt, .pdf, .jpg, .png", ANSI_COLOR_ERROR)
                return
                
            basename = os.path.basename(filename)
            
            # Send file transfer command
            cmd = f"/sendfile {recipient} {basename}"
            self.socket_send(cmd.encode('utf-8'))
            
            # Send file size
            self.socket_send(str(file_size).encode('utf-8'))
            
            self.print_status_message(f"[FILE] Sending {basename} to {recipient}...", ANSI_COLOR_INFO)
                
        except Exception as e:
            self.print_status_message(f"[ERROR] File send failed: {e}", ANSI_COLOR_ERROR)

    def process_command(self, command: str):
        """Process user commands"""
        try:
            parts = command.split(' ', 1)
            cmd = parts[0]
            args = parts[1] if len(parts) > 1 else ""
            
            if cmd == '/help':
                self.print_help_menu()
                
            elif cmd == '/join':
                if not args:
                    self.print_status_message("[ERROR] Usage: /join <room_name>", ANSI_COLOR_ERROR)
                else:
                    # Validate room name
                    if not args.isalnum() or len(args) > 32:
                        self.print_status_message("[ERROR] Room name must be alphanumeric, max 32 chars", ANSI_COLOR_ERROR)
                        return
                    self.socket_send(command.encode('utf-8'))
                    
            elif cmd == '/leave':
                self.socket_send(command.encode('utf-8'))
                
            elif cmd == '/broadcast':
                if not args:
                    self.print_status_message("[ERROR] Usage: /broadcast <message>", ANSI_COLOR_ERROR)
                elif not self.current_room:
                    self.print_status_message("[ERROR] You must join a room first", ANSI_COLOR_ERROR)
                else:
                    self.socket_send(command.encode('utf-8'))
                    
            elif cmd == '/whisper':
                whisper_parts = args.split(' ', 1)
                if len(whisper_parts) < 2:
                    self.print_status_message("[ERROR] Usage: /whisper <username> <message>", ANSI_COLOR_ERROR)
                else:
                    self.socket_send(command.encode('utf-8'))
                    
            elif cmd == '/sendfile':
                file_parts = args.split(' ', 1)
                if len(file_parts) < 2:
                    self.print_status_message("[ERROR] Usage: /sendfile <filename> <username>", ANSI_COLOR_ERROR)
                else:
                    filename, recipient = file_parts
                    self.send_file(filename, recipient)
                    
            elif cmd == '/list':
                if not self.current_room:
                    self.print_status_message("[ERROR] You must join a room first", ANSI_COLOR_ERROR)
                else:
                    self.socket_send(command.encode('utf-8'))
                    
            elif cmd == '/exit':
                self.socket_send(command.encode('utf-8'))
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
                        # Invalid input - must use commands
                        self.print_status_message("[ERROR] All messages must use commands. Type /help for available commands.", ANSI_COLOR_ERROR)
                        
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