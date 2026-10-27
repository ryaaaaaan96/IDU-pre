#!/usr/bin/env python3
"""同线模拟南向设备，并驱动北向 Modbus RTU 测试。"""
import argparse
import json
import struct
import time
from pathlib import Path


def crc16(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value


def frame(data):
    return data + struct.pack('<H', crc16(data))


def words(values):
    return struct.pack('>' + 'H' * len(values), *values)


class Slave:
    def __init__(self, config):
        self.online = config.get('online', True)
        self.holding = {int(k): v for k, v in config['holding'].items()}
        self.inputs = {int(k): v for k, v in config['input'].items()}
        self.requests = []

    def reply(self, data):
        unit, fc = data[:2]
        address, count = struct.unpack('>HH', data[2:6])
        self.requests.append((time.monotonic(), fc, address, count))
        if not self.online:
            return None
        try:
            if fc in (3, 4):
                if not 1 <= count <= 125:
                    raise ValueError
                table = self.holding if fc == 3 else self.inputs
                payload = words([table[address + i] for i in range(count)])
                return frame(bytes([unit, fc, len(payload)]) + payload)
            if fc == 6:
                if address not in self.holding:
                    raise KeyError
                self.holding[address] = count
                return frame(data)
            if fc == 16:
                if not 1 <= count <= 123 or data[6] != count * 2 or len(data) != 7 + count * 2:
                    raise ValueError
                if any(address + i not in self.holding for i in range(count)):
                    raise KeyError
                values = struct.unpack('>' + 'H' * count, data[7:])
                self.holding.update({address + i: v for i, v in enumerate(values)})
                return frame(data[:6])
            return frame(bytes([unit, fc | 0x80, 1]))
        except KeyError:
            return frame(bytes([unit, fc | 0x80, 2]))
        except (ValueError, struct.error):
            return frame(bytes([unit, fc | 0x80, 3]))


class Runner:
    def __init__(self, serial, config, log):
        self.serial, self.config, self.log = serial, config, log
        self.slaves = {int(k): Slave(v) for k, v in config['slaves'].items()}
        self.buffer = bytearray()
        self.last_byte = self.last_bus = time.monotonic()
        self.responses = []
        self.frames = []
        self.results = []

    def event(self, direction, data):
        record = {'time': time.time(), 'direction': direction, 'hex': data.hex(' ')}
        self.log.write(json.dumps(record) + '\n')
        self.log.flush()
        print(f"{direction}: {record['hex']}", flush=True)

    def send(self, data):
        self.serial.write(data)
        self.serial.flush()
        self.last_bus = time.monotonic()
        self.event('TX', data)

    def dispatch(self, packet):
        self.event('RX', packet)
        if len(packet) < 4 or crc16(packet[:-2]) != int.from_bytes(packet[-2:], 'little'):
            self.log.write(json.dumps({'error': 'Invalid RTU frame'}) + '\n')
            return
        data = packet[:-2]
        now = time.monotonic()
        if data[0] == self.config['north_unit']:
            self.responses.append(data)
        elif data[0] in self.slaves and len(data) >= 6:
            # 串口适配器必须关闭本地回显，否则应答会被误认为请求。
            response = self.slaves[data[0]].reply(data)
            self.frames.append((now, data))
            if response:
                time.sleep(self.config['reply_delay_ms'] / 1000)
                self.send(response)

    def pump(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            chunk = self.serial.read(256)
            now = time.monotonic()
            if chunk:
                self.buffer.extend(chunk)
                self.last_byte = self.last_bus = now
            if self.buffer and now - self.last_byte >= self.config['frame_gap_ms'] / 1000:
                packet = bytes(self.buffer)
                self.buffer.clear()
                self.dispatch(packet)

    def request(self, payload):
        # 先处理当前南向事务，连续空闲达到保护时间才发起北向事务。
        deadline = time.monotonic() + self.config['timeout_s']
        while time.monotonic() - self.last_bus < self.config['idle_guard_ms'] / 1000 or self.buffer:
            if time.monotonic() > deadline:
                raise AssertionError('Bus did not become idle')
            self.pump(0.01)
        self.responses.clear()
        self.send(frame(bytes([self.config['north_unit']]) + payload))
        deadline = time.monotonic() + self.config['timeout_s']
        while time.monotonic() < deadline:
            self.pump(0.01)
            while self.responses:
                response = self.responses.pop(0)
                if response[1] in (payload[0], payload[0] | 0x80):
                    return response
        raise AssertionError('Northbound response timeout')

    def step(self, step):
        op = step['op']
        if op == 'online':
            self.slaves[step['unit']].online = step['value']
        elif op == 'wait':
            self.pump(step['seconds'])
        elif op == 'poll':
            start = time.monotonic()
            self.pump(step['seconds'])
            requests = self.slaves[step['unit']].requests
            hits = [r for r in requests if r[0] >= start and r[1] == step['fc'] and r[2] == step['address']]
            assert len(hits) >= step.get('min_count', 1), f'Only {len(hits)} matching requests'
        elif op in ('write', 'read'):
            address = step['address']
            fc = step['fc']
            if op == 'read':
                payload = bytes([fc]) + struct.pack('>HH', address, len(step['values']))
            elif fc == 6:
                payload = bytes([6]) + struct.pack('>HH', address, step['values'][0])
            else:
                values = words(step['values'])
                payload = bytes([16]) + struct.pack('>HHB', address, len(step['values']), len(values)) + values
            start = time.monotonic()
            response = self.request(payload)
            if 'exception' in step:
                expected = bytes([self.config['north_unit'], fc | 0x80, step['exception']])
            elif op == 'read':
                values = words(step['values'])
                expected = bytes([self.config['north_unit'], fc, len(values)]) + values
            else:
                expected = bytes([self.config['north_unit']]) + payload[:5]
            assert response == expected, f'Expected {expected.hex(" ")}, got {response.hex(" ")}'
            if step.get('no_south_write'):
                assert not any(t >= start and d[1] in (6, 16) for t, d in self.frames), 'Unexpected southbound write'
            if 'south_values' in step:
                slave = self.slaves[step.get('south_unit', 1)]
                actual = [slave.holding[step['south_address'] + i] for i in range(len(step['south_values']))]
                assert actual == step['south_values'], f'South values: {actual}'
                writes = [d for t, d in self.frames if t >= start and d[0] == step.get('south_unit', 1)
                          and d[1] in (6, 16) and int.from_bytes(d[2:4], 'big') == step['south_address']]
                assert writes, 'No matching southbound write observed; choose a different target value'
        else:
            raise ValueError(f'Unknown operation: {op}')

    def run(self):
        for step in self.config['steps']:
            try:
                self.step(step)
                result = {'name': step['name'], 'passed': True}
            except (AssertionError, ValueError, KeyError) as error:
                result = {'name': step['name'], 'passed': False, 'error': str(error)}
            self.results.append(result)
            print(json.dumps(result), flush=True)
            if not result['passed'] and self.config.get('stop_on_failure', True):
                break
        return self.results


def main():
    parser = argparse.ArgumentParser(description='Shared RS485 north/south loopback test (no local echo)')
    parser.add_argument('--port', required=True, help='Serial port, e.g. COM5 or /dev/ttyUSB0')
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('default.json'))
    parser.add_argument('--output', type=Path, default=Path('build/forward-loopback'))
    args = parser.parse_args()
    import serial  # 仅硬件执行需要 pyserial，离线测试不依赖串口包。
    config = json.loads(args.config.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, config['baudrate'], timeout=0.002,
                       bytesize=8, parity='N', stopbits=1, xonxoff=False, rtscts=False, dsrdtr=False) as port:
        with (args.output / 'frames.jsonl').open('w') as log:
            results = Runner(port, config, log).run()
    (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if len(results) == len(config['steps']) and all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
