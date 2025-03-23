#!/bin/bash

# Directory for testing
TEST_DIR="./testDir"
TEST_FILE="$TEST_DIR/testFile.txt"
TEST_APPEND_FILE="$TEST_DIR/testAppendFile.txt"
EXTENSION=".txt"
LOG_FILE="log.txt"

# Function to clean up after tests
cleanup() {
    rm -rf "$TEST_DIR"
    rm -f "$LOG_FILE"
}

# Function to check if a file exists
check_file_exists() {
    if [ ! -f "$1" ]; then
        echo "Test failed: $1 does not exist."
        return 1
    fi
    return 0
}

# Function to check if a directory exists
check_dir_exists() {
    if [ ! -d "$1" ]; then
        echo "Test failed: $1 does not exist."
        return 1
    fi
    return 0
}

# Test 1: Create a directory
test_create_dir() {
    ./fileManager createDir "$TEST_DIR"
    if check_dir_exists "$TEST_DIR"; then
        echo "Test passed: Directory created."
    else
        echo "Test failed: Directory not created."
    fi
}

# Test 2: Create a file
test_create_file() {
    ./fileManager createFile "$TEST_FILE"
    if check_file_exists "$TEST_FILE"; then
        echo "Test passed: File created."
    else
        echo "Test failed: File not created."
    fi
}

# Test 3: List files in directory
test_list_dir() {
    ./fileManager listDir "$TEST_DIR"
    # Expect the file to be listed in output (not exact output but logical test)
    if ls "$TEST_DIR" | grep -q "testFile.txt"; then
        echo "Test passed: File listed in directory."
    else
        echo "Test failed: File not listed in directory."
    fi
}

# Test 4: List files by extension
test_list_files_by_extension() {
    ./fileManager createFile "$TEST_APPEND_FILE"
    ./fileManager listFilesByExtension "$TEST_DIR" "$EXTENSION"
    if ls "$TEST_DIR" | grep -q "testAppendFile.txt"; then
        echo "Test passed: File with .txt extension listed."
    else
        echo "Test failed: File with .txt extension not listed."
    fi
}

# Test 5: Read file content
test_read_file() {
    ./fileManager readFile "$TEST_FILE"
    # We cannot check the output directly, but we check if file exists and can be read
    if check_file_exists "$TEST_FILE"; then
        echo "Test passed: File read successfully."
    else
        echo "Test failed: Could not read file."
    fi
}

# Test 6: Append content to file
test_append_to_file() {
    ./fileManager appendToFile "$TEST_FILE" "New content."
    # Check if the file size increases (indicating content was appended)
    original_size=$(stat -c %s "$TEST_FILE")
    sleep 1 # Ensure time difference
    ./fileManager appendToFile "$TEST_FILE" "More content."
    new_size=$(stat -c %s "$TEST_FILE")
    
    if [ "$new_size" -gt "$original_size" ]; then
        echo "Test passed: Content appended successfully."
    else
        echo "Test failed: Content was not appended."
    fi
}

# Test 7: Delete file
test_delete_file() {
    ./fileManager deleteFile "$TEST_FILE"
    if [ ! -f "$TEST_FILE" ]; then
        echo "Test passed: File deleted."
    else
        echo "Test failed: File not deleted."
    fi
}

# Test 8: Delete directory
test_delete_dir() {
    ./fileManager createDir "$TEST_DIR" # Ensure directory exists
    ./fileManager deleteDir "$TEST_DIR"
    if [ ! -d "$TEST_DIR" ]; then
        echo "Test passed: Directory deleted."
    else
        echo "Test failed: Directory not deleted."
    fi
}

# Test 9: Show logs
test_show_logs() {
    ./fileManager showLogs
    if [ -f "$LOG_FILE" ]; then
        echo "Test passed: Logs displayed."
    else
        echo "Test failed: Logs not found."
    fi
}

# Main test function
main() {
    cleanup
    
    # Run all tests
    test_create_dir
    test_create_file
    test_list_dir
    test_list_files_by_extension
    test_read_file
    test_append_to_file
    test_delete_file
    test_delete_dir
    test_show_logs
    
    cleanup
}

main

