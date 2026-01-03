#!/usr/bin/env python3

import json
import urllib.request
from datetime import datetime
import os
import sys
import time

FUNCTION="watt_monitor_v1"

def load_config():
    """スクリプトと同じディレクトリにある param.json を読み込む"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, 'param.json')
    
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"Error: {config_path} が見つかりません。", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: param.json の形式が不正です: {e}", file=sys.stderr)
        sys.exit(1)

def fetch_device_power(device):
    """1台のデバイスから avg 値を取得"""
    ipaddr = device['ipaddr']
    num_channels = device['num_channels']
    url = f"http://{ipaddr}/api/power"
    
    try:
        with urllib.request.urlopen(url, timeout=5) as response:
            data = json.loads(response.read().decode('utf-8'))
            avg = data.get('avg', [])
            # 指定チャンネル数に制限
            return avg[:num_channels]
    except Exception as e:
        print(f"Error fetching {device['name']} ({ipaddr}): {e}")
        return [0.0] * num_channels

def post_to_gas(post_url, post_data):
    """GAS に JSON を POST"""
    data_bytes = json.dumps(post_data).encode('utf-8')
    
    try:
        req = urllib.request.Request(
            post_url,
            data=data_bytes,
            headers={'Content-Type': 'application/json'},
            method='POST'
        )
        with urllib.request.urlopen(req, timeout=10) as response:
            print(post_data)
            response_text = response.read().decode('utf-8')
            print('「' + response_text + '」')
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
    channel_names = []  # ヘッダ用: "device1(1)", "device2(1)" など

    for device in devices:
        avg = fetch_device_power(device)
        avg_powers.extend(avg)
        
        for ch in range(len(avg)):  # 実際のavg長さに基づく（安全）
            channel_names.append(f"{device['name']}({ch+1})")

    # 合計電力も追加（ヘッダにも反映）
    total_power = sum(avg_powers)
    avg_powers.append(total_power)
    channel_names.append("total")

    post_data = {
        "function": FUNCTION,
        "time": current_time,
        "power": avg_powers,
        "names": channel_names   # ヘッダ用の名前リスト
    }
    post_to_gas(post_url, post_data)

if __name__ == '__main__':
    main()