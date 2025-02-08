import subprocess as sp
import shlex
import shutil
from root import PROJECT_ROOT
import os
from configparser import ConfigParser, ExtendedInterpolation
import autotest
from util import safe_cast, deduplicate
from server import Server, Host, Guest, LoadGen
from typing import List, Tuple, Callable
import time
import datetime as dt

# all paths in this script are relative to the project root
os.chdir(PROJECT_ROOT)

RESERVATION_HOSTS = []

def log(message: str) -> None:
    print(f"{os.path.basename(__file__)}> {message}", flush=True)

def read_config(config_file: str) -> Tuple[Host, Guest, LoadGen]:
    config: ConfigParser = autotest.setup_and_parse_config_file(config_file)

    host, guest, loadgen = autotest.create_servers(config).values()
    host = safe_cast(Host, host)
    guest = safe_cast(Guest, guest)
    loadgen = safe_cast(LoadGen, loadgen)
    return host, guest, loadgen


def next_utc(time: str, daily_next: bool = True) -> dt.datetime:
    """
    takes "14:03"
    """
    hour, minute = map(int, time.split(":"))
    now = dt.datetime.now(dt.timezone.utc)
    next_time = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
    if daily_next:
        while next_time <= now:
            next_time += dt.timedelta(days=1)
    return next_time


def run(command: str, deadline: dt.datetime|None = None) -> sp.CompletedProcess:
    """
    May raise subprocess.TimeoutExpired exception
    """
    if deadline is None:
        timeout = None
    else:
        timeout = (deadline - dt.datetime.now(dt.timezone.utc)).total_seconds()
    log(f"{command}")
    ret = sp.run(command, timeout=timeout, shell=True)
    return ret


def maybe_notify(message: str) -> None:
    """
    send a notification, but only if the user has set it up for his user
    """
    log(message)
    bin_path = shutil.which("sendtelegram")
    if bin_path is not None:
        _ = run(f"{bin_path} '{message}'")


def maybe_reserve(host: Server, minutes: int) -> None:
    if host.test("command -v hosthog"):
        duration = f"{minutes}min"
        log(f"Reserving {host.fqdn} for {duration}")
        host.exec(f"sudo hosthog claim -e {duration} scheduled vmux measurements")
        host.exec("sudo hosthog hog")


def maybe_reserve_hosts(minutes: int) -> None:
    for host in RESERVATION_HOSTS:
        maybe_reserve(host, minutes)


def is_reserved(host: Server) -> bool:
    if host.test("command -v hosthog"):
        output = host.exec("sudo hosthog status")
        return "This system has been hogged by" in output
    else:
        # hosthog reservation system not available
        return False


def free_reservations() -> None:
    for host in RESERVATION_HOSTS:
        host.exec("sudo hosthog release")


def reboot(host: Server) -> None:
    """
    reboot, wait until online
    """
    # give users a grace warning shortly before reboot
    grace_period_min = 2
    uptime = int(host.uptime())
    host.exec(f"sudo shutdown -r +{grace_period_min}") # reboot in 10 minutes. Notifies users.
    log(f"Waiting for {grace_period_min}min before rebooting {host.fqdn}")
    time.sleep(grace_period_min*60)

    # expect host to come back online within a timeout
    reboot_timeout_min = 10
    log(f"Waiting for completion of reboot (timeout {reboot_timeout_min}min)")
    high_uptime = uptime + grace_period_min * 60 * 0.95
    # high_uptime = uptime
    command = "[[ $(awk -F. '{print $1}' /proc/uptime) -lt " + str(high_uptime) + " ]]"
    host.wait_for_success(command, timeout=reboot_timeout_min*60)


def configure_cpus(host: Server) -> None:
    host.exec("echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo || true")
    host.exec("echo 100 | sudo tee /sys/devices/system/cpu/intel_pstate/min_perf_pct || true")
    host.exec("echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost || true")


class Schedule:

    timeslots: List[Tuple[str, str]] # List(begin, end) times like [("20:58", "20:59")]
    reserver: Callable[[int], None] # reserver(minutes: int): function that reserves relevant resources for minutes

    def __init__(self, timeslots: List[Tuple[str, str]], reserver: Callable[[int], None] = lambda _: None):
        self.timeslots = timeslots
        self.reserver = reserver


    def next_timeslot(self) -> Tuple[dt.datetime, dt.datetime]:
        now = dt.datetime.now(dt.timezone.utc)
        nearest_next_begin = next_utc(self.timeslots[0][0])
        nearest_next_end = next_utc(self.timeslots[0][1])
        for begin, end in self.timeslots:
            # are we already within a timeslot?
            today_begin = next_utc(begin, daily_next=False)
            today_end = next_utc(end, daily_next=False)
            if today_begin < now and now < today_end:
                # we can run right now
                return today_begin, today_end

            next_begin = next_utc(begin)
            next_end = next_utc(end)
            if next_begin < nearest_next_begin:
                nearest_next_begin = next_begin
                nearest_next_end = next_end

        return nearest_next_begin, nearest_next_end

    def run(self, command: str) -> sp.CompletedProcess:
        """
        Run command, but only during self.timeslots.
        Kill and restart command until it completed.
        """
        while True:
            begin, end = self.next_timeslot()

            duration_until = begin - dt.datetime.now(dt.timezone.utc)
            if duration_until.total_seconds() > 0:
                log(f"Pausing for {duration_until.total_seconds()/60/60:.1f}h (until {begin} UTC)")
                time.sleep(duration_until.total_seconds())

            # it is FROM time. Start scheduling tests
            try:
                log(f"Reserving resources until {end} UTC")
                reserve_minutes = int((end - dt.datetime.now(dt.timezone.utc)).total_seconds() / 60) + 1
                self.reserver(reserve_minutes)
                log(f"Running test until {end} UTC")
                ret = run(command, deadline=end)
            except sp.TimeoutExpired:
                log(f"Aborted test after deadline ({end} UTC)")
            else:
                break # finally we managed to run the command to completion

        return ret

def main():
    CONFIG_PREFIX = "autotest_amy_wilfred"
    host, _, loadgen = read_config(f"./test/conf/{CONFIG_PREFIX}.cfg")
    RESERVATION_HOSTS.append(host)

    def reserve_reboot(minutes: int):
        if not is_reserved(host):
            maybe_notify(f"reproduce.py reserving for {minutes/60:.1f}h")
            reboot(host)
        else:
            maybe_notify(f"reproduce.py renewing reservation for {minutes/60:.1f}h")
            # no need to reboot, we trust the server since it was reserved for us already
            free_reservations()
        maybe_reserve_hosts(minutes)
        configure_cpus(host)
        configure_cpus(loadgen)


    # run("sleep 10")
    # run("echo foo; sleep 70; echo bar")
    timeslots = [
        # TODO ranges that cross day boundaries don't work
        ("00:01", "08:00"),
    ]
    schedule = Schedule(timeslots, reserver=reserve_reboot)
    # schedule.run(f"echo foo; sleep {70}; echo bar")

    maybe_notify("reproduce.py tests start")

    ret = schedule.run(f"python3 ./test/autotest -vvv -c test/conf/{CONFIG_PREFIX}.cfg test-load-lat-file -t test/conf/tests_multihost.cfg")
    maybe_notify(f"autotest normal {ret.returncode}")

    ret = schedule.run(f"python3 ./test/autotest -vvv -c test/conf/{CONFIG_PREFIX}_medium.cfg test-load-lat-file -t test/conf/tests_scalable_multihost.cfg")
    maybe_notify(f"autotest medium {ret.returncode}")

    ret = schedule.run(f"python3 ./test/src/measure.py -c test/conf/{CONFIG_PREFIX}_medium.cfg -vvv -o ./out-vmux0.0.11")
    maybe_notify(f"measure medium {ret.returncode}")

    maybe_notify("reproduce.py done")


if __name__ == "__main__":
    autotest.setup_logging2(3)
    try:
        main()
    except Exception as e:
        log("reproduce.py failed:")
        print(e.with_traceback())
        maybe_notify("reproduce.py failed")
    free_reservations()
