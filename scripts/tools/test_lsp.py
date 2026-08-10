#!/usr/bin/env python3
"""Test script for Havel LSP server"""

import subprocess
import json
import sys
import select

def read_lsp_message(proc, timeout=5):
    """Read a complete LSP message from stdout"""
    content_length = None
    buffer = b''
    
    while True:
        ready = select.select([proc.stdout], [], [], timeout)
        if not ready[0]:
            return None
        
        chunk = proc.stdout.read(1)
        if not chunk:
            return None
        
        buffer += chunk
        
        # Check for end of headers
        if b'\r\n\r\n' in buffer:
            header_end = buffer.index(b'\r\n\r\n')
            headers = buffer[:header_end].decode('utf-8')
            
            for line in headers.split('\r\n'):
                if line.startswith('Content-Length:'):
                    content_length = int(line.split(':')[1].strip())
            
            body_start = header_end + 4
            body = buffer[body_start:]
            
            if content_length and len(body) >= content_length:
                return json.loads(body[:content_length].decode('utf-8'))

def send_request(proc, method, params, req_id=1):
    """Send JSON-RPC request to LSP server"""
    msg = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": method,
        "params": params
    }
    content = json.dumps(msg)
    header = f"Content-Length: {len(content)}\r\n\r\n"
    
    print(f"SEND: {method}", file=sys.stderr)
    print(f"  {content[:80]}...", file=sys.stderr)
    
    proc.stdin.write(header.encode())
    proc.stdin.write(content.encode())
    proc.stdin.flush()
    
    return read_lsp_message(proc)

def main():
    # Start LSP server
    proc = subprocess.Popen(
        ['./build-release/havel-lsp'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd='/home/all/repos/havel-wm/havel'
    )
    
    try:
        # 1. Initialize
        print("\n=== Test 1: Initialize ===", file=sys.stderr)
        resp = send_request(proc, "initialize", {
            "processId": None,
            "rootUri": None,
            "capabilities": {}
        }, 1)
        print(f"RESP: {json.dumps(resp, indent=2)[:500]}", file=sys.stderr)
        
        # 2. Initialized notification
        print("\n=== Test 2: Initialized ===", file=sys.stderr)
        send_request(proc, "initialized", {}, 2)
        
        # 3. Open a test document
        print("\n=== Test 3: DidOpen ===", file=sys.stderr)
        test_code = """// Test Havel file
fn main() {
    let x = 10
    let y = 20
    print(x + y)
}

struct Point {
    x: Int
    y: Int
}
"""
        send_request(proc, "textDocument/didOpen", {
            "textDocument": {
                "uri": "file:///test.hv",
                "languageId": "havel",
                "version": 1,
                "text": test_code
            }
        }, 3)
        
        # Wait for diagnostics
        import time
        time.sleep(0.5)
        
        # 4. Hover request
        print("\n=== Test 4: Hover ===", file=sys.stderr)
        resp = send_request(proc, "textDocument/hover", {
            "textDocument": {"uri": "file:///test.hv"},
            "position": {"line": 1, "character": 3}
        }, 4)
        print(f"RESP: {json.dumps(resp, indent=2)}", file=sys.stderr)
        
        # 5. Document symbols
        print("\n=== Test 5: Document Symbols ===", file=sys.stderr)
        resp = send_request(proc, "textDocument/documentSymbol", {
            "textDocument": {"uri": "file:///test.hv"}
        }, 5)
        print(f"RESP: {json.dumps(resp, indent=2)}", file=sys.stderr)
        
        # 6. Definition request
        print("\n=== Test 6: Definition ===", file=sys.stderr)
        resp = send_request(proc, "textDocument/definition", {
            "textDocument": {"uri": "file:///test.hv"},
            "position": {"line": 1, "character": 3}
        }, 6)
        print(f"RESP: {json.dumps(resp, indent=2)}", file=sys.stderr)
        
        # 7. Shutdown
        print("\n=== Test 7: Shutdown ===", file=sys.stderr)
        resp = send_request(proc, "shutdown", {}, 7)
        print(f"RESP: {json.dumps(resp, indent=2)}", file=sys.stderr)
        
        # 8. Exit
        print("\n=== Test 8: Exit ===", file=sys.stderr)
        send_request(proc, "exit", {}, 8)
        
        print("\n=== All tests completed ===", file=sys.stderr)
        
    finally:
        proc.terminate()
        proc.wait()

if __name__ == "__main__":
    main()
