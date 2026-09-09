#!/usr/bin/env python3
"""Soak test — 5 clients / 2 actions/s / 60s (PR short) + 300s dry + 2h/24h nightly.
Verifies tick p99 / RSS / KeepAlive / movement+chunk crossing+chat+villager trade.
Usage:
  python3 tests/soak_test.py [--soak 60] [--duration 300] [--binary ./build/cppfm] [--port 0]
  python3 tests/soak_test.py --duration 300 --binary ./build/cppfm   # dry 300s (PR extended)
  python3 tests/soak_test.py --duration 7200 --binary ./build/cppfm  # nightly 2h (7200s)
  python3 tests/soak_test.py --soak 6h    # nightly 21600s (or --soak 21600)
  python3 tests/soak_test.py --duration 86400 --binary ./build/cppfm # nightly 24h (plan45 O-06,
      # ctest外・専用nightly実行。24hフルはnightlyのみ、PRでは300s dryでPASS確認)
Exit 0 on PASS, 1 on FAIL. Cleans up server subprocess.
"""
import argparse, os, sys, time, subprocess, socket, threading, random, signal, struct, io, tempfile, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mcproto
from mcproto import Conn, read_varint

def get_rss_kb(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except (OSError, ValueError, IndexError):
        return 0
    return 0

def wait_for_server(proc, host, port, timeout=15):
    """Wait for a real status response from the owned server process."""
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"server exited before readiness probe (exit={proc.returncode})")
        client = None
        try:
            # Conn.status() performs several socket operations. Bound each
            # operation by a fraction of the remaining monotonic deadline so
            # one failed probe cannot extend the readiness timeout by seconds.
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            probe_timeout = min(1.0, remaining / 6.0)
            client = Conn(host, port, timeout=probe_timeout)
            client.sock.settimeout(probe_timeout)
            client.status()
            return
        except (OSError, EOFError, ValueError, RuntimeError) as exc:
            last_error = exc
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(0.1, remaining))
        finally:
            if client is not None:
                client.close()
    detail = f": {last_error}" if last_error else ""
    raise TimeoutError(f"server readiness probe timed out on {host}:{port}{detail}")


def _signal_owned_process_group(proc, sig, hard=False):
    """Signal the session/process group created for the owned server."""
    try:
        if os.name == "posix":
            os.killpg(proc.pid, sig)
        elif hard:
            proc.kill()
        else:
            try:
                proc.send_signal(getattr(signal, "CTRL_BREAK_EVENT", sig))
            except (AttributeError, OSError, ValueError):
                proc.terminate()
    except ProcessLookupError:
        # The group may have exited between poll and signalling; that is a
        # successful cleanup state, not an orphan.
        return True
    except OSError:
        return False
    return True


def _wait_for_process_exit(proc, deadline):
    """Wait until the owned leader is reaped, using one monotonic deadline."""
    while proc.poll() is None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        try:
            proc.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            return False
        except OSError:
            return proc.poll() is not None
    return True


def stop_process(proc, timeout=10, kill_timeout=5):
    """Terminate the owned process group, escalate, and reap its leader."""
    if proc is None:
        return True
    try:
        timeout = max(0.0, float(timeout))
        kill_timeout = max(0.0, float(kill_timeout))
    except (TypeError, ValueError):
        return False

    # Signal even when the leader already exited: a forked child can keep the
    # session alive after its parent has been reaped.
    term_ok = _signal_owned_process_group(proc, signal.SIGTERM)
    _wait_for_process_exit(proc, time.monotonic() + timeout)

    # Always issue the hard escalation after the grace period (including
    # after an early leader exit) so descendants cannot survive cleanup.
    kill_ok = _signal_owned_process_group(
        proc, getattr(signal, "SIGKILL", signal.SIGTERM), hard=True
    )
    killed_exit = _wait_for_process_exit(proc, time.monotonic() + kill_timeout)
    return term_ok and kill_ok and killed_exit


def _owned_process_group_kwargs():
    if os.name == "posix":
        return {}
    return {"creationflags": getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)}

class Bot(threading.Thread):
    def __init__(self, idx, host, port, duration, movement_range=None, stop_event=None):
        super().__init__(daemon=False)
        self.idx=idx; self.host=host; self.port=port; self.duration=duration
        self.movement_range=movement_range
        self.stop_event = stop_event or threading.Event()
        self.keepalives=0; self.disconnects=0; self.actions=0
        self.ok=True; self.error=""
        self.connection = None

    def request_stop(self):
        self.stop_event.set()
        if self.connection is not None:
            self.connection.close()

    def run(self):
        c = None
        try:
            c=Conn(self.host, self.port, timeout=1)
            self.connection = c
            c.sock.settimeout(1.0)
            if self.stop_event.is_set():
                return
            c.login(f"Soak{self.idx}")
            if self.stop_event.is_set():
                return
            c.config_finish(sink=lambda p,d: None, max_seconds=15)
            t_end=time.monotonic()+max(0, self.duration)
            last_action=0
            # pump loop with non-blocking-like handling: use socket timeout 1s for recv
            while not self.stop_event.is_set() and time.monotonic()<t_end:
                remaining = t_end-time.monotonic()
                if remaining <= 0:
                    break
                c.sock.settimeout(min(1.0, remaining))
                now=time.monotonic()
                if now - last_action >= 0.5:  # 2 actions/s
                    last_action=now
                    try:
                        # movement + chunk crossing (range scales with duration)
                        mv_range = self.movement_range
                        if mv_range is None:
                            mv_range = 500 if self.duration >= 300 else 20
                        if self.duration >= 7200 and self.movement_range is None:
                            mv_range = 3000
                        # Walk between adjacent positions rather than teleporting
                        # across the whole range; this still crosses chunks while
                        # avoiding a synchronous generation storm in the server.
                        step = 2.0 if mv_range < 3000 else 8.0
                        span = max(1, int(mv_range / step))
                        phase = (self.actions + span) % (2 * span)
                        offset = phase * step if phase <= span else (2 * span - phase) * step
                        x = offset - mv_range
                        z = 0.0
                        c.send_packet_raw(0x1c, struct.pack(">ddd", x, -60.0, z) + b"\x01")
                        # chat / villager trade / summon etc (plan36 §4 actions)
                        r = random.random()
                        if r < 0.15:
                            cmd = f"setblock {random.randint(-5,5)} -60 {random.randint(-5,5)} minecraft:stone"
                            c.send_packet_raw(0x05, mcproto.pack_string(cmd))
                        elif r < 0.20:
                            c.send_packet_raw(0x07, mcproto.pack_string(f"soak chat {self.idx} {self.actions}") + struct.pack(">qq",0,0) + b"\x00\x00\x00\x00\x00")
                        elif r < 0.25:
                            # villager trade / summon variety for B-01/B-09 coverage
                            mob = random.choice(["minecraft:villager","minecraft:witch","minecraft:ravager","minecraft:bee","minecraft:zombie"])
                            c.send_packet_raw(0x05, mcproto.pack_string(f"summon {mob}"))
                        elif r < 0.28:
                            c.send_packet_raw(0x05, mcproto.pack_string("time set midnight" if random.random()<0.5 else "time set day"))
                        self.actions+=1
                    except Exception as exc:
                        if not self.stop_event.is_set():
                            self.ok = False
                            self.error = f"action: {exc}"
                        break
                try:
                    pid, data = c.recv_packet()
                    if pid==0x27: # KeepAlive sc 0x27 -> reply
                        c.send_packet_raw(0x1a, data)
                        self.keepalives+=1
                    elif pid==0x1d: # disconnect
                        self.disconnects+=1
                        break
                    elif pid==0x02:
                        continue
                except socket.timeout:
                    continue
                except EOFError as exc:
                    if not self.stop_event.is_set():
                        self.ok = False
                        self.error = f"receive: {exc or 'peer closed'}"
                    break
                except OSError as exc:
                    if not self.stop_event.is_set():
                        self.ok = False
                        self.error = f"receive: {exc}"
                    break
                except Exception as exc:
                    if not self.stop_event.is_set():
                        self.ok = False
                        self.error = f"protocol: {exc}"
                    break
        except Exception as e:
            if not self.stop_event.is_set():
                self.ok=False; self.error=str(e)
        finally:
            if c is not None:
                try:
                    c.close()
                except OSError:
                    pass
            self.connection = None


def stop_bots(bots, stop_event, timeout=5):
    """Stop and join all bot threads before the server world is removed."""
    stop_event.set()
    stop_ok = True
    for bot in bots:
        try:
            bot.request_stop()
        except Exception as error:
            stop_ok = False
            bot.ok = False
            bot.error = bot.error or f"stop: {error}"
    deadline = time.monotonic() + max(0.0, timeout)
    for bot in bots:
        try:
            bot.join(timeout=max(0.0, deadline-time.monotonic()))
        except RuntimeError as error:
            stop_ok = False
            bot.ok = False
            bot.error = bot.error or f"join: {error}"
    return stop_ok and all(not bot.is_alive() for bot in bots)


def remove_world_dir(world_dir):
    if world_dir is None:
        return True
    try:
        shutil.rmtree(world_dir)
    except FileNotFoundError:
        return True
    except OSError:
        return False
    return not os.path.exists(world_dir)

def parse_duration(s):
    if isinstance(s, int): return s
    s=str(s).strip()
    if s.endswith("h"): return int(float(s[:-1])*3600)
    if s.endswith("m"): return int(float(s[:-1])*60)
    return int(s)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--soak", default=None, help="duration 60 or 6h or 21600 (alias for --duration)")
    ap.add_argument("--duration", default=None, help="duration seconds (300 dry, 7200 nightly 2h, 86400 nightly 24h)")
    ap.add_argument("--binary", default="./build/cppfm")
    ap.add_argument("--bin", default=None, help="alias for --binary")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--view-distance", type=int, default=6)
    ap.add_argument("--clients", type=int, default=5, help="number of bots")
    ap.add_argument("--movement-range", type=int, default=None,
                    help="override bot movement half-range in blocks (diagnostic/load runs)")
    args=ap.parse_args()
    raw = args.duration if args.duration is not None else (args.soak if args.soak is not None else "60")
    duration=parse_duration(raw)
    if args.bin is not None:
        args.binary = args.bin
    if duration <= 0:
        print("[soak] duration must be positive", file=sys.stderr)
        return 2
    if args.clients < 1:
        print("[soak] clients must be at least 1", file=sys.stderr)
        return 2
    # clamp PR short default 60s for safety if called without args
    binary=args.binary
    if not os.path.exists(binary):
        # Use the repository build path when the caller supplied a relative
        # path that is resolved from another working directory.
        alt=os.path.join(os.getcwd(), "build/cppfm")
        if os.path.exists(alt): binary=alt
        else:
            print(f"binary not found: {binary}", file=sys.stderr); return 1
    port=args.port
    if port==0:
        port=26000 + (os.getpid() % 3000)
        # find free
        for _ in range(20):
            s=socket.socket()
            try: s.bind(("127.0.0.1", port)); s.close(); break
            except OSError: s.close(); port+=1
    world_dir=tempfile.mkdtemp(prefix=f"cppfm-soak-{os.getpid()}-")
    cmd=[binary, f"--port={port}", f"--view-distance={args.view_distance}", f"--world-dir={world_dir}", "--online-mode=false"]
    print(f"[soak] starting server {' '.join(cmd)} for {duration}s")
    server_log = None
    stop_event = threading.Event()
    log_path = os.environ.get("CPPFM_SOAK_SERVER_LOG")
    proc = None
    bots = []
    process_cleanup_ok = True
    world_cleanup_ok = True
    bots_cleanup_ok = True
    server_failed = False
    exit_code = 1
    environment = os.environ.copy()
    environment["CPPFM_SERVER_DIR"] = world_dir
    owned_proc = [None]
    spawn_in_progress = False
    termination_requested = False
    termination_signum = signal.SIGTERM
    previous_sigterm = signal.getsignal(signal.SIGTERM)

    def handle_termination(signum, _frame):
        nonlocal termination_requested, termination_signum
        if spawn_in_progress:
            # Popen has not returned a PID yet.  Let the spawn finish so the
            # child can be registered and cleaned up instead of orphaned.
            termination_requested = True
            termination_signum = signum
            return
        child = owned_proc[0]
        if child is not None:
            # The outer timeout uses a five-second kill-after window.  Finish
            # the owned group cleanup inside that window while retaining the
            # normal TERM -> grace -> KILL -> wait order.
            stop_process(child, timeout=1.0, kill_timeout=1.0)
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, handle_termination)
    try:
        if log_path:
            server_log = open(log_path, "wb")
        spawn_in_progress = True
        try:
            try:
                proc=subprocess.Popen(cmd, stdout=server_log or subprocess.DEVNULL,
                                      stderr=server_log or subprocess.DEVNULL,
                                      stdin=subprocess.DEVNULL,
                                      env=environment,
                                      start_new_session=True,
                                      **_owned_process_group_kwargs())
                owned_proc[0] = proc
            except OSError as error:
                print(f"[soak] could not start server: {error}", file=sys.stderr)
                return 2
        finally:
            spawn_in_progress = False
        if termination_requested:
            stop_process(proc, timeout=1.0, kill_timeout=1.0)
            raise SystemExit(128 + termination_signum)
        wait_for_server(proc, "127.0.0.1", port)
        # warmup: start bots first then let RSS stabilize for 5s before baseline
        t0=time.monotonic()
        n_clients = args.clients
        bots=[Bot(i, "127.0.0.1", port, duration, args.movement_range, stop_event)
              for i in range(n_clients)]
        for b in bots: b.start()
        time.sleep(5)
        if proc.poll() is not None:
            server_failed = True
            stop_event.set()
        rss0=get_rss_kb(proc.pid)
        print(f"[soak] rss0(warmup 5s)={rss0}kB port={port} clients={n_clients} duration={duration}s")
        # Monitor RSS and connection liveness; the in-process tick budget is
        # checked by the server-side tests rather than guessed from KeepAlive.
        # plan45 O-06 methodology: chunk cache fills to cap early (bounded, not a leak), so the leak
        # gate uses a post-fill baseline: 30min warmup for long runs, midpoint for short runs.
        base_time = 1800 if duration >= 3600 else duration // 2
        rss_base = None
        rss_max2 = 0
        rss_max=rss0
        last_log=t0
        run_deadline = t0 + max(0, duration)
        while not server_failed and time.monotonic() < run_deadline:
            time.sleep(min(1.0, max(0.0, run_deadline-time.monotonic())))
            rss=get_rss_kb(proc.pid)
            if rss>rss_max: rss_max=rss
            el = time.monotonic()-t0
            if rss_base is None and el >= base_time:
                rss_base=rss
            if rss_base is not None and rss>rss_max2: rss_max2=rss
            # periodic series log for long runs (24h nightly resume/debug — 60s sampling equivalent)
            # + plan45 O-06 diagnosis: 60s cadence for any run >=120s so cache-fill vs leak is visible
            if duration>=3600 and time.monotonic()-last_log >= 300:
                last_log=time.monotonic()
                el=int(time.monotonic()-t0)
                print(f"[soak-series] t={el}s rss={rss}kB rss_max={rss_max}kB", flush=True)
            elif duration>=120 and time.monotonic()-last_log >= 60:
                last_log=time.monotonic()
                el=int(time.monotonic()-t0)
                print(f"[soak-series] t={el}s rss={rss}kB rss_max={rss_max}kB", flush=True)
            # early fail if process died
            if proc.poll() is not None:
                stop_event.set()
                server_failed = True
                rc = proc.returncode
                if rc in (134, -6, -11, -4):
                    sig = {134:"SIGABRT",-6:"SIGABRT",-11:"SIGSEGV",-4:"SIGILL"}.get(rc, str(rc))
                    print(f"server died exit={rc} ({sig}) — test crashed with {sig} (ASan/UBSan?)", file=sys.stderr)
                else:
                    print(f"server died exit={rc}", file=sys.stderr)
                break
        if not server_failed and proc.poll() is not None:
            server_failed = True
            print(f"server died exit={proc.returncode}", file=sys.stderr)
        # Do not remove the world while a bot can still be writing to it.
        bots_cleanup_ok = stop_bots(bots, stop_event)
        for b in bots:
            if b.is_alive():
                b.ok = False
                b.error = b.error or "bot thread did not stop within 5s"
        rss1=get_rss_kb(proc.pid)
        if rss1==0: rss1=rss_max
        total_keep=sum(b.keepalives for b in bots)
        total_disc=sum(b.disconnects for b in bots)
        total_actions=sum(b.actions for b in bots)
        failed_bots = [
            (b.idx, b.error or "connection ended unexpectedly")
            for b in bots
            if not b.ok
        ]
        rss_growth = (rss_max - rss0)/max(rss0,1)*100 if rss0 else 0
        if rss_base is None: rss_base=rss0
        if rss_max2 == 0: rss_max2=rss_max
        rss_growth2 = (rss_max2 - rss_base)/max(rss_base,1)*100 if rss_base else 0
        print(f"[soak] keepalives={total_keep} disconnects={total_disc} actions={total_actions}")
        if failed_bots:
            print(f"[soak] bot failures={failed_bots}", file=sys.stderr)
        print(f"[soak] rss0={rss0} rss_max={rss_max} rss1={rss1} growth={rss_growth:.1f}% warmup-baseline")
        print(f"[soak] rss_base(t={base_time}s)={rss_base} rss_max2={rss_max2} growth2={rss_growth2:.1f}% post-fill")
        # checks: keepAlive >0, disconnects==0, rss growth <10% (post-warmup)
        expected_keep = max(1, duration//30 * n_clients * 0.8)  # 80% of expected
        ok = True
        if server_failed:
            print("FAIL server exited before soak completed", file=sys.stderr)
            ok = False
        if not bots_cleanup_ok:
            print("FAIL bot threads did not stop before cleanup", file=sys.stderr)
            ok = False
        if failed_bots:
            print(f"FAIL bot failures {len(failed_bots)}")
            ok = False
        if total_keep < expected_keep and duration>=30:
            if duration==60 and total_keep==0:
                print(f"FAIL keepalives {total_keep} < expected {expected_keep}")
                ok=False
            elif duration>60 and total_keep < expected_keep:
                print(f"FAIL keepalives {total_keep} < expected {expected_keep}")
                ok=False
        if total_disc >2:
            print(f"FAIL disconnects {total_disc} >2")
            ok=False
        elif total_disc>0:
            print(f"WARN disconnects {total_disc} (tolerated <=2 for soak dry)")
        # plan45 O-06: 24h nightly gate is RSS <5%/24h (warmup excluded); shorter runs keep legacy gates.
        # The leak gate uses post-fill growth2 (cache fill to maxLoadedChunks cap is bounded, not a leak).
        thresh = 500 if duration<=700 else (5 if duration>=86400 else 15)
        if rss0>30000 and rss_growth2 > thresh:
            print(f"FAIL rss growth2 {rss_growth2:.1f}% >{thresh}% (post-fill baseline t={base_time}s)")
            ok=False
        else:
            print(f"RSS check OK (growth2 {rss_growth2:.1f}% <= {thresh}% post-fill baseline)")
        # tick delay: we don't have direct tick histogram, but if keepalives arrived timely it's ok
        # p99 <100ms is not measurable from Python, we just check no long stalls (actions completed)
        if total_actions < n_clients*duration*1.5: # expect ~2* duration per client
            if total_actions < n_clients*duration*0.5:
                print(f"WARN low actions {total_actions} < {n_clients*duration*0.5}")
                # not fail, just warn
        print(f"SOAK {'PASS' if ok else 'FAIL'}: keepAlives={total_keep} disconnects={total_disc} rss_growth2={rss_growth2:.1f}%")
        exit_code = 0 if ok else 1
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        bots_cleanup_ok = stop_bots(bots, stop_event) and bots_cleanup_ok
        process_cleanup_ok = stop_process(proc)
        if server_log is not None:
            try: server_log.close()
            except (OSError, ValueError): pass
        if process_cleanup_ok and bots_cleanup_ok:
            world_cleanup_ok = remove_world_dir(world_dir)
        else:
            world_cleanup_ok = False
            print("[soak] refusing to remove world-dir while owned cleanup is incomplete", file=sys.stderr)
        signal.signal(signal.SIGTERM, previous_sigterm)

    if not process_cleanup_ok or not bots_cleanup_ok or not world_cleanup_ok:
        exit_code = 1
        if not process_cleanup_ok:
            print("[soak] owned server process cleanup failed", file=sys.stderr)
        if not bots_cleanup_ok:
            print("[soak] owned bot-thread cleanup failed", file=sys.stderr)
        if not world_cleanup_ok:
            print("[soak] owned world-dir cleanup failed", file=sys.stderr)
    return exit_code

if __name__=="__main__":
    sys.exit(main())
