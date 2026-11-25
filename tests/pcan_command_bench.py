"""008.17 disabled-power bench helper. No bus access unless an explicit live mode.
PCAN ABI reference: PEAK PCANBasic.h (CAN_Initialize/Read/Write/Uninitialize).
https://www.peak-system.com/support/software-information/development-kits/pcan-basic/
This is a test client, not authenticated control or an operating motor controller.
"""
import argparse
import binascii
import ctypes as ct
import contextlib
import io
import os
from pathlib import Path
import struct
import sys
import time

REQUEST_ID, REPLY_ID, BUILD = 0x321, 0x322, 0x817
PHASES = {1: "STAGING", 2: "ACCEPTED", 3: "CORE_RESULT",
          4: "REJECTED", 5: "BUSY", 6: "STALE", 7: "UNKNOWN"}
CORE = {0: "OK", 4: "EXPIRED", 6: "STALE_EPOCH", 8: "STOP_PENDING",
        9: "FAULT", 10: "NOT_RUNNING", 11: "ALREADY_RUNNING",
        12: "NOT_READY", 14: "APPLIED", 15: "STOPPED"}

class CanMessage(ct.Structure):
    _fields_ = [("ID", ct.c_uint32), ("MSGTYPE", ct.c_uint8),
                ("LEN", ct.c_uint8), ("DATA", ct.c_uint8 * 8)]

class CanTimestamp(ct.Structure):
    _fields_ = [("millis", ct.c_uint32), ("overflow", ct.c_uint16),
                ("micros", ct.c_uint16)]

def driver(path=None):
    if os.name != "nt":
        raise RuntimeError("PCAN live operation requires Windows.")
    dll_path = Path(path) if path else Path(os.environ["WINDIR"]) / "System32" / "PCANBasic.dll"
    if not dll_path.is_absolute() or not dll_path.is_file():
        raise RuntimeError("Provide an existing absolute --dll path matching Python bitness.")
    dll = ct.WinDLL(str(dll_path))
    signatures = {
        "CAN_Initialize": [ct.c_uint16, ct.c_uint16, ct.c_uint8, ct.c_uint32, ct.c_uint16],
        "CAN_Uninitialize": [ct.c_uint16],
        "CAN_Read": [ct.c_uint16, ct.POINTER(CanMessage), ct.POINTER(CanTimestamp)],
        "CAN_Write": [ct.c_uint16, ct.POINTER(CanMessage)],
    }
    for name, args in signatures.items():
        fn = getattr(dll, name)
        fn.argtypes, fn.restype = args, ct.c_uint32
    return dll

class Pcan:
    def __init__(self, channel, dll_path):
        self.channel, self.dll, self.opened = channel, driver(dll_path), False
    def __enter__(self):
        self.check(self.dll.CAN_Initialize(self.channel, 0x1C, 0, 0, 0), "Initialize")
        self.opened = True
        try:
            # Drain only this client's queue; never reset the bus/controller.
            for _ in range(1024):
                if self.read() is None:
                    return self
            raise RuntimeError("RX queue remains busy; stop other senders before testing.")
        except BaseException:
            self.__exit__(None, None, None)
            raise
    def __exit__(self, *unused):
        if self.opened:
            code = self.dll.CAN_Uninitialize(self.channel)
            self.opened = False
            if code:
                print("Uninitialize status 0x%X" % code, file=sys.stderr)
    @staticmethod
    def check(code, operation):
        if code:
            raise RuntimeError("%s failed: PCAN status 0x%X. No command retry." % (operation, code))
    def write(self, payload):
        if len(payload) != 8:
            raise ValueError("Only standard classic eight-byte frames are allowed.")
        msg = CanMessage()
        msg.ID, msg.MSGTYPE, msg.LEN = REQUEST_ID, 0, 8
        msg.DATA[:] = payload
        self.check(self.dll.CAN_Write(self.channel, ct.byref(msg)), "Write")
    def read(self):
        msg, stamp = CanMessage(), CanTimestamp()
        code = self.dll.CAN_Read(self.channel, ct.byref(msg), ct.byref(stamp))
        if code == 0x20:  # PCAN_ERROR_QRCVEMPTY
            return None
        self.check(code, "Read")
        return msg.ID, msg.MSGTYPE, bytes(msg.DATA[:msg.LEN]) if msg.LEN <= 8 else b""

class Client:
    def __init__(self, bus):
        self.bus, self.token = bus, 0x40
    def exchange(self, payload, match, timeout=0.4):
        self.bus.write(payload)  # Exactly once; timeout never retries a command.
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            frame = self.bus.read()
            if frame is None:
                time.sleep(0.001)
                continue
            ident, flags, data = frame
            if ident == REPLY_ID and flags == 0 and len(data) == 8 and match(data):
                return data
        raise RuntimeError("No matching 322 reply. Command outcome may be unknown; use --result, not a new START.")
    def query(self, opcode):
        self.token = (self.token + 1) & 255
        prefix = bytes([0x52, 1, opcode, self.token])
        reply = self.exchange(bytes([0x51, 1, opcode, self.token, 0, 0, 0, 0]),
                              lambda x: x[:4] == prefix)
        return struct.unpack("<I", reply[4:])[0]
    def result(self, request_id, timeout=0.4):
        raw = bytes([0x43, 2, 0x39, 0x21]) + struct.pack("<I", request_id)
        return self.exchange(raw, lambda x: x[:2] == b"A\x02" and struct.unpack("<I", x[4:])[0] == request_id, timeout=timeout)
    def identity(self):
        if self.query(4) != BUILD:
            raise RuntimeError("This helper requires firmware BUILD008.17.")
        if self.query(5) != 1 or self.query(0x0A) != 1:
            raise RuntimeError("Boot identity or CAN time is not trusted. Do not edit Watch to bypass.")
        status = self.query(2)
        flags = (status >> 8) & 255
        if flags & 0x18 != 0x18 or flags & 4:
            raise RuntimeError("Expected disabled-power CommandBench, with outputs OFF.")
        if self.query(3):
            raise RuntimeError("Board fault is present; this tool does not clear faults.")
        first = [self.query(n) for n in range(6, 10)]
        second = [self.query(n) for n in range(6, 10)]
        if first != second or not all(first):
            raise RuntimeError("Boot identity changed or is invalid.")
        epoch = self.query(0x0C)
        if epoch == 0:
            raise RuntimeError("Invalid run epoch.")
        return first, epoch, status

def frames(request_id, boot, epoch, speed, iq_ma, ttl_ms, opcode, token=0x31):
    if not 1 <= request_id <= 0xFFFFFFFF or len(boot) != 4 or not all(1 <= w <= 0xFFFFFFFF for w in boot):
        raise ValueError("Nonzero 32-bit request ID and four boot words required.")
    if not 1 <= epoch <= 0xFFFFFFFF or not -1000 <= speed <= 1000 or not 0 <= iq_ma <= 4000:
        raise ValueError("Bench only: speed +/-1000rpm, Iq 0..4000mA, nonzero epoch.")
    if not 1 <= ttl_ms <= 1000 or opcode not in (1, 2) or not 0 <= token <= 255:
        raise ValueError("TTL 1..1000ms; opcode1 START/2 UPDATE.")
    words = [request_id] + list(boot) + [epoch,
        (speed & 0xFFFF) | (iq_ma << 16), opcode | (ttl_ms << 16)]
    raw = struct.pack("<8I", *words)
    crc = binascii.crc32(raw) & 0xFFFFFFFF
    return [bytes([0x43, 2, 0x30 + i, token]) + struct.pack("<I", w)
            for i, w in enumerate(words + [crc])]

def describe(data):
    phase, detail, request_id = data[2], data[3], struct.unpack("<I", data[4:])[0]
    label = CORE.get(detail, str(detail)) if phase == 3 else ("0x%02X" % detail)
    return "request=%d phase=%s detail=%s raw=%s" % (
        request_id, PHASES.get(phase, str(phase)), label, data.hex(" ").upper())

def transact(client, request_id, boot, epoch, speed, iq_ma, ttl_ms, opcode,
             previous_deadline=None):
    """Send once; UPDATE must also finish within the preceding command's lease."""
    batch = frames(request_id, boot, epoch, speed, iq_ma, ttl_ms, opcode)
    started = time.monotonic()
    deadline = started + ttl_ms / 1000
    def progress(page):
        now = time.monotonic()
        print("Progress: request=%d page=0x%02X elapsed_ms=%.1f remaining_ms=%.1f" %
              (request_id, page, (now - started) * 1000, (deadline - now) * 1000))
    if previous_deadline is not None:
        deadline = min(deadline, previous_deadline)
    for index, payload in enumerate(batch):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            progress(payload[2])
            raise RuntimeError("Local lease deadline reached; no retry/new START. Use --result.")
        try:
            reply = client.exchange(payload,
                lambda x: x[:2] == b"A\x02" and struct.unpack("<I", x[4:])[0] == request_id,
                timeout=min(0.15, remaining))
        except (RuntimeError, OSError):
            progress(payload[2])
            raise
        expected = 0x31 + index
        if index < 8 and not (reply[2] == 1 and reply[3] == expected):
            print(describe(reply))
            progress(payload[2])
            raise RuntimeError("Batch rejected or interrupted; no further command pages sent.")
        if index == 8:
            print(describe(reply))
    receipt_deadline = min(deadline, time.monotonic() + 0.5)
    while reply[2] == 2:
        remaining = receipt_deadline - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(0.01, remaining))
        remaining = receipt_deadline - time.monotonic()
        if remaining <= 0:
            break
        reply = client.result(request_id, timeout=min(0.15, remaining))
    print("Final:", describe(reply))
    progress(0x38)
    if reply[2:4] != b"\x03\x0e":
        raise RuntimeError("Not APPLIED; inspect --result before another request.")
    if time.monotonic() >= deadline:
        raise RuntimeError("APPLIED arrived after the local lease budget; no following command.")
    return deadline


def observe_expiry(client, boot, epoch, ttl_ms, label):
    # No renewal. Wait longer than the latest command's entire lease.
    time.sleep(ttl_ms / 1000 + 0.05)
    state_before = client.query(5)
    boot_before = [client.query(n) for n in range(6, 10)]
    status, new_epoch, fault = client.query(2), client.query(0x0C), client.query(3)
    time_state = client.query(0x0A)
    boot_after = [client.query(n) for n in range(6, 10)]
    state_after = client.query(5)
    if state_before != 1 or state_after != 1 or boot_before != boot or boot_after != boot:
        raise RuntimeError("Boot identity changed; reset occurred, so lease expiry is not proven.")
    print("After lease: status=0x%08X epoch=%d fault=0x%08X time=%d" %
          (status, new_epoch, fault, time_state))
    if status & 0x600 or new_epoch == epoch or fault or time_state != 1:
        raise RuntimeError("Expected permit/output OFF, changed epoch, no fault and trusted CAN time.")
    print("Bench %s expiry observation PASS. This is not a physical-stop test." % label)


def send_once(client, args):
    boot, epoch, _ = client.identity()
    previous = client.result(args.request_id)
    if previous[2] != 7:
        print("Request is already known; no new command sent:", describe(previous))
        return
    transact(client, args.request_id, boot, epoch, args.speed, args.iq_ma, args.ttl_ms,
             1 if args.command == "start" else 2)
    print("APPLIED means core installation only; power remains disabled.")
    observe_expiry(client, boot, epoch, args.ttl_ms, "single-command")


def read_failure_state(client, expected_boot, expected_epoch):
    """Read-only post-failure observation, never a reconstruction of the fault instant."""
    print("Read-only state AFTER failure (no command retry):")
    try:
        state = client.query(5)
        boot = [client.query(n) for n in range(6, 10)]
        epoch, status, fault = client.query(0x0C), client.query(2), client.query(3)
        time_state, rx_age = client.query(0x0A), client.query(0x0B)
        print("boot_state=%d boot=%s matches_start=%s" %
              (state, " ".join("%08X" % word for word in boot), boot == expected_boot))
        print("epoch=%d (start=%d) status=0x%08X fault=0x%08X time_state=%d rx_age_us=%d" %
              (epoch, expected_epoch, status, fault, time_state, rx_age))
    except (RuntimeError, OSError, ValueError) as error:
        print("Read-only diagnostics unavailable:", error)
    print("Keep Run. Read axis_command_result in Watch before reset; do not retry automatically.")


def demo_update(client, request_id):
    """Two new requests, no periodic sender, automatic retry, or reset."""
    # Validate both IDs before even read-only bus traffic (no 32-bit wrap).
    for req in (request_id, request_id + 1):
        frames(req, [1,2,3,4], 1, 100, 500, 1000, 1)
    boot, epoch, status = client.identity()
    if status & 0x600:
        raise RuntimeError("Expected no existing command permit and outputs OFF.")
    for req in (request_id, request_id + 1):
        previous = client.result(req)
        if previous[2] != 7:
            print("Request is already known; no new commands sent:", describe(previous))
            raise RuntimeError("Demo requires two unused consecutive IDs. Do not blindly repeat.")
    print("Demo boot=" + " ".join("%08X" % word for word in boot) + " epoch=%d" % epoch)
    try:
        print("START request=%d: 100 rpm / 500 mA / 1000 ms" % request_id)
        start_deadline = transact(client, request_id, boot, epoch, 100, 500, 1000, 1)
        print("UPDATE request=%d: 200 rpm / 750 mA / 1000 ms" % (request_id + 1))
        # Same identity/epoch. Firmware rejects a reset, revoke or expiry between requests.
        transact(client, request_id + 1, boot, epoch, 200, 750, 1000, 2,
                 previous_deadline=start_deadline)
        print("Both APPLIED: core installation only; no FOC command or power output.")
        observe_expiry(client, boot, epoch, 1000, "START/UPDATE")
        result = client.result(request_id + 1)
        print("Retained UPDATE:", describe(result))
        if result[2:4] != b"\x03\x0e":
            raise RuntimeError("Latest UPDATE result was not retained; demo incomplete.")
        if [client.query(n) for n in range(6, 10)] != boot or client.query(5) != 1:
            raise RuntimeError("Boot changed during result reread; demo incomplete.")
        print("START/UPDATE demo PASS. No command was retried or renewed after UPDATE.")
    except (RuntimeError, OSError):
        read_failure_state(client, boot, epoch)
        raise


def demo_self_test():
    class Clock:
        now = 0.0
        def monotonic(self): return self.now
        def sleep(self, delay): self.now += delay

    class DemoClient:
        def __init__(self, clock, case):
            self.clock, self.case, self.sent, self.committed = clock, case, [], set()
            self.current, self.epoch_read = 0, False
        def identity(self): return [1,2,3,4], 1, 0x1902
        def result(self, req, timeout=0.4):
            self.clock.now += 0.002
            phase, detail = (3,14) if req in self.committed else (7,0)
            if self.case == "known_first" and req == 2: phase, detail = 3,14
            if self.case == "known_second" and req == 3: phase, detail = 3,14
            if req == 2 and req in self.committed:
                if self.case == "start_failure": detail = 12
                if self.case == "late_start": self.clock.now += 1.0
            return bytes([0x41,2,phase,detail]) + struct.pack("<I",req)
        def exchange(self, payload, match, timeout):
            page = payload[2]
            self.sent.append(payload)
            if page == 0x30: self.current = struct.unpack("<I",payload[4:])[0]
            delay = 0.14 if self.case == "slow_update" and self.current == 3 else 0.005
            if delay >= timeout:
                self.clock.now += timeout
                raise RuntimeError("Simulated reply timeout; no retry.")
            self.clock.now += delay
            phase, detail = (2,0) if page == 0x38 else (1,page+1)
            if self.case == "epoch_page" and self.current == 3 and page == 0x35:
                phase, detail = 4,4
            if page == 0x38:
                self.committed.add(self.current)
                if self.case == "update_epoch" and self.current == 3:
                    phase, detail = 4,4
                if self.case == "update_boot" and self.current == 3:
                    phase, detail = 4,3
            result = bytes([0x41,2,phase,detail]) + struct.pack("<I",self.current)
            assert match(result)
            return result
        def query(self, op):
            self.clock.now += 0.001
            if op == 5: return 1
            if op == 0x0A: return 2 if self.case == "time_lost" else 1
            if op == 0x0C:
                self.epoch_read = True
                return 1 if self.case == "no_expiry" else 2
            if 6 <= op <= 9:
                changed = self.case == "reset_mid_check" and self.epoch_read
                return op-5 + (100 if changed else 0)
            return {2:0x1902, 3:0, 0x0B:800}[op]

    original_sleep, original_clock = time.sleep, time.monotonic
    try:
        for case in ("pass", "known_first", "known_second", "start_failure",
                     "late_start", "slow_update", "update_epoch", "update_boot",
                     "reset_mid_check", "no_expiry", "time_lost", "epoch_page"):
            clock = Clock()
            time.sleep, time.monotonic = clock.sleep, clock.monotonic
            client, output = DemoClient(clock,case), io.StringIO()
            try:
                with contextlib.redirect_stdout(output): demo_update(client,2)
            except RuntimeError:
                assert case != "pass", output.getvalue()
                assert "START/UPDATE demo PASS" not in output.getvalue()
            else:
                assert case == "pass"
                assert "START/UPDATE demo PASS" in output.getvalue()
                assert len(client.sent) == 18
                assert client.sent[:9] == frames(2,[1,2,3,4],1,100,500,1000,1)
                assert client.sent[9:] == frames(3,[1,2,3,4],1,200,750,1000,2)
            if case.startswith("known"): assert not client.sent
            if case in ("start_failure", "late_start"): assert len(client.sent) == 9
            if case == "slow_update": assert 9 < len(client.sent) < 18
            if case == "epoch_page":
                assert len(client.sent) == 15
                assert "page=0x35" in output.getvalue()
                assert "Read-only state AFTER failure" in output.getvalue()
                assert "epoch=2 (start=1)" in output.getvalue()
                assert "axis_command_result" in output.getvalue()
                assert 3 not in client.committed
            # No STOP, result query or automatic replacement command mixed into batches.
            assert all(0x30 <= payload[2] <= 0x38 for payload in client.sent)
        before = len(client.sent)
        try: demo_update(client,0xFFFFFFFF)
        except ValueError: pass
        else: raise AssertionError("Wrapped second ID accepted")
        assert len(client.sent) == before
    finally:
        time.sleep, time.monotonic = original_sleep, original_clock


def self_test():
    assert ct.sizeof(CanMessage) == 16 and CanMessage.DATA.offset == 6
    assert ct.sizeof(CanTimestamp) == 8
    assert binascii.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926
    batch = frames(1, [1, 2, 3, 4], 1, 100, 500, 1000, 1)
    assert len(batch) == 9 and all(len(p) == 8 for p in batch)
    assert batch[0].hex() == "4302303101000000"
    assert batch[6].hex() == "430236316400f401"
    assert batch[7].hex() == "430237310100e803"
    raw = b"".join(p[4:] for p in batch[:8])
    assert struct.unpack("<I", batch[8][4:])[0] == binascii.crc32(raw) & 0xFFFFFFFF
    assert struct.unpack("<I", batch[8][4:])[0] == 0x80B3BFF2
    for params in [(0, [1,2,3,4],1,0,0,1,1),(1,[1,2,3,4],1,1001,0,1,1),
                   (1,[1,2,3,4],1,0,4001,1,1),(1,[1,2,3,4],1,0,0,1001,1)]:
        try: frames(*params)
        except ValueError: pass
        else: raise AssertionError("Invalid inputs accepted")
    class FakeBus:
        def __init__(self):
            self.sent = []
            self.items = [(0x322,2,b"\x52\x01\x04\x41\x17\x08\x00\x00"),
                (0x322,0,b"\x52\x01\x04\x40\x17\x08\x00\x00"),
                (0x322,0,b"\x52\x01\x04\x41\x17\x08\x00\x00")]
        def write(self, payload): self.sent.append(payload)
        def read(self): return self.items.pop(0) if self.items else None
    fake = FakeBus()
    assert Client(fake).query(4) == BUILD and len(fake.sent) == 1
    try: Client(fake).exchange(bytes(8), lambda _: True, timeout=0.003)
    except RuntimeError: pass
    else: raise AssertionError("Missing reply accepted")
    assert len(fake.sent) == 2  # No retransmission.
    class FakeClient:
        def __init__(self, reset=False, reject=False):
            self.reset, self.reject, self.sent, self.lookups = reset, reject, [], 0
        def identity(self): return [1,2,3,4], 1, 0x1902
        def result(self, req, timeout=0.4):
            self.lookups += 1
            return bytes([0x41,2,7 if self.lookups == 1 else 3,
                          0 if self.lookups == 1 else 14]) + struct.pack("<I",req)
        def exchange(self, payload, match, timeout):
            self.sent.append(payload)
            page = payload[2]
            reply = bytes([0x41,2,2 if page == 0x38 else 1,
                           0 if page == 0x38 else page+1]) + struct.pack("<I",1)
            if self.reject: reply = bytes([0x41,2,4,3,1,0,0,0])
            assert match(reply)
            return reply
        def query(self, op):
            if op == 5: return 1
            if 6 <= op <= 9: return (op-5) + (100 if self.reset else 0)
            return {2:0x1902, 0x0C:2, 3:0, 0x0A:1}[op]
    options = argparse.Namespace(request_id=1,speed=100,iq_ma=500,ttl_ms=1000,command="start")
    original_sleep = time.sleep
    try:
        time.sleep = lambda _: None  # CPU-only client simulation, never opens DLL/bus.
        for reset, reject in [(False,False),(True,False),(False,True)]:
            simulated = FakeClient(reset,reject)
            output = io.StringIO()
            try:
                with contextlib.redirect_stdout(output): send_once(simulated,options)
            except RuntimeError:
                assert reset or reject
                assert "expiry observation PASS" not in output.getvalue()
            else:
                assert not reset and not reject
                assert "expiry observation PASS" in output.getvalue()
            assert len(simulated.sent) == (1 if reject else 9)
    finally:
        time.sleep = original_sleep
    demo_self_test()
    print("Offline PASS: ABI, CRC, ranges, reply filtering, no-retry timeout, batch/reject/reset evidence.")
    print("Demo offline PASS: 12 scenarios; exact START/UPDATE payloads, unused IDs, shared deadline, rejection/reset/expiry checks.")
    print("Golden batch CRC: 0x%08X" % struct.unpack("<I", batch[-1][4:])[0])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument("--check-driver", action="store_true", help="Load DLL symbols only; no CAN initialize.")
    mode.add_argument("--inspect", action="store_true", help="Read firmware/identity/epoch only.")
    mode.add_argument("--send", action="store_true", help="Send ONE START/UPDATE; never auto-retry.")
    mode.add_argument("--demo-update", action="store_true", help="Two fixed Bench requests: START then UPDATE, followed by expiry.")
    mode.add_argument("--result", type=lambda v:int(v,0), help="Read retained request result only.")
    mode.add_argument("--stop", type=lambda v:int(v,0), help="Send one existing STOP request ID.")
    parser.add_argument("--bench-no-power", action="store_true", help="User confirms bus power disconnected.")
    parser.add_argument("--request-id", type=lambda v:int(v,0), default=1)
    parser.add_argument("--command", choices=["start", "update"], default="start")
    parser.add_argument("--speed", type=int, default=100)
    parser.add_argument("--iq-ma", type=int, default=500)
    parser.add_argument("--ttl-ms", type=int, default=1000)
    parser.add_argument("--channel", type=lambda v:int(v,0), default=0x51)
    parser.add_argument("--dll", help="Absolute vendor PCANBasic.dll path matching Python bitness.")
    args = parser.parse_args()
    if args.self_test:
        self_test(); return
    if args.check_driver:
        driver(args.dll)
        print("DLL ABI symbols available; no CAN channel opened."); return
    if not any([args.inspect, args.send, args.demo_update, args.result is not None, args.stop is not None]):
        parser.print_help(); return
    if (args.send or args.demo_update) and not args.bench_no_power:
        parser.error("--send/--demo-update requires --bench-no-power for the disabled-power bench.")
    if not 1 <= args.channel <= 0xFFFF:
        parser.error("Invalid channel.")
    for value in (args.result, args.stop):
        if value is not None and not 1 <= value <= 0xFFFFFFFF:
            parser.error("Request IDs are 1..0xFFFFFFFF.")
    # Validate proposed command before opening any channel.
    if args.send:
        frames(args.request_id, [1,2,3,4], 1, args.speed, args.iq_ma, args.ttl_ms,
               1 if args.command == "start" else 2)
    if args.demo_update:
        if (args.command, args.speed, args.iq_ma, args.ttl_ms) != ("start", 100, 500, 1000):
            parser.error("--demo-update uses fixed values; omit --command/--speed/--iq-ma/--ttl-ms.")
        for req in (args.request_id, args.request_id + 1):
            frames(req, [1,2,3,4], 1, 100, 500, 1000, 1)
    with Pcan(args.channel, args.dll) as bus:
        client = Client(bus)
        if args.send:
            send_once(client, args)
        elif args.demo_update:
            demo_update(client, args.request_id)
        elif args.inspect:
            boot, epoch, status = client.identity()
            print("BUILD008.17, boot="+" ".join("%08X"%w for w in boot))
            print("epoch=%d status=0x%08X" % (epoch,status))
        elif args.result is not None:
            boot_state = client.query(5)
            boot = [client.query(n) for n in range(6, 10)]
            print("Result belongs to current boot (state=%d): %s" %
                  (boot_state, " ".join("%08X" % w for w in boot)))
            print(describe(client.result(args.result)))
        else:
            payload = b"C\x01\x10\x00" + struct.pack("<I",args.stop)
            reply = client.exchange(payload, lambda x: x[:3] == b"A\x01\x10" and x[4:] == payload[4:])
            print("STOP reply:",reply.hex(" ").upper())

if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError) as error:
        print("ERROR:", error, file=sys.stderr)
        sys.exit(1)

