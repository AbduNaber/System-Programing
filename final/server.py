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
import struct

# Constants from chatDefination.h
BUFFER_SIZE = 1024
MAX_CLIENTS = 15
MAX_USERNAME_LENGTH = 16
MAX_GROUP_NAME_LENGTH = 32
MAX_GROUPS = 15
MAX_GROUP_MEMBERS = 10
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
        self.not_empty = threading.Condition(self.lock)
        self.not_full = threading.Condition(self.lock)

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
            self.not_full.notify()

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
            print(f"Server listening on port {self.port}...")
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
            print("Max clients reached. Rejecting connection.")
            client_socket.send(b"Server full. Try again later.\n")
            client_socket.close()
            return
        
        print(f"Client connected from {client_ip}:{client_port} (slot {client_id})")
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
                print(f"Client {client_id} ({client.username}): {message}")
                
                if message.startswith('/'):
                    self.log_event("[COMMAND] Processing command from client %d: %s", 
                                  client_id, message)
                    self.handle_command(client_id, message)
                elif message.startswith("FILE_EXISTS"):
                    self.log_event("[FILE] Conflict: '%s' received twice -> renamed by client", 
                                  message[12:])
                else:
                    # Unknown command
                    response = f"Unknown command: '{message}'. Type /help for available commands."
                    client.socket.send(response.encode())
                    self.log_event("[UNKNOWN_COMMAND] Client %d sent invalid command: %s",
                                  client_id, message)
                    
            except Exception as e:
                self.log_event("[ERROR] Client %d error: %s", client_id, str(e))
                break
        
        # Client disconnected
        self.log_event("[DISCONNECT] Client %d (%s) disconnected",
                      client_id, client.username or "unnamed")
        print(f"Client {client_id} disconnected")
        
        with self.clients_lock:
            self.remove_client_from_room(client_id)
            client.active = False
            try:
                client.socket.close()
            except:
                pass
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
            self.log_event("[UNKNOWN_COMMAND] Client %d sent unrecognized command: %s",
                          client_id, cmd)

    def handle_username(self, client_id: int, username: str):
        """Handle username setting"""
        client = self.clients[client_id]
        
        if not username:
            client.socket.send(b"[SERVER] Usage: /username <name>")
            return
        
        username = username.strip()
        
        with self.clients_lock:
            # Check if username already taken
            for cid, c in self.clients.items():
                if c.username == username and cid != client_id:
                    client.socket.send(b"ALREADY_TAKEN")
                    self.log_event("[USERNAME_TAKEN] Client %d tried to use taken username: %s",
                                  client_id, username)
                    return
            
            old_username = client.username
            client.username = username[:MAX_USERNAME_LENGTH-1]
            client.socket.send(b"SET_USERNAME")
            self.log_event("[USERNAME_SET] Client %d changed username from '%s' to '%s'",
                          client_id, old_username or "unnamed", username)

    def handle_join(self, client_id: int, room_name: str):
        """Handle room joining"""
        client = self.clients[client_id]
        
        if not room_name:
            client.socket.send(b"[SERVER] Usage: /join <room_name>")
            return
        
        room_name = room_name.strip()
        
        with self.clients_lock, self.rooms_lock:
            # Remove from current room
            old_room = client.current_room
            if old_room:
                self.remove_client_from_room(client_id)
                self.log_event("[ROOM_LEAVE] Client %d (%s) left room '%s'",
                              client_id, client.username, old_room)
            
            # Add to new room
            self.add_client_to_room(client_id, room_name)
            client.current_room = room_name
            response = f"[SERVER] Joined room '{room_name}'"
            client.socket.send(response.encode())
            self.log_event("[ROOM_JOIN] Client %d (%s) joined room '%s'",
                          client_id, client.username, room_name)

    def handle_broadcast(self, client_id: int, message: str):
        """Handle broadcast messages"""
        client = self.clients[client_id]
        
        if not client.current_room:
            client.socket.send(b"[SERVER] You must join a room first")
            self.log_event("[BROADCAST_ERROR] Client %d tried to broadcast without joining room",
                          client_id)
            return
        
        formatted_msg = f"[BROADCAST] {client.username}: {message}"
        self.log_event("[BROADCAST_START] Client %d broadcasting to room '%s': %s",
                      client_id, client.current_room, message)
        
        self.broadcast_to_room(formatted_msg, client.current_room, client.socket)
        client.socket.send(b"[SERVER] Message broadcasted")

    def handle_leave(self, client_id: int):
        """Handle leaving room"""
        client = self.clients[client_id]
        
        with self.clients_lock, self.rooms_lock:
            if client.current_room:
                old_room = client.current_room
                self.remove_client_from_room(client_id)
                client.socket.send(b"ROOM_LEFT")
                self.log_event("[ROOM_LEAVE] Client %d (%s) left room '%s'",
                              client_id, client.username, old_room)
            else:
                client.socket.send(b"[SERVER] You are not in a room")

    def handle_whisper(self, client_id: int, args: str):
        """Handle private messages"""
        client = self.clients[client_id]
        parts = args.split(' ', 1)
        
        if len(parts) < 2:
            client.socket.send(b"[SERVER] Usage: /whisper <username> <message>")
            return
        
        target_username, message = parts
        formatted_msg = f"[WHISPER from {client.username}]: {message}"
        
        self.log_event("[WHISPER_START] Client %d (%s) whispering to '%s': %s",
                      client_id, client.username, target_username, message)
        
        self.send_private_message(formatted_msg, target_username, client.socket)
        response = f"[SERVER] Whisper sent to {target_username}"
        client.socket.send(response.encode())

    def handle_sendfile(self, client_id: int, args: str):
        """Handle file transfer initiation"""
        client = self.clients[client_id]
        parts = args.split()
        
        if len(parts) < 2:
            client.socket.send(b"[SERVER] Usage: /sendfile <recipient> <filename>\n")
            return
        
        recipient = parts[0]
        filename = ' '.join(parts[1:])
        
        self.log_event("[FILE_TRANSFER_START] Client %d (%s) initiating file transfer to '%s', file: %s",
                      client_id, client.username, recipient, filename)
        
        # Validate file type
        valid_extensions = ['.txt', '.pdf', '.jpg', '.png']
        if not any(filename.lower().endswith(ext) for ext in valid_extensions):
            client.socket.send(b"INVALID_FILE_TYPE")
            self.log_event("[FILE_TRANSFER_ERROR] Invalid file type '%s' from %s",
                          filename, client.username)
            return
        
        # Find recipient
        recipient_id = None
        with self.clients_lock:
            for cid, c in self.clients.items():
                if c.username == recipient and c.active:
                    recipient_id = cid
                    break
        
        if recipient_id is None:
            client.socket.send(b"[SERVER] Recipient not found.\n")
            self.log_event("[FILE_TRANSFER_ERROR] Recipient '%s' not found", recipient)
            return
        
        # Receive file size
        try:
            size_data = client.socket.recv(64)
            filesize = int(size_data.decode().strip())
            
            self.log_event("[FILE_TRANSFER] File metadata received - size: %d bytes", filesize)
            
            if filesize > MAX_FILE_SIZE:
                client.socket.send(b"FILE_SIZE_EXCEEDS_LIMIT")
                self.log_event("[FILE_TRANSFER_ERROR] File size %d exceeds limit", filesize)
                return
            
            # Create file metadata
            file_meta = FileMeta(
                sender=client.username,
                recipient=recipient,
                filename=filename,
                filesize=filesize,
                sender_socket=client.socket,
                recipient_socket=self.clients[recipient_id].socket
            )
            
            # Try to start transfer
            if self.file_queue.start_transfer(file_meta):
                # Can start immediately
                self.log_event("[FILE_TRANSFER] Starting immediate transfer: %s -> %s",
                              client.username, recipient)
                print(f"[FILE_TRANSFER] Starting immediate transfer: {client.username} -> {recipient}")
                
                client.socket.send(b"READY_FOR_FILE")
                
                # Inform recipient
                filemeta_msg = f"INCOMING_FILE {client.username} {filename} {filesize}\n"
                self.clients[recipient_id].socket.send(filemeta_msg.encode())
                
                # Start transfer thread
                transfer_thread = threading.Thread(
                    target=self.handle_file_transfer,
                    args=(file_meta,)
                )
                transfer_thread.daemon = True
                transfer_thread.start()
            else:
                # Queued
                client.socket.send(b"[SERVER] File transfer queued. Please wait for your turn.\n")
                self.log_event("[FILE-QUEUE] Upload '%s' from %s added to queue",
                              filename, client.username)
                
        except Exception as e:
            self.log_event("[FILE_TRANSFER_ERROR] Error processing file transfer: %s", str(e))
            client.socket.send(b"[SERVER] File transfer failed.\n")

    def handle_file_transfer(self, meta: FileMeta):
        """Handle the actual file transfer"""
        meta.start_time = time.time()
        wait_duration = meta.start_time - meta.enqueue_time if meta.enqueue_time else 0
        
        self.log_event("[FILE_TRANSFER] Processing transfer: %s -> %s (%s, %d bytes) after %d seconds",
                      meta.sender, meta.recipient, meta.filename, meta.filesize, int(wait_duration))
        
        # Simulate file transfer (relay_file equivalent)
        result = self.relay_file()
        
        if result == 0:
            meta.sender_socket.send(b"FILE_TRANSFER_SUCCESS")
            meta.sender_socket.send(b"[SERVER] File sent successfully.\n")
            meta.recipient_socket.send(b"FILE_TRANSFER_SUCCESS")
            meta.recipient_socket.send(b"[SERVER] File received successfully.\n")
            self.log_event("[SEND FILE] '%s' sent from %s to %s (success)",
                          meta.filename, meta.sender, meta.recipient)
        else:
            meta.sender_socket.send(b"FILE_TRANSFER_FAILED")
            meta.recipient_socket.send(b"FILE_TRANSFER_FAILED")
            self.log_event("[SEND FILE] '%s' from %s to %s (failed)",
                          meta.filename, meta.sender, meta.recipient)
        
        # Mark transfer finished
        self.file_queue.finish_transfer()
        
        # Try next queued transfer
        next_meta = self.file_queue.try_start_next()
        if next_meta:
            self.log_event("[FILE_TRANSFER] Starting next queued transfer: %s -> %s",
                          next_meta.sender, next_meta.recipient)
            
            # Inform recipient
            filemeta_msg = f"INCOMING_FILE {next_meta.sender} {next_meta.filename} {next_meta.filesize}\n"
            next_meta.recipient_socket.send(filemeta_msg.encode())
            
            # Start next transfer
            transfer_thread = threading.Thread(
                target=self.handle_file_transfer,
                args=(next_meta,)
            )
            transfer_thread.daemon = True
            transfer_thread.start()

    def relay_file(self) -> int:
        """Simulate file relay (placeholder)"""
        self.log_event("[FILE_RELAY] File transfer initiated")
        # In real implementation, this would handle actual file data transfer
        time.sleep(0.1)  # Simulate transfer time
        return 0  # Success

    def handle_list(self, client_id: int):
        """List users in current room"""
        client = self.clients[client_id]
        
        if not client.current_room:
            client.socket.send(b"[SERVER] You must join a room first")
            return
        
        with self.clients_lock, self.rooms_lock:
            if client.current_room in self.rooms:
                room = self.rooms[client.current_room]
                users = []
                for member_id in room.members:
                    if member_id in self.clients and self.clients[member_id].active:
                        users.append(self.clients[member_id].username)
                
                response = "[SERVER] Users in room: " + " ".join(users)
                client.socket.send(response.encode())
                self.log_event("[LIST_RESULT] Room '%s' has %d active users",
                              client.current_room, len(users))

    def broadcast_to_room(self, message: str, room_name: str, sender_socket: socket.socket):
        """Broadcast message to all users in room except sender"""
        with self.rooms_lock:
            if room_name not in self.rooms:
                return
            
            room = self.rooms[room_name]
            messages_sent = 0
            
            for member_id in room.members:
                if member_id in self.clients:
                    member = self.clients[member_id]
                    if member.active and member.socket != sender_socket:
                        try:
                            member.socket.send(message.encode())
                            messages_sent += 1
                            self.log_event("[BROADCAST_DELIVERY] Message delivered to client %d (%s)",
                                          member_id, member.username)
                        except:
                            pass
            
            self.log_event("[BROADCAST_SUMMARY] Broadcast in room '%s' delivered to %d clients",
                          room_name, messages_sent)

    def send_private_message(self, message: str, target_username: str, sender_socket: socket.socket):
        """Send private message to specific user"""
        with self.clients_lock:
            target_found = False
            for client_id, client in self.clients.items():
                if client.username == target_username and client.active:
                    try:
                        client.socket.send(message.encode())
                        self.log_event("[WHISPER_DELIVERY] Private message delivered to %s",
                                      target_username)
                        target_found = True
                        break
                    except:
                        pass
            
            if not target_found:
                error_msg = f"User '{target_username}' not found or offline"
                sender_socket.send(error_msg.encode())
                self.log_event("[WHISPER_ERROR] Target user '%s' not found or offline",
                              target_username)

    def add_client_to_room(self, client_id: int, room_name: str):
        """Add client to room"""
        if room_name not in self.rooms:
            self.rooms[room_name] = Room(name=room_name, members=[])
            self.log_event("[ROOM_CREATED] New room '%s' created", room_name)
        
        room = self.rooms[room_name]
        if client_id not in room.members:
            room.members.append(client_id)
            room.member_count = len(room.members)
            self.log_event("[ROOM_ADD] Client %d added to room '%s', %d members total",
                          client_id, room_name, room.member_count)

    def remove_client_from_room(self, client_id: int):
        """Remove client from their current room"""
        client = self.clients.get(client_id)
        if not client or not client.current_room:
            return
        
        room_name = client.current_room
        if room_name in self.rooms:
            room = self.rooms[room_name]
            if client_id in room.members:
                room.members.remove(client_id)
                room.member_count = len(room.members)
                self.log_event("[ROOM_REMOVE] Client %d removed from room '%s', %d members remaining",
                              client_id, room_name, room.member_count)
                
                # Remove empty room
                if room.member_count == 0:
                    del self.rooms[room_name]
                    self.log_event("[ROOM_DELETE] Empty room '%s' deleted", room_name)
        
        client.current_room = ""

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <port>")
        sys.exit(1)
    
    try:
        port = int(sys.argv[1])
    except ValueError:
        print("Error: Port must be a number")
        sys.exit(1)
    
    server = ChatServer(port)
    server.start()

if __name__ == "__main__":
    main()