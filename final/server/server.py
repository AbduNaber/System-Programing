#!/usr/bin/env python3
"""
Chat Server Implementation
Compile/Run: python3 chatserver.py <port>
"""

import socket
import threading
import time
import sys
import signal
import os
import logging
from datetime import datetime
from queue import Queue, Full
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
import re

# Constants from chatDefination.h
BUFFER_SIZE = 1024
MAX_CLIENTS = 15
MAX_USERNAME_LENGTH = 16
MAX_GROUP_NAME_LENGTH = 32
MAX_GROUPS = 15
MAX_GROUP_MEMBERS = 15  # Changed to match MAX_CLIENTS
MAX_SIMULTANEOUS_TRANSFERS = 5
MAX_FILE_QUEUE = 5
MAX_FILE_SIZE = 3 * 1024 * 1024  # 3 MB
LOG_FILE = "server.log"

@dataclass
class ClientInfo:
    socket: socket.socket
    username: str = ""
    current_room: str = ""
    active: bool = True
    address: Tuple[str, int] = None

@dataclass
class Room:
    name: str
    members: List[int]
    member_count: int = 0

@dataclass
class FileMeta:
    sender: str
    recipient: str
    filename: str
    filesize: int
    sender_socket: socket.socket
    recipient_socket: socket.socket
    enqueue_time: float = 0
    start_time: float = 0

class FileQueue:
    def __init__(self):
        self.queue = Queue(maxsize=MAX_FILE_QUEUE)
        self.active_transfers = 0
        self.lock = threading.Lock()

    def enqueue(self, meta: FileMeta) -> bool:
        try:
            with self.lock:
                if self.queue.full():
                    return False
                meta.enqueue_time = time.time()
                self.queue.put(meta)
                return True
        except Full:
            return False

    def start_transfer(self, meta: FileMeta) -> bool:
        with self.lock:
            if self.active_transfers < MAX_SIMULTANEOUS_TRANSFERS:
                self.active_transfers += 1
                return True
            else:
                # Queue for later
                if self.enqueue(meta):
                    return False
                return False

    def finish_transfer(self):
        with self.lock:
            self.active_transfers -= 1

    def try_start_next(self) -> Optional[FileMeta]:
        with self.lock:
            if not self.queue.empty() and self.active_transfers < MAX_SIMULTANEOUS_TRANSFERS:
                meta = self.queue.get()
                self.active_transfers += 1
                return meta
            return None

class ChatServer:
    def __init__(self, port: int):
        self.port = port
        self.running = True
        self.server_socket = None
        self.clients: Dict[int, ClientInfo] = {}
        self.rooms: Dict[str, Room] = {}
        self.clients_lock = threading.Lock()
        self.rooms_lock = threading.Lock()
        self.file_queue = FileQueue()
        
        # Setup logging
        logging.basicConfig(
            filename=LOG_FILE,
            level=logging.INFO,
            format='%(asctime)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        
        # Setup signal handlers
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)

    def log_event(self, message: str, *args):
        """Log events to file"""
        if args:
            message = message % args
        logging.info(message)
        print(f"[LOG] {message}")  # Also print to console for debugging
        
    def signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        signal_name = "SIGINT" if signum == signal.SIGINT else "SIGTERM"
        self.log_event("[SHUTDOWN] %s received. Disconnecting clients", signal_name)
        print("\nServer shutting down...")
        
        with self.clients_lock:
            for client_id, client in list(self.clients.items()):
                if client.active:
                    try:
                        client.socket.send(b"[SERVER] Server is shutting down. Disconnecting...\n")
                        client.socket.shutdown(socket.SHUT_RDWR)
                        client.socket.close()
                    except:
                        pass
                    client.active = False
        
        self.running = False
        if self.server_socket:
            self.server_socket.close()

    def start(self):
        """Start the server"""
        self.log_event("[STARTUP] Chat server starting up")
        self.log_event("[STARTUP] Server port set to %d", self.port)
        
        # Create socket
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.server_socket.bind(('', self.port))
            self.server_socket.listen(MAX_CLIENTS)
            self.log_event("[STARTUP] Server listening on port %d", self.port)
            print(f"[INFO] Server listening on port {self.port}...")
        except Exception as e:
            self.log_event("[ERROR] Failed to bind to port %d: %s", self.port, str(e))
            print(f"Failed to bind to port {self.port}: {e}")
            sys.exit(1)
        
        # Accept connections
        while self.running:
            try:
                client_socket, client_addr = self.server_socket.accept()
                self.handle_new_connection(client_socket, client_addr)
            except OSError:
                if self.running:
                    self.log_event("[ERROR] Accept failed")
                break
        
        self.log_event("[SHUTDOWN] Server shutdown complete")

    def handle_new_connection(self, client_socket: socket.socket, client_addr: Tuple[str, int]):
        """Handle new client connection"""
        client_ip, client_port = client_addr
        self.log_event("[CONNECTION] New connection from %s:%d", client_ip, client_port)
        
        # Find available slot
        with self.clients_lock:
            client_id = None
            for i in range(MAX_CLIENTS):
                if i not in self.clients or not self.clients[i].active:
                    client_id = i
                    self.clients[i] = ClientInfo(
                        socket=client_socket,
                        active=True,
                        address=client_addr
                    )
                    break
        
        if client_id is None:
            self.log_event("[CONNECTION_REJECTED] Max clients reached, rejecting %s:%d", 
                          client_ip, client_port)
            print(f"[CONNECT] Max clients reached. Rejecting connection from {client_ip}:{client_port}")
            client_socket.send(b"Server full. Try again later.\n")
            client_socket.close()
            return
        
        print(f"[CONNECT] Client connected: user=unnamed from {client_ip}:{client_port}")
        self.log_event("[CONNECTION_ACCEPTED] Client assigned to slot %d from %s:%d", 
                      client_id, client_ip, client_port)
        client_socket.send(b"SUCCESS_LOGIN")
        
        # Start client handler thread
        client_thread = threading.Thread(target=self.handle_client, args=(client_id,))
        client_thread.daemon = True
        client_thread.start()

    def handle_client(self, client_id: int):
        """Handle client messages"""
        client = self.clients[client_id]
        
        while client.active and self.running:
            try:
                data = client.socket.recv(BUFFER_SIZE)
                if not data:
                    break
                
                message = data.decode('utf-8').strip()
                if not message:
                    continue
                
                self.log_event("[MESSAGE_RECEIVED] Client %d (%s): %s [%d bytes]",
                              client_id, client.username or "unnamed", message, len(data))
                print(f"[COMMAND] {client.username or 'unnamed'} {message}")
                
                if message.startswith('/'):
                    self.log_event("[COMMAND] Processing command from client %d: %s", 
                                  client_id, message)
                    self.handle_command(client_id, message)
                elif message.startswith("FILE_EXISTS"):
                    self.log_event("[FILE] Conflict: '%s' received twice -> renamed by client", 
                                  message[12:])
                elif message.isdigit():
                    # This might be file size data during file transfer
                    pass
                else:
                    # Unknown command
                    response = f"[SERVER] Unknown command: '{message}'. Type /help for available commands."
                    client.socket.send(response.encode())
                    self.log_event("[UNKNOWN_COMMAND] Client %d sent invalid command: %s",
                                  client_id, message)
                    
            except Exception as e:
                self.log_event("[ERROR] Client %d error: %s", client_id, str(e))
                break
        
        # Client disconnected
        self.log_event("[DISCONNECT] Client %d (%s) disconnected",
                      client_id, client.username or "unnamed")
        print(f"[DISCONNECT] Client {client.username or 'unnamed'} disconnected.")
        
        with self.clients_lock:
            self.remove_client_from_room(client_id)
            client.active = False
            try:
                client.socket.close()
            except:
                pass
            if client_id in self.clients:
                del self.clients[client_id]

    def handle_command(self, client_id: int, message: str):
        """Handle client commands"""
        parts = message.split(' ', 1)
        cmd = parts[0]
        args = parts[1] if len(parts) > 1 else ""
        
        client = self.clients[client_id]
        
        if cmd == "/username":
            self.handle_username(client_id, args)
        elif cmd == "/join":
            self.handle_join(client_id, args)
        elif cmd == "/broadcast":
            self.handle_broadcast(client_id, args)
        elif cmd == "/leave":
            self.handle_leave(client_id)
        elif cmd == "/whisper":
            self.handle_whisper(client_id, args)
        elif cmd == "/sendfile":
            self.handle_sendfile(client_id, args)
        elif cmd == "/list":
            self.handle_list(client_id)
        elif cmd == "/exit":
            client.socket.send(b"[SERVER] Goodbye!")
            client.active = False
            self.log_event("[EXIT] Client %d (%s) disconnected voluntarily",
                          client_id, client.username)
        else:
            response = "[SERVER] Unknown command. Available commands: /username, /join, /broadcast, /whisper, /sendfile, /list, /exit"
            client.socket.send(response.encode())

    def handle_username(self, client_id: int, username: str):
        """Handle username setting"""
        if not username:
            self.clients[client_id].socket.send(b"[SERVER] Username cannot be empty")
            return
            
        if len(username) > MAX_USERNAME_LENGTH:
            self.clients[client_id].socket.send(b"[SERVER] Username too long")
            return
            
        # Check if username is alphanumeric
        if not re.match("^[a-zA-Z0-9_]+$", username):
            self.clients[client_id].socket.send(b"[SERVER] Username must be alphanumeric")
            return
            
        # Check if username is already taken
        with self.clients_lock:
            for other_id, other_client in self.clients.items():
                if other_id != client_id and other_client.username == username:
                    self.clients[client_id].socket.send(b"ALREADY_TAKEN")
                    self.log_event("[USERNAME_TAKEN] Client %d tried to use taken username: %s", 
                                  client_id, username)
                    return
            
            self.clients[client_id].username = username
            self.clients[client_id].socket.send(b"SET_USERNAME")
            self.log_event("[USERNAME_SET] Client %d set username to: %s", client_id, username)
            print(f"[USERNAME] Client {client_id} set username to: {username}")

    def handle_join(self, client_id: int, room_name: str):
        """Handle room joining"""
        if not room_name:
            self.clients[client_id].socket.send(b"[SERVER] Room name cannot be empty")
            return
            
        if not self.validate_room_name(room_name):
            self.clients[client_id].socket.send(b"[SERVER] Invalid room name. Use alphanumeric characters only (max 32 chars)")
            return
            
        client = self.clients[client_id]
        if not client.username:
            client.socket.send(b"[SERVER] Please set a username first using /username <name>")
            return
            
        with self.rooms_lock:
            # Remove from current room if any
            if client.current_room:
                self.remove_client_from_room(client_id)
            
            # Create room if it doesn't exist
            if room_name not in self.rooms:
                self.rooms[room_name] = Room(name=room_name, members=[])
                self.log_event("[ROOM_CREATED] Room '%s' created by client %d (%s)", 
                              room_name, client_id, client.username)
            
            room = self.rooms[room_name]
            
            # Check if room is full
            if len(room.members) >= MAX_GROUP_MEMBERS:
                client.socket.send(b"[SERVER] Room is full")
                return
            
            # Add client to room
            room.members.append(client_id)
            room.member_count = len(room.members)
            client.current_room = room_name
            
            # Notify client
            response = f"[SERVER] You joined room '{room_name}'"
            client.socket.send(response.encode())
            
            # Notify other room members
            join_msg = f"[SERVER] {client.username} joined the room"
            self.broadcast_to_room(join_msg, room_name, client_id)
            
            self.log_event("[ROOM_JOIN] Client %d (%s) joined room '%s'", 
                          client_id, client.username, room_name)
            print(f"[ROOM] {client.username} joined room '{room_name}'")

    def handle_broadcast(self, client_id: int, message: str):
        """Handle broadcast message"""
        client = self.clients[client_id]
        
        if not client.username:
            client.socket.send(b"[SERVER] Please set a username first")
            return
            
        if not client.current_room:
            client.socket.send(b"[SERVER] You must join a room first")
            return
            
        if not message:
            client.socket.send(b"[SERVER] Message cannot be empty")
            return
            
        broadcast_msg = f"[BROADCAST] {client.username}: {message}"
        self.broadcast_to_room(broadcast_msg, client.current_room, client_id)
        
        self.log_event("[BROADCAST] Client %d (%s) in room '%s': %s", 
                      client_id, client.username, client.current_room, message)

    def handle_leave(self, client_id: int):
        """Handle leaving current room"""
        client = self.clients[client_id]
        
        if not client.current_room:
            client.socket.send(b"[SERVER] You are not in any room")
            return
            
        room_name = client.current_room
        
        with self.rooms_lock:
            self.remove_client_from_room(client_id)
            
        client.socket.send(b"ROOM_LEFT")
        
        # Notify other room members
        leave_msg = f"[SERVER] {client.username} left the room"
        self.broadcast_to_room(leave_msg, room_name, client_id)
        
        self.log_event("[ROOM_LEAVE] Client %d (%s) left room '%s'", 
                      client_id, client.username, room_name)
        print(f"[ROOM] {client.username} left room '{room_name}'")

    def handle_whisper(self, client_id: int, args: str):
        """Handle private message"""
        parts = args.split(' ', 1)
        if len(parts) < 2:
            self.clients[client_id].socket.send(b"[SERVER] Usage: /whisper <username> <message>")
            return
            
        target_username, message = parts
        client = self.clients[client_id]
        
        if not client.username:
            client.socket.send(b"[SERVER] Please set a username first")
            return
            
        if target_username == client.username:
            client.socket.send(b"[SERVER] You cannot whisper to yourself")
            return
            
        # Find target client
        target_client_id = None
        with self.clients_lock:
            for other_id, other_client in self.clients.items():
                if other_client.username == target_username and other_client.active:
                    target_client_id = other_id
                    break
        
        if target_client_id is None:
            client.socket.send(f"[SERVER] User '{target_username}' not found or offline".encode())
            return
            
        # Send private message
        whisper_msg = f"[WHISPER from {client.username}] {message}"
        self.clients[target_client_id].socket.send(whisper_msg.encode())
        
        # Confirm to sender
        confirm_msg = f"[WHISPER to {target_username}] {message}"
        client.socket.send(confirm_msg.encode())
        
        self.log_event("[WHISPER] %s -> %s: %s", client.username, target_username, message)

    def handle_sendfile(self, client_id: int, args: str):
        """Handle file transfer request"""
        parts = args.split(' ', 2)
        if len(parts) < 3:
            self.clients[client_id].socket.send(b"[SERVER] Usage: /sendfile <username> <filename> <filesize>")
            return
            
        target_username, filename, filesize_str = parts
        
        try:
            filesize = int(filesize_str)
        except ValueError:
            self.clients[client_id].socket.send(b"[SERVER] Invalid file size")
            return
            
        if filesize > MAX_FILE_SIZE:
            self.clients[client_id].socket.send(f"[SERVER] File too large (max {MAX_FILE_SIZE} bytes)".encode())
            return
            
        client = self.clients[client_id]
        
        if not client.username:
            client.socket.send(b"[SERVER] Please set a username first")
            return
            
        # Find target client
        target_client_id = None
        with self.clients_lock:
            for other_id, other_client in self.clients.items():
                if other_client.username == target_username and other_client.active:
                    target_client_id = other_id
                    break
        
        if target_client_id is None:
            client.socket.send(f"[SERVER] User '{target_username}' not found or offline".encode())
            return
            
        # Create file meta
        file_meta = FileMeta(
            sender=client.username,
            recipient=target_username,
            filename=filename,
            filesize=filesize,
            sender_socket=client.socket,
            recipient_socket=self.clients[target_client_id].socket
        )
        
        # Start file transfer
        if self.file_queue.start_transfer(file_meta):
            self.handle_file_transfer(file_meta)
        else:
            client.socket.send(b"[SERVER] File transfer queue full. Try again later.")
            
        self.log_event("[FILE_TRANSFER] %s -> %s: %s (%d bytes)", 
                      client.username, target_username, filename, filesize)

    def handle_list(self, client_id: int):
        """Handle list users in current room"""
        client = self.clients[client_id]
        
        if not client.current_room:
            client.socket.send(b"[SERVER] You are not in any room")
            return
            
        with self.rooms_lock:
            room = self.rooms.get(client.current_room)
            if not room:
                client.socket.send(b"[SERVER] Room not found")
                return
                
            users = []
            for member_id in room.members:
                if member_id in self.clients and self.clients[member_id].active:
                    users.append(self.clients[member_id].username)
            
            if users:
                user_list = ", ".join(users)
                response = f"[SERVER] Users in room '{client.current_room}': {user_list}"
            else:
                response = f"[SERVER] No users in room '{client.current_room}'"
                
            client.socket.send(response.encode())

    def handle_file_transfer(self, file_meta: FileMeta):
        """Handle file transfer between clients"""
        try:
            # Notify recipient about incoming file
            incoming_msg = f"INCOMING_FILE {file_meta.sender} {file_meta.filename} {file_meta.filesize}"
            file_meta.recipient_socket.send(incoming_msg.encode())
            
            # Notify sender that recipient was notified
            file_meta.sender_socket.send(b"READY_FOR_FILE")
            
            # Relay file data
            self.relay_file_data(file_meta)
            
        except Exception as e:
            self.log_event("[FILE_TRANSFER_ERROR] %s", str(e))
            try:
                file_meta.sender_socket.send(b"FILE_TRANSFER_FAILED")
                file_meta.recipient_socket.send(b"FILE_TRANSFER_FAILED")
            except:
                pass
        finally:
            self.file_queue.finish_transfer()
            
            # Try to start next queued transfer
            next_meta = self.file_queue.try_start_next()
            if next_meta:
                transfer_thread = threading.Thread(target=self.handle_file_transfer, args=(next_meta,))
                transfer_thread.daemon = True
                transfer_thread.start()

    def relay_file_data(self, file_meta: FileMeta):
        """Relay file data from sender to recipient"""
        total_relayed = 0
        
        while total_relayed < file_meta.filesize:
            try:
                # Calculate remaining bytes
                remaining = file_meta.filesize - total_relayed
                chunk_size = min(BUFFER_SIZE, remaining)
                
                # Receive from sender
                data = file_meta.sender_socket.recv(chunk_size)
                if not data:
                    raise Exception("Sender disconnected during file transfer")
                
                # Send to recipient
                file_meta.recipient_socket.send(data)
                total_relayed += len(data)
                
            except Exception as e:
                self.log_event("[FILE_RELAY_ERROR] %s", str(e))
                raise
        
        # Notify both parties of successful transfer
        file_meta.sender_socket.send(b"FILE_TRANSFER_SUCCESS")
        file_meta.recipient_socket.send(b"FILE_TRANSFER_SUCCESS")
        
        self.log_event("[FILE_TRANSFER_SUCCESS] Relayed %d bytes from %s to %s", 
                      total_relayed, file_meta.sender, file_meta.recipient)

    def broadcast_to_room(self, message: str, room_name: str, sender_id: int):
        """Broadcast message to all members of a room except sender"""
        with self.rooms_lock:
            room = self.rooms.get(room_name)
            if not room:
                return
                
            for member_id in room.members:
                if member_id != sender_id and member_id in self.clients:
                    client = self.clients[member_id]
                    if client.active:
                        try:
                            client.socket.send(message.encode())
                        except:
                            # Client might have disconnected
                            pass

    def remove_client_from_room(self, client_id: int):
        """Remove client from their current room"""
        client = self.clients[client_id]
        if not client.current_room:
            return
            
        room_name = client.current_room
        if room_name in self.rooms:
            room = self.rooms[room_name]
            if client_id in room.members:
                room.members.remove(client_id)
                room.member_count = len(room.members)
                
                # Remove empty rooms
                if room.member_count == 0:
                    del self.rooms[room_name]
                    self.log_event("[ROOM_DELETED] Empty room '%s' deleted", room_name)
        
        client.current_room = ""

    def validate_room_name(self, room_name: str) -> bool:
        """Validate room name format"""
        if not room_name or len(room_name) > MAX_GROUP_NAME_LENGTH:
            return False
        return re.match("^[a-zA-Z0-9_]+$", room_name) is not None


def main():
    """Main function"""
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <port>")
        sys.exit(1)
        
    try:
        port = int(sys.argv[1])
        if port <= 0 or port > 65535:
            raise ValueError("Port must be between 1 and 65535")
            
        server = ChatServer(port)
        server.start()
        
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nServer interrupted by user")
        sys.exit(0)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()