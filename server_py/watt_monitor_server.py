#!/usr/bin/env python3

import json
import urllib.request
from datetime import datetime
import os
import sys

FUNCTION = "watt_monitor_v1"

def load_config():
    """Load param.json located in the same directory as this script"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, 'param.json')
    
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"Error: {config_path} not found.", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid format in param.json: {e}", file=sys.stderr)
        sys.exit(1)

def fetch_device_power(device):
    """Retrieve avg values from one device"""
    ipaddr = device['ipaddr']
    num_channels = device['num_channels']
    url = f"http://{ipaddr}/api/power"
    
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            data = json.loads(response.read().decode('utf-8'))
            avg = data.get('avg', [])
            # Limit to the specified number of channels
            return avg[:num_channels]
    except Exception as e:
        print(f"Error fetching {device['name']} ({ipaddr}): {e}")
        return [0.0] * num_channels

def post_to_gas(post_url, post_data):
    """POST JSON data to Google Apps Script"""
    data_bytes = json.dumps(post_data).encode('utf-8')
    
    try:
        req = urllib.request.Request(
            post_url,
            data=data_bytes,
            headers={'Content-Type': 'application/json'},
            method='POST'
        )
        with urllib.request.urlopen(req, timeout=10) as response:
            response_text = response.read().decode('utf-8')
            result = json.loads(response_text)
            print(f"POST success: {result}")
    except Exception as e:
        print(f"POST error: {e}")

def main():
    config = load_config()
    devices = config['devices']
    post_url = config['post_url'].strip()

    current_time = datetime.now().strftime('%Y/%m/%d %H:%M')

    avg_powers = []
    channel_names = []  # For header: "device1(1)", "device2(1)", etc.

    for device in devices:
        avg = fetch_device_power(device)
        avg_powers.extend(avg)
        
        for ch in range(len(avg)):  # Based on actual avg length (safe)
            channel_names.append(f"{device['name']}({ch+1})")

    # Add total power (also reflected in header)
    total_power = sum(avg_powers)
    avg_powers.append(total_power)
    channel_names.append("total")

    post_data = {
        "function": FUNCTION,
        "time": current_time,
        "power": avg_powers,
        "names": channel_names   # List of names for header
    }
    post_to_gas(post_url, post_data)

if __name__ == '__main__':
    main()